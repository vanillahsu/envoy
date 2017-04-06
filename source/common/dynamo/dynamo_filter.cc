#include "common/dynamo/dynamo_filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/dynamo/dynamo_request_parser.h"
#include "common/dynamo/dynamo_utility.h"
#include "common/http/codes.h"
#include "common/http/exception.h"
#include "common/http/utility.h"
#include "common/json/json_loader.h"

namespace Dynamo {

Http::FilterHeadersStatus DynamoFilter::decodeHeaders(Http::HeaderMap& headers, bool) {
  if (enabled_) {
    start_decode_ = std::chrono::system_clock::now();
    operation_ = RequestParser::parseOperation(headers);
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus DynamoFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (enabled_ && end_stream) {
    onDecodeComplete(data);
  }

  if (end_stream) {
    return Http::FilterDataStatus::Continue;
  } else {
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }
}

Http::FilterTrailersStatus DynamoFilter::decodeTrailers(Http::HeaderMap&) {
  if (enabled_) {
    Buffer::OwnedImpl empty;
    onDecodeComplete(empty);
  }

  return Http::FilterTrailersStatus::Continue;
}

void DynamoFilter::onDecodeComplete(const Buffer::Instance& data) {
  std::string body = buildBody(decoder_callbacks_->decodingBuffer().get(), data);
  if (!body.empty()) {
    try {
      Json::ObjectPtr json_body = Json::Factory::LoadFromString(body);
      table_descriptor_ = RequestParser::parseTable(operation_, *json_body);
    } catch (const Json::Exception& jsonEx) {
      // Body parsing failed. This should not happen, just put a stat for that.
      stats_.counter(fmt::format("{}invalid_req_body", stat_prefix_)).inc();
    }
  }
}

void DynamoFilter::onEncodeComplete(const Buffer::Instance& data) {
  if (!response_headers_) {
    return;
  }

  uint64_t status = Http::Utility::getResponseStatus(*response_headers_);
  chargeBasicStats(status);

  std::string body = buildBody(encoder_callbacks_->encodingBuffer().get(), data);
  if (!body.empty()) {
    try {
      Json::ObjectPtr json_body = Json::Factory::LoadFromString(body);
      chargeTablePartitionIdStats(*json_body);

      if (Http::CodeUtility::is4xx(status)) {
        chargeFailureSpecificStats(*json_body);
      }
      // Batch Operations will always return status 200 for a partial or full success. Check
      // unprocessed keys to determine partial success.
      // http://docs.aws.amazon.com/amazondynamodb/latest/developerguide/Programming.Errors.html#Programming.Errors.BatchOperations
      if (RequestParser::isBatchOperation(operation_)) {
        chargeUnProcessedKeysStats(*json_body);
      }
    } catch (const Json::Exception&) {
      // Body parsing failed. This should not happen, just put a stat for that.
      stats_.counter(fmt::format("{}invalid_resp_body", stat_prefix_)).inc();
    }
  }
}

Http::FilterHeadersStatus DynamoFilter::encodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  if (enabled_) {
    response_headers_ = &headers;

    if (end_stream) {
      Buffer::OwnedImpl empty;
      onEncodeComplete(empty);
    }
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus DynamoFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (enabled_ && end_stream) {
    onEncodeComplete(data);
  }

  if (end_stream) {
    return Http::FilterDataStatus::Continue;
  } else {
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }
}

Http::FilterTrailersStatus DynamoFilter::encodeTrailers(Http::HeaderMap&) {
  if (enabled_) {
    Buffer::OwnedImpl empty;
    onEncodeComplete(empty);
  }

  return Http::FilterTrailersStatus::Continue;
}

std::string DynamoFilter::buildBody(const Buffer::Instance* buffered,
                                    const Buffer::Instance& last) {
  std::string body;
  if (buffered) {
    uint64_t num_slices = buffered->getRawSlices(nullptr, 0);
    Buffer::RawSlice slices[num_slices];
    buffered->getRawSlices(slices, num_slices);
    for (Buffer::RawSlice& slice : slices) {
      body.append(static_cast<const char*>(slice.mem_), slice.len_);
    }
  }

  uint64_t num_slices = last.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  last.getRawSlices(slices, num_slices);
  for (Buffer::RawSlice& slice : slices) {
    body.append(static_cast<const char*>(slice.mem_), slice.len_);
  }

  return body;
}

void DynamoFilter::chargeBasicStats(uint64_t status) {
  if (!operation_.empty()) {
    chargeStatsPerEntity(operation_, "operation", status);
  } else {
    stats_.counter(fmt::format("{}operation_missing", stat_prefix_)).inc();
  }

  if (!table_descriptor_.table_name.empty()) {
    chargeStatsPerEntity(table_descriptor_.table_name, "table", status);
  } else if (table_descriptor_.is_single_table) {
    stats_.counter(fmt::format("{}table_missing", stat_prefix_)).inc();
  } else {
    stats_.counter(fmt::format("{}multiple_tables", stat_prefix_)).inc();
  }
}

void DynamoFilter::chargeStatsPerEntity(const std::string& entity, const std::string& entity_type,
                                        uint64_t status) {
  std::chrono::milliseconds latency = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now() - start_decode_);

  std::string group_string =
      Http::CodeUtility::groupStringForResponseCode(static_cast<Http::Code>(status));

  stats_.counter(fmt::format("{}{}.{}.upstream_rq_total", stat_prefix_, entity_type, entity)).inc();
  stats_.counter(fmt::format("{}{}.{}.upstream_rq_total_{}", stat_prefix_, entity_type, entity,
                             group_string)).inc();
  stats_.counter(fmt::format("{}{}.{}.upstream_rq_total_{}", stat_prefix_, entity_type, entity,
                             std::to_string(status))).inc();

  stats_.deliverTimingToSinks(
      fmt::format("{}{}.{}.upstream_rq_time", stat_prefix_, entity_type, entity), latency);
  stats_.deliverTimingToSinks(
      fmt::format("{}{}.{}.upstream_rq_time_{}", stat_prefix_, entity_type, entity, group_string),
      latency);
  stats_.deliverTimingToSinks(fmt::format("{}{}.{}.upstream_rq_time_{}", stat_prefix_, entity_type,
                                          entity, std::to_string(status)),
                              latency);
}

void DynamoFilter::chargeUnProcessedKeysStats(const Json::Object& json_body) {
  // The unprocessed keys block contains a list of tables and keys for that table that did not
  // complete apart of the batch operation. Only the table names will be logged for errors.
  std::vector<std::string> unprocessed_tables = RequestParser::parseBatchUnProcessedKeys(json_body);
  for (const std::string& unprocessed_table : unprocessed_tables) {
    stats_.counter(fmt::format("{}error.{}.BatchFailureUnprocessedKeys", stat_prefix_,
                               unprocessed_table)).inc();
  }
}

void DynamoFilter::chargeFailureSpecificStats(const Json::Object& json_body) {
  std::string error_type = RequestParser::parseErrorType(json_body);

  if (!error_type.empty()) {
    if (table_descriptor_.table_name.empty()) {
      stats_.counter(fmt::format("{}error.no_table.{}", stat_prefix_, error_type)).inc();
    } else {
      stats_.counter(fmt::format("{}error.{}.{}", stat_prefix_, table_descriptor_.table_name,
                                 error_type)).inc();
    }
  } else {
    stats_.counter(fmt::format("{}empty_response_body", stat_prefix_)).inc();
  }
}

void DynamoFilter::chargeTablePartitionIdStats(const Json::Object& json_body) {
  if (table_descriptor_.table_name.empty() || operation_.empty()) {
    return;
  }

  std::vector<RequestParser::PartitionDescriptor> partitions =
      RequestParser::parsePartitions(json_body);
  for (const RequestParser::PartitionDescriptor& partition : partitions) {
    std::string stats_string = Utility::buildPartitionStatString(
        stat_prefix_, table_descriptor_.table_name, operation_, partition.partition_id_);
    stats_.counter(stats_string).add(partition.capacity_);
  }
}

} // Dynamo

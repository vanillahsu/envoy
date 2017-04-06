#include "common/redis/command_splitter_impl.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/redis/mocks.h"

using testing::_;
using testing::ByRef;
using testing::DoAll;
using testing::Eq;
using testing::InSequence;
using testing::Ref;
using testing::Return;
using testing::WithArg;

namespace Redis {
namespace CommandSplitter {

class RedisCommandSplitterImplTest : public testing::Test {
public:
  void makeBulkStringArray(RespValue& value, const std::vector<std::string>& strings) {
    std::vector<RespValue> values(strings.size());
    for (uint64_t i = 0; i < strings.size(); i++) {
      values[i].type(RespType::BulkString);
      values[i].asString() = strings[i];
    }

    value.type(RespType::Array);
    value.asArray().swap(values);
  }

  ConnPool::MockInstance* conn_pool_{new ConnPool::MockInstance()};
  Stats::IsolatedStoreImpl store_;
  InstanceImpl splitter_{ConnPool::InstancePtr{conn_pool_}, store_, "redis.foo."};
  MockSplitCallbacks callbacks_;
  SplitRequestPtr handle_;
};

TEST_F(RedisCommandSplitterImplTest, InvalidRequestNotArray) {
  RespValue response;
  response.type(RespType::Error);
  response.asString() = "invalid request";
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  RespValue request;
  EXPECT_EQ(nullptr, splitter_.makeRequest(request, callbacks_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.splitter.invalid_request").value());
}

TEST_F(RedisCommandSplitterImplTest, InvalidRequestArrayTooSmall) {
  RespValue response;
  response.type(RespType::Error);
  response.asString() = "invalid request";
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  RespValue request;
  makeBulkStringArray(request, {"incr"});
  EXPECT_EQ(nullptr, splitter_.makeRequest(request, callbacks_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.splitter.invalid_request").value());
}

TEST_F(RedisCommandSplitterImplTest, InvalidRequestArrayNotStrings) {
  RespValue response;
  response.type(RespType::Error);
  response.asString() = "invalid request";
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  RespValue request;
  makeBulkStringArray(request, {"incr", ""});
  request.asArray()[1].type(RespType::Null);
  EXPECT_EQ(nullptr, splitter_.makeRequest(request, callbacks_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.splitter.invalid_request").value());
}

TEST_F(RedisCommandSplitterImplTest, UnsupportedCommand) {
  RespValue response;
  response.type(RespType::Error);
  response.asString() = "unsupported command 'newcommand'";
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  RespValue request;
  makeBulkStringArray(request, {"newcommand", "hello"});
  EXPECT_EQ(nullptr, splitter_.makeRequest(request, callbacks_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.splitter.unsupported_command").value());
}

class RedisAllParamsToOneServerCommandHandlerTest : public RedisCommandSplitterImplTest {
public:
  void makeRequest(const std::string& hash_key, const RespValue& request) {
    EXPECT_CALL(*conn_pool_, makeRequest(hash_key, Ref(request), _))
        .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callbacks_)), Return(&pool_request_)));
    handle_ = splitter_.makeRequest(request, callbacks_);
  }

  void fail() {
    RespValue response;
    response.type(RespType::Error);
    response.asString() = "upstream failure";
    EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
    pool_callbacks_->onFailure();
  }

  void respond() {
    RespValuePtr response1(new RespValue());
    RespValue* response1_ptr = response1.get();
    EXPECT_CALL(callbacks_, onResponse_(PointeesEq(response1_ptr)));
    pool_callbacks_->onResponse(std::move(response1));
  }

  ConnPool::PoolCallbacks* pool_callbacks_;
  ConnPool::MockPoolRequest pool_request_;
};

TEST_F(RedisAllParamsToOneServerCommandHandlerTest, IncrSuccess) {
  InSequence s;

  RespValue request;
  makeBulkStringArray(request, {"incr", "hello"});
  makeRequest("hello", request);
  EXPECT_NE(nullptr, handle_);

  respond();

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.incr.total").value());
};

TEST_F(RedisAllParamsToOneServerCommandHandlerTest, IncrFail) {
  InSequence s;

  RespValue request;
  makeBulkStringArray(request, {"incr", "hello"});
  makeRequest("hello", request);
  EXPECT_NE(nullptr, handle_);

  fail();
};

TEST_F(RedisAllParamsToOneServerCommandHandlerTest, IncrCancel) {
  InSequence s;

  RespValue request;
  makeBulkStringArray(request, {"incr", "hello"});
  makeRequest("hello", request);
  EXPECT_NE(nullptr, handle_);

  EXPECT_CALL(pool_request_, cancel());
  handle_->cancel();
};

TEST_F(RedisAllParamsToOneServerCommandHandlerTest, IncrNoUpstream) {
  InSequence s;

  RespValue request;
  makeBulkStringArray(request, {"incr", "hello"});
  EXPECT_CALL(*conn_pool_, makeRequest("hello", Ref(request), _)).WillOnce(Return(nullptr));
  RespValue response;
  response.type(RespType::Error);
  response.asString() = "no upstream host";
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(request, callbacks_);
  EXPECT_EQ(nullptr, handle_);
};

class RedisMGETCommandHandlerTest : public RedisCommandSplitterImplTest {
public:
  void setup(uint32_t num_gets, const std::list<uint64_t>& null_handle_indexes) {
    std::vector<std::string> request_strings = {"mget"};
    for (uint32_t i = 0; i < num_gets; i++) {
      request_strings.push_back(std::to_string(i));
    }

    RespValue request;
    makeBulkStringArray(request, request_strings);

    std::vector<RespValue> tmp_expected_requests(num_gets);
    expected_requests_.swap(tmp_expected_requests);
    pool_callbacks_.resize(num_gets);
    std::vector<ConnPool::MockPoolRequest> tmp_pool_requests(num_gets);
    pool_requests_.swap(tmp_pool_requests);
    for (uint32_t i = 0; i < num_gets; i++) {
      makeBulkStringArray(expected_requests_[i], {"get", std::to_string(i)});
      ConnPool::PoolRequest* request_to_use = nullptr;
      if (std::find(null_handle_indexes.begin(), null_handle_indexes.end(), i) ==
          null_handle_indexes.end()) {
        request_to_use = &pool_requests_[i];
      }
      EXPECT_CALL(*conn_pool_, makeRequest(std::to_string(i), Eq(ByRef(expected_requests_[i])), _))
          .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callbacks_[i])), Return(request_to_use)));
    }

    handle_ = splitter_.makeRequest(request, callbacks_);
  }

  std::vector<RespValue> expected_requests_;
  std::vector<ConnPool::PoolCallbacks*> pool_callbacks_;
  std::vector<ConnPool::MockPoolRequest> pool_requests_;
};

TEST_F(RedisMGETCommandHandlerTest, Normal) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  RespValue expected_response;
  expected_response.type(RespType::Array);
  std::vector<RespValue> elements(2);
  elements[0].type(RespType::BulkString);
  elements[0].asString() = "response";
  elements[1].type(RespType::BulkString);
  elements[1].asString() = "5";
  expected_response.asArray().swap(elements);

  RespValuePtr response2(new RespValue());
  response2->type(RespType::BulkString);
  response2->asString() = "5";
  pool_callbacks_[1]->onResponse(std::move(response2));

  RespValuePtr response1(new RespValue());
  response1->type(RespType::BulkString);
  response1->asString() = "response";
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(std::move(response1));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mget.total").value());
};

TEST_F(RedisMGETCommandHandlerTest, NormalWithNull) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  RespValue expected_response;
  expected_response.type(RespType::Array);
  std::vector<RespValue> elements(2);
  elements[0].type(RespType::BulkString);
  elements[0].asString() = "response";
  expected_response.asArray().swap(elements);

  RespValuePtr response2(new RespValue());
  pool_callbacks_[1]->onResponse(std::move(response2));

  RespValuePtr response1(new RespValue());
  response1->type(RespType::BulkString);
  response1->asString() = "response";
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(std::move(response1));
};

TEST_F(RedisMGETCommandHandlerTest, NoUpstreamHostForAll) {
  // No InSequence to avoid making setup() more complicated.

  RespValue expected_response;
  expected_response.type(RespType::Array);
  std::vector<RespValue> elements(2);
  elements[0].type(RespType::Error);
  elements[0].asString() = "no upstream host";
  elements[1].type(RespType::Error);
  elements[1].asString() = "no upstream host";
  expected_response.asArray().swap(elements);

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  setup(2, {0, 1});
  EXPECT_EQ(nullptr, handle_);
};

TEST_F(RedisMGETCommandHandlerTest, NoUpstreamHostForOne) {
  InSequence s;

  setup(2, {0});
  EXPECT_NE(nullptr, handle_);

  RespValue expected_response;
  expected_response.type(RespType::Array);
  std::vector<RespValue> elements(2);
  elements[0].type(RespType::Error);
  elements[0].asString() = "no upstream host";
  elements[1].type(RespType::Error);
  elements[1].asString() = "upstream failure";
  expected_response.asArray().swap(elements);

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[1]->onFailure();
};

TEST_F(RedisMGETCommandHandlerTest, Failure) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  RespValue expected_response;
  expected_response.type(RespType::Array);
  std::vector<RespValue> elements(2);
  elements[0].type(RespType::BulkString);
  elements[0].asString() = "response";
  elements[1].type(RespType::Error);
  elements[1].asString() = "upstream failure";
  expected_response.asArray().swap(elements);

  pool_callbacks_[1]->onFailure();

  RespValuePtr response1(new RespValue());
  response1->type(RespType::BulkString);
  response1->asString() = "response";
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(std::move(response1));
};

TEST_F(RedisMGETCommandHandlerTest, InvalidUpstreamResponse) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  RespValue expected_response;
  expected_response.type(RespType::Array);
  std::vector<RespValue> elements(2);
  elements[0].type(RespType::Error);
  elements[0].asString() = "upstream protocol error";
  elements[1].type(RespType::Error);
  elements[1].asString() = "upstream failure";
  expected_response.asArray().swap(elements);

  pool_callbacks_[1]->onFailure();

  RespValuePtr response1(new RespValue());
  response1->type(RespType::Integer);
  response1->asInteger() = 5;
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(std::move(response1));
};

TEST_F(RedisMGETCommandHandlerTest, Cancel) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  EXPECT_CALL(pool_requests_[0], cancel());
  EXPECT_CALL(pool_requests_[1], cancel());
  handle_->cancel();
};

} // CommandSplitter
} // Redis

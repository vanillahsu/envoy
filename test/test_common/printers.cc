#include "printers.h"

#include "envoy/redis/codec.h"

#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"

namespace Http {
void PrintTo(const HeaderMapImpl& headers, std::ostream* os) {
  headers.iterate([](const HeaderEntry& header, void* context) -> void {
    std::ostream* os = static_cast<std::ostream*>(context);
    *os << "{'" << header.key().c_str() << "','" << header.value().c_str() << "'}";
  }, os);
}

void PrintTo(const HeaderMapPtr& headers, std::ostream* os) {
  PrintTo(*dynamic_cast<HeaderMapImpl*>(headers.get()), os);
}

void PrintTo(const HeaderMap& headers, std::ostream* os) {
  PrintTo(*dynamic_cast<const HeaderMapImpl*>(&headers), os);
}
} // Http

namespace Buffer {
void PrintTo(const Instance& buffer, std::ostream* os) {
  *os << "buffer with size=" << buffer.length();
}

void PrintTo(const Buffer::OwnedImpl& buffer, std::ostream* os) {
  PrintTo(dynamic_cast<const Buffer::Instance&>(buffer), os);
}
} // Buffer

namespace Redis {
void PrintTo(const RespValue& value, std::ostream* os) { *os << value.toString(); }

void PrintTo(const RespValuePtr& value, std::ostream* os) { *os << value->toString(); }
} // Redis

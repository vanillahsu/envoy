#include "common/http/header_map_impl.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/utility.h"

namespace Http {

HeaderString::HeaderString() : type_(Type::Inline) {
  buffer_.dynamic_ = inline_buffer_;
  clear();
}

HeaderString::HeaderString(const LowerCaseString& static_value) : type_(Type::Static) {
  buffer_.static_ = static_value.get().c_str();
  string_length_ = static_value.get().size();
}

HeaderString::HeaderString(const std::string& static_value) : type_(Type::Static) {
  buffer_.static_ = static_value.c_str();
  string_length_ = static_value.size();
}

HeaderString::HeaderString(HeaderString&& move_value) {
  type_ = move_value.type_;
  string_length_ = move_value.string_length_;
  switch (move_value.type_) {
  case Type::Static: {
    buffer_.static_ = move_value.buffer_.static_;
    break;
  }
  case Type::Dynamic: {
    // When we move a dynamic header, we switch the moved header back to its default state (inline).
    buffer_.dynamic_ = move_value.buffer_.dynamic_;
    dynamic_capacity_ = move_value.dynamic_capacity_;
    move_value.type_ = Type::Inline;
    move_value.buffer_.dynamic_ = move_value.inline_buffer_;
    move_value.clear();
    break;
  }
  case Type::Inline: {
    buffer_.dynamic_ = inline_buffer_;
    memcpy(inline_buffer_, move_value.inline_buffer_, string_length_ + 1);
    move_value.string_length_ = 0;
    move_value.inline_buffer_[0] = 0;
    break;
  }
  }
}

HeaderString::~HeaderString() {
  if (type_ == Type::Dynamic) {
    free(buffer_.dynamic_);
  }
}

void HeaderString::append(const char* data, uint32_t size) {
  switch (type_) {
  case Type::Static: {
    // Switch back to inline and fall through. We do not actually append to the static string
    // currently which would require a copy.
    type_ = Type::Inline;
    buffer_.dynamic_ = inline_buffer_;
    string_length_ = 0;
  }

  case Type::Inline: {
    if (size + 1 + string_length_ <= sizeof(inline_buffer_)) {
      // Already inline and the new value fits in inline storage.
      break;
    }

    // Fall through.
  }

  case Type::Dynamic: {
    // We can get here either because we didn't fit in inline or we are already dynamic.
    if (type_ == Type::Inline) {
      uint32_t new_capacity = (string_length_ + size) * 2;
      buffer_.dynamic_ = static_cast<char*>(malloc(new_capacity));
      memcpy(buffer_.dynamic_, inline_buffer_, string_length_);
      dynamic_capacity_ = new_capacity;
      type_ = Type::Dynamic;
    } else {
      if (size + 1 + string_length_ > dynamic_capacity_) {
        // Need to reallocate.
        dynamic_capacity_ = (string_length_ + size) * 2;
        buffer_.dynamic_ = static_cast<char*>(realloc(buffer_.dynamic_, dynamic_capacity_));
      }
    }
  }
  }

  memcpy(buffer_.dynamic_ + string_length_, data, size);
  string_length_ += size;
  buffer_.dynamic_[string_length_] = 0;
}

void HeaderString::clear() {
  switch (type_) {
  case Type::Static: {
    break;
  }
  case Type::Inline: {
    inline_buffer_[0] = 0;
    // fall through
  }
  case Type::Dynamic: {
    string_length_ = 0;
  }
  }
}

void HeaderString::setCopy(const char* data, uint32_t size) {
  switch (type_) {
  case Type::Static: {
    // Switch back to inline and fall through.
    type_ = Type::Inline;
    buffer_.dynamic_ = inline_buffer_;
  }

  case Type::Inline: {
    if (size + 1 <= sizeof(inline_buffer_)) {
      // Already inline and the new value fits in inline storage.
      break;
    }

    // Fall through.
  }

  case Type::Dynamic: {
    // We can get here either because we didn't fit in inline or we are already dynamic.
    if (type_ == Type::Inline) {
      dynamic_capacity_ = size * 2;
      buffer_.dynamic_ = static_cast<char*>(malloc(dynamic_capacity_));
      type_ = Type::Dynamic;
    } else {
      if (size + 1 > dynamic_capacity_) {
        // Need to reallocate. Use free/malloc to avoid the copy since we are about to overwrite.
        dynamic_capacity_ = size * 2;
        free(buffer_.dynamic_);
        buffer_.dynamic_ = static_cast<char*>(malloc(dynamic_capacity_));
      }
    }
  }
  }

  memcpy(buffer_.dynamic_, data, size);
  buffer_.dynamic_[size] = 0;
  string_length_ = size;
}

void HeaderString::setInteger(uint64_t value) {
  switch (type_) {
  case Type::Static: {
    // Switch back to inline and fall through.
    type_ = Type::Inline;
    buffer_.dynamic_ = inline_buffer_;
  }

  case Type::Inline:
  case Type::Dynamic: {
    // Whether dynamic or inline the buffer is guaranteed to be large enough.
    string_length_ = StringUtil::itoa(buffer_.dynamic_, 32, value);
  }
  }
}

HeaderMapImpl::HeaderEntryImpl::HeaderEntryImpl(const LowerCaseString& key) : key_(key) {}

HeaderMapImpl::HeaderEntryImpl::HeaderEntryImpl(const LowerCaseString& key, HeaderString&& value)
    : key_(key), value_(std::move(value)) {}

HeaderMapImpl::HeaderEntryImpl::HeaderEntryImpl(HeaderString&& key, HeaderString&& value)
    : key_(std::move(key)), value_(std::move(value)) {}

void HeaderMapImpl::HeaderEntryImpl::value(const char* value, uint32_t size) {
  value_.setCopy(value, size);
}

void HeaderMapImpl::HeaderEntryImpl::value(const std::string& value) {
  this->value(value.c_str(), static_cast<uint32_t>(value.size()));
}

void HeaderMapImpl::HeaderEntryImpl::value(uint64_t value) { value_.setInteger(value); }

void HeaderMapImpl::HeaderEntryImpl::value(const HeaderEntry& header) {
  value(header.value().c_str(), header.value().size());
}

#define INLINE_HEADER_STATIC_MAP_ENTRY(name)                                                       \
  add(Headers::get().name.get().c_str(), [](HeaderMapImpl & h) -> StaticLookupResponse {           \
    return {&h.inline_headers_.name##_, &Headers::get().name};                                     \
  });

const HeaderMapImpl::StaticLookupTable HeaderMapImpl::static_lookup_table_;

HeaderMapImpl::StaticLookupTable::StaticLookupTable() {
  ALL_INLINE_HEADERS(INLINE_HEADER_STATIC_MAP_ENTRY)

  // Special case where we map a legacy host header to :authority.
  add(Headers::get().HostLegacy.get().c_str(), [](HeaderMapImpl& h) -> StaticLookupResponse {
    return {&h.inline_headers_.Host_, &Headers::get().Host};
  });
}

void HeaderMapImpl::StaticLookupTable::add(const char* key, StaticLookupEntry::EntryCb cb) {
  StaticLookupEntry* current = &root_;
  while (uint8_t c = *key) {
    if (!current->entries_[c]) {
      current->entries_[c].reset(new StaticLookupEntry());
    }

    current = current->entries_[c].get();
    key++;
  }

  current->cb_ = cb;
}

HeaderMapImpl::StaticLookupEntry::EntryCb
HeaderMapImpl::StaticLookupTable::find(const char* key) const {
  const StaticLookupEntry* current = &root_;
  while (uint8_t c = *key) {
    current = current->entries_[c].get();
    if (current) {
      key++;
    } else {
      return nullptr;
    }
  }

  return current->cb_;
}

HeaderMapImpl::HeaderMapImpl() { memset(&inline_headers_, 0, sizeof(inline_headers_)); }

HeaderMapImpl::HeaderMapImpl(const HeaderMap& rhs) : HeaderMapImpl() {
  rhs.iterate([](const HeaderEntry& header, void* context) -> void {
    // TODO(mattklein123) PERF: Avoid copying here is not necessary.
    HeaderString key_string;
    key_string.setCopy(header.key().c_str(), header.key().size());
    HeaderString value_string;
    value_string.setCopy(header.value().c_str(), header.value().size());

    static_cast<HeaderMapImpl*>(context)
        ->addViaMove(std::move(key_string), std::move(value_string));
  }, this);
}

HeaderMapImpl::HeaderMapImpl(
    const std::initializer_list<std::pair<LowerCaseString, std::string>>& values)
    : HeaderMapImpl() {
  for (auto& value : values) {
    HeaderString key_string;
    key_string.setCopy(value.first.get().c_str(), value.first.get().size());
    HeaderString value_string;
    value_string.setCopy(value.second.c_str(), value.second.size());
    addViaMove(std::move(key_string), std::move(value_string));
  }
}

bool HeaderMapImpl::operator==(const HeaderMapImpl& rhs) const {
  if (size() != rhs.size()) {
    return false;
  }

  for (auto i = headers_.begin(), j = rhs.headers_.begin(); i != headers_.end(); ++i, ++j) {
    if (i->key() != j->key().c_str() || i->value() != j->value().c_str()) {
      return false;
    }
  }

  return true;
}

void HeaderMapImpl::insertByKey(HeaderString&& key, HeaderString&& value) {
  StaticLookupEntry::EntryCb cb = static_lookup_table_.find(key.c_str());
  if (cb) {
    // TODO(mattklein123): Currently, for all of the inline headers, we don't support appending. The
    // only inline header where we should be converting multiple headers into a comma delimited
    // list is XFF. This is not a crisis for now but we should allow an inline header to indicate
    // that it should be appended to. In that case, we would do an append here. We can do this in
    // a follow up.
    key.clear();
    StaticLookupResponse static_lookup_response = cb(*this);
    maybeCreateInline(static_lookup_response.entry_, *static_lookup_response.key_,
                      std::move(value));
  } else {
    std::list<HeaderEntryImpl>::iterator i =
        headers_.emplace(headers_.end(), std::move(key), std::move(value));
    i->entry_ = i;
  }
}

void HeaderMapImpl::addViaMove(HeaderString&& key, HeaderString&& value) {
  insertByKey(std::move(key), std::move(value));
}

void HeaderMapImpl::addStatic(const LowerCaseString& key, const std::string& value) {
  HeaderString static_key(key);
  HeaderString static_value(value);
  addViaMove(std::move(static_key), std::move(static_value));
}

void HeaderMapImpl::addStaticKey(const LowerCaseString& key, uint64_t value) {
  HeaderString static_key(key);
  HeaderString new_value;
  new_value.setInteger(value);
  insertByKey(std::move(static_key), std::move(new_value));
  ASSERT(new_value.empty());
}

void HeaderMapImpl::addStaticKey(const LowerCaseString& key, const std::string& value) {
  HeaderString static_key(key);
  HeaderString new_value;
  new_value.setCopy(value.c_str(), value.size());
  insertByKey(std::move(static_key), std::move(new_value));
  ASSERT(new_value.empty());
}

uint64_t HeaderMapImpl::byteSize() const {
  uint64_t byte_size = 0;
  for (const HeaderEntryImpl& header : headers_) {
    byte_size += header.key().size();
    byte_size += header.value().size();
  }

  return byte_size;
}

const HeaderEntry* HeaderMapImpl::get(const LowerCaseString& key) const {
  for (const HeaderEntryImpl& header : headers_) {
    if (header.key() == key.get().c_str()) {
      return &header;
    }
  }

  return nullptr;
}

void HeaderMapImpl::iterate(ConstIterateCb cb, void* context) const {
  for (const HeaderEntryImpl& header : headers_) {
    cb(header, context);
  }
}

void HeaderMapImpl::remove(const LowerCaseString& key) {
  StaticLookupEntry::EntryCb cb = static_lookup_table_.find(key.get().c_str());
  if (cb) {
    StaticLookupResponse static_lookup_response = cb(*this);
    removeInline(static_lookup_response.entry_);
  } else {
    for (auto i = headers_.begin(); i != headers_.end();) {
      if (i->key() == key.get().c_str()) {
        i = headers_.erase(i);
      } else {
        ++i;
      }
    }
  }
}

HeaderMapImpl::HeaderEntryImpl& HeaderMapImpl::maybeCreateInline(HeaderEntryImpl** entry,
                                                                 const LowerCaseString& key) {
  if (*entry) {
    return **entry;
  }

  std::list<HeaderEntryImpl>::iterator i = headers_.emplace(headers_.end(), key);
  i->entry_ = i;
  *entry = &(*i);
  return **entry;
}

HeaderMapImpl::HeaderEntryImpl& HeaderMapImpl::maybeCreateInline(HeaderEntryImpl** entry,
                                                                 const LowerCaseString& key,
                                                                 HeaderString&& value) {
  if (*entry) {
    value.clear();
    return **entry;
  }

  std::list<HeaderEntryImpl>::iterator i = headers_.emplace(headers_.end(), key, std::move(value));
  i->entry_ = i;
  *entry = &(*i);
  return **entry;
}

void HeaderMapImpl::removeInline(HeaderEntryImpl** ptr_to_entry) {
  if (!*ptr_to_entry) {
    return;
  }

  HeaderEntryImpl* entry = *ptr_to_entry;
  *ptr_to_entry = nullptr;
  headers_.erase(entry->entry_);
}

} // Http

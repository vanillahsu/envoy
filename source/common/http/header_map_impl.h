#pragma once

#include "envoy/http/header_map.h"

#include "common/common/non_copyable.h"
#include "common/http/headers.h"

namespace Http {

/**
 * These are definitions of all of the inline header access functions described inside header_map.h
 */
#define DEFINE_INLINE_HEADER_FUNCS(name)                                                           \
public:                                                                                            \
  const HeaderEntry* name() const override { return inline_headers_.name##_; }                     \
  HeaderEntry* name() override { return inline_headers_.name##_; }                                 \
  HeaderEntry& insert##name() override {                                                           \
    return maybeCreateInline(&inline_headers_.name##_, Headers::get().name);                       \
  }                                                                                                \
  void remove##name() override { removeInline(&inline_headers_.name##_); }

#define DEFINE_INLINE_HEADER_STRUCT(name) HeaderEntryImpl* name##_;

/**
 * Implementation of Http::HeaderMap. This is heavily optimized for performance. Roughly, when
 * headers are added to the map, we do a hash lookup to see if it's one of the O(1) headers.
 * If it is, we store a reference to it that can be accessed later directly. Most high performance
 * paths use O(1) direct access. In general, we try to copy as little as possible and allocate as
 * little as possible in any of the paths.
 */
class HeaderMapImpl : public HeaderMap {
public:
  HeaderMapImpl();
  HeaderMapImpl(const std::initializer_list<std::pair<LowerCaseString, std::string>>& values);
  HeaderMapImpl(const HeaderMap& rhs);

  /**
   * Add a header via full move. This is the expected high performance paths for codecs populating
   * a map when receiving.
   */
  void addViaMove(HeaderString&& key, HeaderString&& value);

  /**
   * For testing. Equality is based on equality of the backing list. This is an exact match
   * comparison (order matters).
   */
  bool operator==(const HeaderMapImpl& rhs) const;

  // Http::HeaderMap
  void addStatic(const LowerCaseString& key, const std::string& value) override;
  void addStaticKey(const LowerCaseString& key, uint64_t value) override;
  void addStaticKey(const LowerCaseString& key, const std::string& value) override;
  uint64_t byteSize() const override;
  const HeaderEntry* get(const LowerCaseString& key) const override;
  void iterate(ConstIterateCb cb, void* context) const override;
  void remove(const LowerCaseString& key) override;
  size_t size() const override { return headers_.size(); }

protected:
  struct HeaderEntryImpl : public HeaderEntry, NonCopyable {
    HeaderEntryImpl(const LowerCaseString& key);
    HeaderEntryImpl(const LowerCaseString& key, HeaderString&& value);
    HeaderEntryImpl(HeaderString&& key, HeaderString&& value);

    // HeaderEntry
    const HeaderString& key() const override { return key_; }
    void value(const char* value, uint32_t size) override;
    void value(const std::string& value) override;
    void value(uint64_t value) override;
    void value(const HeaderEntry& header) override;
    const HeaderString& value() const override { return value_; }
    HeaderString& value() override { return value_; }

    HeaderString key_;
    HeaderString value_;
    std::list<HeaderEntryImpl>::iterator entry_;
  };

  struct StaticLookupResponse {
    HeaderEntryImpl** entry_;
    const LowerCaseString* key_;
  };

  struct StaticLookupEntry {
    typedef StaticLookupResponse (*EntryCb)(HeaderMapImpl&);

    EntryCb cb_{};
    std::array<std::unique_ptr<StaticLookupEntry>, 256> entries_;
  };

  /**
   * This is the static lookup table that is used to determine whether a header is one of the O(1)
   * headers. This uses a trie for lookup time at most equal to the size of the incoming string.
   */
  struct StaticLookupTable {
    StaticLookupTable();
    void add(const char* key, StaticLookupEntry::EntryCb cb);
    StaticLookupEntry::EntryCb find(const char* key) const;

    StaticLookupEntry root_;
  };

  static const StaticLookupTable static_lookup_table_;

  struct AllInlineHeaders {
    ALL_INLINE_HEADERS(DEFINE_INLINE_HEADER_STRUCT)
  };

  void insertByKey(HeaderString&& key, HeaderString&& value);
  HeaderEntryImpl& maybeCreateInline(HeaderEntryImpl** entry, const LowerCaseString& key);
  HeaderEntryImpl& maybeCreateInline(HeaderEntryImpl** entry, const LowerCaseString& key,
                                     HeaderString&& value);
  void removeInline(HeaderEntryImpl** entry);

  AllInlineHeaders inline_headers_;
  std::list<HeaderEntryImpl> headers_;

  ALL_INLINE_HEADERS(DEFINE_INLINE_HEADER_FUNCS)
};

typedef std::unique_ptr<HeaderMapImpl> HeaderMapImplPtr;

} // Http

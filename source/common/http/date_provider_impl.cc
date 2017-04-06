#include "common/http/date_provider_impl.h"

namespace Http {

DateFormatter DateProviderImplBase::date_formatter_("%a, %d %b %Y %H:%M:%S GMT");

TlsCachingDateProviderImpl::TlsCachingDateProviderImpl(Event::Dispatcher& dispatcher,
                                                       ThreadLocal::Instance& tls)
    : tls_(tls), tls_slot_(tls.allocateSlot()),
      refresh_timer_(dispatcher.createTimer([this]() -> void { onRefreshDate(); })) {

  onRefreshDate();
}

void TlsCachingDateProviderImpl::onRefreshDate() {
  std::string new_date_string = date_formatter_.now();
  tls_.set(tls_slot_,
           [new_date_string](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
             return std::make_shared<ThreadLocalCachedDate>(new_date_string);
           });

  refresh_timer_->enableTimer(std::chrono::milliseconds(500));
}

void TlsCachingDateProviderImpl::setDateHeader(HeaderMap& headers) {
  headers.insertDate().value(tls_.getTyped<ThreadLocalCachedDate>(tls_slot_).date_string_);
}

void SlowDateProviderImpl::setDateHeader(HeaderMap& headers) {
  headers.insertDate().value(date_formatter_.now());
}

} // Http

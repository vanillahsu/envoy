#include "common/common/assert.h"
#include "envoy/event/dispatcher.h"
#include "server/watchdog_impl.h"

namespace Server {

void WatchDogImpl::startWatchdog(Event::Dispatcher& dispatcher) {
  timer_ = dispatcher.createTimer([this]() -> void {
    this->touch();
    timer_->enableTimer(timer_interval_);
  });
  timer_->enableTimer(timer_interval_);
}

} // Server

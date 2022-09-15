#pragma once

#include <frp/threading/Timer.h>
#include <frp/collections/Dictionary.h>

namespace frp {
    namespace collections {
        class RestartTasksManger {
            typedef std::shared_ptr<boost::asio::deadline_timer>            DeadlineTimerPtr;
            typedef std::unordered_map<void*, DeadlineTimerPtr>             DeadlineTimerTable;

        public:
            inline void                                                     CloseAllRestartTasks() noexcept {
                Dictionary::ReleaseAllPairs(restarts_, 
                    [](DeadlineTimerPtr& timeout) noexcept {
                        frp::threading::Hosting::Cancel(timeout);
                    });
            }

        public:
            template<typename Reference, typename TimeoutHandler>
            inline bool                                                     AddRestartTask(
                const std::shared_ptr<Reference>&                           reference,
                const std::shared_ptr<boost::asio::io_context>&             context,
                const BOOST_ASIO_MOVE_ARG(TimeoutHandler)                   handler,
                int                                                         timeout) noexcept {
                if (!context) {
                    return false;
                }

                if (timeout < 1) {
                    timeout = 1000;
                }

                const TimeoutHandler handler_ = BOOST_ASIO_MOVE_CAST(TimeoutHandler)(constantof(handler));
                const std::shared_ptr<Reference> reference_ = reference;

                const std::shared_ptr<boost::asio::deadline_timer> timeout_ = frp::threading::SetTimeout(context,
                    [reference_, this, handler_](void* key) noexcept {
                        std::shared_ptr<boost::asio::deadline_timer> timeout_;
                        if (Dictionary::TryRemove(restarts_, key, timeout_)) {
                            handler_();
                        }
                    }, timeout);

                const bool success_ = restarts_.insert(std::make_pair(timeout_.get(), timeout_)).second;
                if (!success_) {
                    frp::threading::Hosting::Cancel(timeout_);
                }
                return success_;
            }

            template<typename Reference, typename TimeoutHandler>
            inline bool                                                     AddRestartTask(
                const std::shared_ptr<Reference>&                           reference,
                const std::shared_ptr<frp::threading::Hosting>&             hosting,
                const BOOST_ASIO_MOVE_ARG(TimeoutHandler)                   handler,
                int                                                         timeout) noexcept {
                if (!hosting) {
                    return NULL;
                }

                std::shared_ptr<boost::asio::io_context> context = hosting->GetContext();
                if (!context) {
                    return NULL;
                }
                return AddRestartTask(reference, context, forward0f(handler), timeout);
            }

        private:
            DeadlineTimerTable                                              restarts_;
        };
    }
}
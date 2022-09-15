#pragma once

#include <frp/transmission/ITransmission.h>
#include <frp/collections/ConnectionManager.h>

namespace frp {
    namespace collections {
        template<typename Connection>
        class TransmissionManager : public ConnectionManager<std::shared_ptr<Connection> const> {
        protected:
            typedef frp::transmission::ITransmission                    ITransmission;
            typedef std::shared_ptr<ITransmission>                      TransmissionPtr;
            typedef std::list<TransmissionPtr>                          TransmissionList;

        private:
            typedef std::unordered_map<void*, TransmissionPtr>          TransmissionTable;

        protected:
            inline void                                                 ReleaseAllTransmission() noexcept {
                Dictionary::ReleaseAllPairs(transmissionTable_);
                transmissions_.clear();
                transmissionTable_.clear();
            }
            inline int                                                  GetTransmissionCount() noexcept {
                return transmissionTable_.size();
            }
            inline TransmissionPtr                                      GetTransmission() noexcept {
                typename TransmissionList::iterator tail = transmissions_.begin();
                typename TransmissionList::iterator endl = transmissions_.end();
                if (tail == endl) {
                    return NULL;
                }

                TransmissionPtr transmission = std::move(*tail);
                transmissions_.erase(tail);
                transmissions_.push_back(transmission);
                return transmission;
            }
            inline TransmissionPtr                                      GetBestTransmission() noexcept {
                typename TransmissionList::iterator tail = transmissions_.begin();
                typename TransmissionList::iterator endl = transmissions_.end();
                if (tail == endl) {
                    return NULL;
                }

                if (GetTransmissionCount() == 1) {
                    return *tail;
                }
                
                TransmissionPtr transmission;
                void* key = TransmissionManager::GetBestKey();
                if (!key || !Dictionary::TryGetValue(transmissionTable_, key, transmission)) {
                    transmission = std::move(*tail);
                    transmissions_.erase(tail);
                    transmissions_.push_back(transmission);
                }
                return std::move(transmission);
            }
            inline bool                                                 CloseTransmission(const ITransmission* key) noexcept {
                if (key) {
                    TransmissionPtr transmission;
                    Dictionary::TryRemove(transmissionTable_, (void*)key, transmission);

                    typename TransmissionList::iterator tl_tail = transmissions_.begin();
                    typename TransmissionList::iterator tl_endl = transmissions_.end();
                    for (; tl_tail != tl_endl; tl_tail++) {
                        TransmissionPtr& p = *tl_tail;
                        if (key == p.get()) {
                            transmissions_.erase(tl_tail);
                            break;
                        }
                    }

                    if (transmission) {
                        transmission->Close();
                        return true;
                    }
                }
                return false;
            }
            inline bool                                                 AddTransmission(const TransmissionPtr& transmission) noexcept {
                if (!transmission) {
                    return false;
                }

                void* key = transmission.get();
                if (Dictionary::ContainsKey(transmissionTable_, key)) {
                    return false;
                }

                if (!Dictionary::TryAdd(transmissionTable_, key, transmission)) {
                    return false;
                }

                transmissions_.push_back(transmission);
                return true;
            }

        private:
            TransmissionList                                            transmissions_;
            TransmissionTable                                           transmissionTable_;
        };
    }
}
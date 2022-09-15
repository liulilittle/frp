#pragma once

#include <frp/collections/Dictionary.h>

namespace frp {
    namespace collections {
        template<class Connection> class ConnectionManager;
        template<class Connection>
        class ConnectionManager<std::shared_ptr<Connection> const> {
        protected:
            typedef std::shared_ptr<Connection>                 ConnectionPtr;

        private:
            typedef std::unordered_map<int, ConnectionPtr>      ConnectionTable;
            typedef std::unordered_map<void*, ConnectionTable>  ConnectionKeyTable;

        public:
            inline ConnectionManager() noexcept 
                : aid_(0) {
                
            }

        protected:
            inline bool                                         ReleaseConnection(void* key, int id) noexcept {
                ConnectionTable* connections_ = GetConnectionTable(key);
                if (!connections_) {
                    return false;
                }

                ConnectionPtr connection;
                if (!Dictionary::TryRemove(*connections_, id, connection)) {
                    return false;
                }

                /* There are no more connections. */
                if (connections_->empty()) {
                    ReleaseAllConnection(key);
                }

                /* Closing the current connection is only valid. */
                connection->Close();
                return true;
            }
            inline void                                         ReleaseAllConnection() noexcept {
                Dictionary::ReleaseAllPairs2Layer(connectionss_);
            }
            inline void                                         ReleaseAllConnection(void* key) noexcept {
                ConnectionTable connections;
                if (Dictionary::TryRemove(connectionss_, key, connections)) {
                    Dictionary::ReleaseAllPairs(connections);
                }
            }
            inline ConnectionPtr                                GetConnection(void* key, int id) noexcept {
                ConnectionTable* connections = GetConnectionTable(key);
                if (!connections) {
                    return NULL;
                }

                ConnectionPtr connection;
                Dictionary::TryGetValue(*connections, id, connection);
                return connection;
            }
            inline int                                          NewConnectionId() noexcept {
                for (;;) {
                    int id = ++aid_;
                    if (!id) {
                        continue;
                    }

                    typename ConnectionKeyTable::iterator tail = connectionss_.begin();
                    typename ConnectionKeyTable::iterator endl = connectionss_.end();
                    if (tail == endl) {
                        return id;
                    }

                    for (; tail != endl; tail++) {
                        if (!Dictionary::ContainsKey(tail->second, id)) {
                            return id;
                        }
                    }
                }
            }
            inline bool                                         AddConnection(void* key, int id, const ConnectionPtr& connection) noexcept {
                if (!key || !id || !connection) {
                    return false;
                }

                ConnectionTable& connections = connectionss_[key];
                if (Dictionary::TryAdd(connections, id, connection)) {
                    return true;
                }
                elif(connections.empty()) {
                    ReleaseAllConnection(key);
                }
                return false;
            }
            inline void*                                        GetBestKey() noexcept {
                typename ConnectionKeyTable::iterator tail = connectionss_.begin();
                typename ConnectionKeyTable::iterator endl = connectionss_.end();

                void* key = NULL;
                std::size_t key_size = 0;

                for (; tail != endl; tail++) {
                    std::size_t nxt_size = tail->second.size();
                    if (!key || key_size > nxt_size) {
                        key = tail->first;
                        key_size = nxt_size;
                    }
                }
                return key;
            }

        private:
            inline ConnectionTable*                             GetConnectionTable(void* key) noexcept {
                ConnectionTable* out = NULL;
                Dictionary::TryGetValuePointer(connectionss_, key, out);
                return out;
            }

        private:
            std::atomic<int>                                    aid_;
            ConnectionKeyTable                                  connectionss_;
        };
    }
}
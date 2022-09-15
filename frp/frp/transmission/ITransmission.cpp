#include <frp/transmission/ITransmission.h>

namespace frp {
    namespace transmission {
        void ITransmission::Close() noexcept {
            Dispose();
        }
    }
}
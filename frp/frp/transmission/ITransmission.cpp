#include <frp/transmission/ITransmission.h>

namespace frp {
    namespace transmission {
        ITransmission::~ITransmission() noexcept = default;

        void ITransmission::Close() noexcept {
            Dispose();
        }
    }
}
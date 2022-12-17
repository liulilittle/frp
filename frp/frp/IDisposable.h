#pragma once

#include <frp/Reference.h>

namespace frp {
    class IDisposable : public Reference {
    public:
        virtual                                 ~IDisposable() noexcept = default;

    public:
        virtual void                            Dispose() = 0;
    };
}
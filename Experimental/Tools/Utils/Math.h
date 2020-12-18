
#include "BasicMath.hpp"

namespace DE
{

template <typename R, typename T>
inline R BitCast(const T& value)
{
    static_assert(sizeof(R) == sizeof(T), "types must have the same size");
    R res;
    std::memcpy(&res, &value, sizeof(res));
    return res;
}

} // namespace DE

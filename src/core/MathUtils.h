#pragma once

namespace align
{
template <typename T>
constexpr T Lerp(T a, T b, T t)
{
    return a + (b - a) * t;
}
} // namespace align


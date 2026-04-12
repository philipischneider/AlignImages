#pragma once

#include <array>

namespace align
{
struct Transform2D
{
    std::array<double, 9> matrix {
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0
    };
};
} // namespace align


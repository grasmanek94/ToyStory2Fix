#pragma once

#include <cstdint>

namespace HighResolutionLimits
{
    constexpr uint32_t OriginalLimit = 2048;
    // Experimental 8K ceiling; native surface and graphics-driver limits still apply.
    constexpr uint32_t RaisedLimit = 8192;
}

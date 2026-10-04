#pragma once

#include <algorithm>
#include <cmath>
#include "ObjectDrawDistance.h"

namespace SceneryDrawDistance
{
    struct Settings
    {
        float distantNear;
        float detailedFar;
        float detailedDistanceSquared;
    };

    inline bool Extend(const Settings& original, float projectionFar, float distance, Settings& result)
    {
        result = original;
        // Keep the native projection/depth precision and atmospheric fog. Only
        // move the detailed/distant scenery split, never beyond the hardware clip.
        if (!std::isfinite(projectionFar) || !std::isfinite(original.distantNear) ||
            !std::isfinite(original.detailedFar) || original.distantNear < 0.0f ||
            original.detailedFar <= original.distantNear || projectionFar < original.detailedFar ||
            std::isnan(original.detailedDistanceSquared) || original.detailedDistanceSquared < 0.0f)
            return false;
        distance = ObjectDrawDistance::SanitizeDistance(distance);
        result.detailedFar = (std::max)(original.detailedFar, (std::min)(distance, projectionFar));
        // Shift BOTH halves. Extending only the detailed pass would also draw
        // distant versions at their old split, causing overlapping LOD geometry.
        result.distantNear = original.distantNear + (result.detailedFar - original.detailedFar);
        result.detailedDistanceSquared = (std::max)(original.detailedDistanceSquared, distance * distance);
        return true;
    }

    inline int GridRadius(int original, int columns, int rows)
    {
        // The recovered loader creates 20x20 grids. Retain a small hard bound for
        // modified/unexpected layouts: native loops visit (2*radius)^2 cells.
        if (original < 0 || columns <= 0 || rows <= 0 || columns > 64 || rows > 64)
            return original;
        return (std::max)(original, (std::max)(columns, rows));
    }
}

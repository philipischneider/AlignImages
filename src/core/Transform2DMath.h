#pragma once

#include "core/Transform2D.h"

namespace align
{
// Composes two 2D homogeneous transforms: applies `inner` first, then `outer` (outer * inner).
Transform2D ComposeTransform2D(const Transform2D& outer, const Transform2D& inner);

Transform2D InvertTransform2D(const Transform2D& transform);

Transform2D MakeTranslation2D(double dx, double dy);

// Rotates by angleRadians around (pivotX, pivotY), leaving that point fixed.
Transform2D MakeRotationAround2D(double pivotX, double pivotY, double angleRadians);

// Uniformly scales by scaleFactor around (pivotX, pivotY), leaving that point fixed.
Transform2D MakeScaleAround2D(double pivotX, double pivotY, double scaleFactor);

// Combined rotation + uniform scale around (pivotX, pivotY) in one similarity transform (the two
// commute around a shared pivot, so this is equivalent to composing the two above, just built
// directly).
Transform2D MakeRotateScaleAround2D(double pivotX, double pivotY, double angleRadians, double scaleFactor);
} // namespace align

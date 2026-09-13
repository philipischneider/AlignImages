#include "core/Transform2DMath.h"

#include <opencv2/core.hpp>

#include <cmath>

namespace align
{
namespace
{
cv::Mat ToMat(const Transform2D& t)
{
    return (cv::Mat_<double>(3, 3) <<
        t.matrix[0], t.matrix[1], t.matrix[2],
        t.matrix[3], t.matrix[4], t.matrix[5],
        t.matrix[6], t.matrix[7], t.matrix[8]);
}

Transform2D FromMat(const cv::Mat& m)
{
    Transform2D t;
    for (int r = 0; r < 3; ++r)
    {
        for (int c = 0; c < 3; ++c)
        {
            t.matrix[static_cast<size_t>(r * 3 + c)] = m.at<double>(r, c);
        }
    }
    return t;
}
} // namespace

Transform2D ComposeTransform2D(const Transform2D& outer, const Transform2D& inner)
{
    return FromMat(ToMat(outer) * ToMat(inner));
}

Transform2D InvertTransform2D(const Transform2D& transform)
{
    return FromMat(ToMat(transform).inv());
}

Transform2D MakeTranslation2D(double dx, double dy)
{
    Transform2D t;
    t.matrix[2] = dx;
    t.matrix[5] = dy;
    return t;
}

Transform2D MakeRotationAround2D(double pivotX, double pivotY, double angleRadians)
{
    const double c = std::cos(angleRadians);
    const double s = std::sin(angleRadians);
    Transform2D t;
    t.matrix[0] = c;  t.matrix[1] = -s; t.matrix[2] = pivotX - c * pivotX + s * pivotY;
    t.matrix[3] = s;  t.matrix[4] = c;  t.matrix[5] = pivotY - s * pivotX - c * pivotY;
    t.matrix[6] = 0.0; t.matrix[7] = 0.0; t.matrix[8] = 1.0;
    return t;
}

Transform2D MakeScaleAround2D(double pivotX, double pivotY, double scaleFactor)
{
    Transform2D t;
    t.matrix[0] = scaleFactor; t.matrix[1] = 0.0;         t.matrix[2] = pivotX * (1.0 - scaleFactor);
    t.matrix[3] = 0.0;         t.matrix[4] = scaleFactor; t.matrix[5] = pivotY * (1.0 - scaleFactor);
    t.matrix[6] = 0.0;         t.matrix[7] = 0.0;         t.matrix[8] = 1.0;
    return t;
}

Transform2D MakeRotateScaleAround2D(double pivotX, double pivotY, double angleRadians, double scaleFactor)
{
    const double c = scaleFactor * std::cos(angleRadians);
    const double s = scaleFactor * std::sin(angleRadians);
    Transform2D t;
    t.matrix[0] = c;  t.matrix[1] = -s; t.matrix[2] = pivotX - c * pivotX + s * pivotY;
    t.matrix[3] = s;  t.matrix[4] = c;  t.matrix[5] = pivotY - s * pivotX - c * pivotY;
    t.matrix[6] = 0.0; t.matrix[7] = 0.0; t.matrix[8] = 1.0;
    return t;
}
} // namespace align

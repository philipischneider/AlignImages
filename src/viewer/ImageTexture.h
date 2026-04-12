#pragma once

#include "core/Result.h"

namespace cv
{
class Mat;
}

namespace align
{
class ImageTexture
{
public:
    ImageTexture() = default;
    ~ImageTexture();

    ImageTexture(const ImageTexture&) = delete;
    ImageTexture& operator=(const ImageTexture&) = delete;

    Result Upload(const cv::Mat& imageBgr);
    void Reset();

    unsigned int GetId() const { return m_textureId; }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    bool IsValid() const { return m_textureId != 0; }

private:
    unsigned int m_textureId = 0;
    int m_width = 0;
    int m_height = 0;
};
} // namespace align


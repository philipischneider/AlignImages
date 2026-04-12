#include "viewer/ImageTexture.h"

#include "core/OpenGLHeaders.h"

#include <opencv2/imgproc.hpp>

namespace align
{
ImageTexture::~ImageTexture()
{
    Reset();
}

Result ImageTexture::Upload(const cv::Mat& imageBgr)
{
    if (imageBgr.empty())
    {
        Reset();
        return Result{false, "Cannot upload an empty image."};
    }

    cv::Mat imageRgba;
    cv::cvtColor(imageBgr, imageRgba, cv::COLOR_BGR2RGBA);

    if (m_textureId == 0)
    {
        glGenTextures(1, &m_textureId);
    }

    glBindTexture(GL_TEXTURE_2D, m_textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, imageRgba.cols, imageRgba.rows, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 imageRgba.data);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_width = imageRgba.cols;
    m_height = imageRgba.rows;
    return Result{};
}

void ImageTexture::Reset()
{
    if (m_textureId != 0)
    {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }

    m_width = 0;
    m_height = 0;
}
} // namespace align

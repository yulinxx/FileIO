#include "FileIO/ImageUtils.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace Fio
{
    const float MM_PER_INCH = 25.4f;

    ImageInfo readImageInfo(const char* strUtf8Path)
    {
        ImageInfo info;
        if (!strUtf8Path || !strUtf8Path[0])
        {
            return info;
        }

        int width, height, channels;

        if (stbi_info(strUtf8Path, &width, &height, &channels))
        {
            info.width = width;
            info.height = height;
            info.dpiX = 96.0f;
            info.dpiY = 96.0f;
            info.isValid = true;
        }

        if (info.isValid && info.dpiX <= 0)
        {
            info.dpiX = 96.0f;
        }
        if (info.isValid && info.dpiY <= 0)
        {
            info.dpiY = 96.0f;
        }

        return info;
    }

    float pixelsToUnit(int pixelSize, float dpi, UnitType targetUnit)
    {
        if (dpi <= 0)
        {
            dpi = 96.0f;
        }

        float inches = static_cast<float>(pixelSize) / dpi;

        switch (targetUnit)
        {
        case UnitType::Millimeter:
            return inches * MM_PER_INCH;
        case UnitType::Inch:
            return inches;
        case UnitType::Pixel:
        default:
            return static_cast<float>(pixelSize);
        }
    }

    float inchToMm(float inch)
    {
        return inch * MM_PER_INCH;
    }

    float mmToInch(float mm)
    {
        return mm / MM_PER_INCH;
    }
}  // namespace Fio
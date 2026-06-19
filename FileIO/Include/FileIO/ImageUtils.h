#pragma once

#include <string>
#include "FileIOAPI.h"

namespace Fio
{
    enum class UnitType
    {
        Millimeter,
        Inch,
        Pixel
    };

    ////////////////////////////////////////////////////////////////

    struct ImageInfo
    {
        int width = 0;
        int height = 0;
        float dpiX = 96.0f;
        float dpiY = 96.0f;
        bool isValid = false;
    };

    FILEIO_API ImageInfo readImageInfo(const std::string& filePath);

    FILEIO_API float pixelsToUnit(int pixelSize, float dpi, UnitType targetUnit = UnitType::Millimeter);

    FILEIO_API float inchToMm(float inch);

    FILEIO_API float mmToInch(float mm);
} // namespace Fio
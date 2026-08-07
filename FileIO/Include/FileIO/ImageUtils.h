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

    /// ABI 说明：readImageInfo 参数使用 const std::string&，
    /// 仅限同编译器/同 CRT 体系内部使用。如需跨编译器应改用 const char*。
    FILEIO_API ImageInfo readImageInfo(const std::string& filePath);

    FILEIO_API float pixelsToUnit(int pixelSize, float dpi, UnitType targetUnit = UnitType::Millimeter);

    FILEIO_API float inchToMm(float inch);

    FILEIO_API float mmToInch(float mm);
} // namespace Fio
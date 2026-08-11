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

    /// ABI 说明：readImageInfo 接受 UTF-8 路径文本（调用方持有缓冲，只读同步使用），
    /// 不跨 DLL 传递 std::string。返回 ImageInfo 为 POD。
    FILEIO_API ImageInfo readImageInfo(const char* strUtf8Path);

    FILEIO_API float pixelsToUnit(int pixelSize, float dpi, UnitType targetUnit = UnitType::Millimeter);

    FILEIO_API float inchToMm(float inch);

    FILEIO_API float mmToInch(float mm);
} // namespace Fio
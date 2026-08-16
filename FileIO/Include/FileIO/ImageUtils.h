#pragma once

#include <string>
#include <vector>
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

    /// 解码图片文件为 RGBA8 像素数据（w*h*4 字节）。
    /// 支持 webp（经 libwebp）、tiff（经 libtiff）、以及其余常见格式
    /// （png/jpg/bmp/tga/gif 等，经 stb_image）。
    /// 若 QImage 无法解码（如缺少 Qt imageformats 插件）时，调用方可作为兜底。
    /// @param strUtf8Path UTF-8 路径文本
    /// @param outRgba 输出的 RGBA8 像素数据
    /// @param outW 输出宽度（像素）
    /// @param outH 输出高度（像素）
    /// @return 是否成功解码
    FILEIO_API bool loadImageToRgba(const char* strUtf8Path, std::vector<unsigned char>& outRgba, int& outW, int& outH);

    FILEIO_API float pixelsToUnit(int pixelSize, float dpi, UnitType targetUnit = UnitType::Millimeter);

    FILEIO_API float inchToMm(float inch);

    FILEIO_API float mmToInch(float mm);
}  // namespace Fio
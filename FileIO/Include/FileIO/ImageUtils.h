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
        /// 文件中真实存在的 DPI。缺少元数据时为 0，调用方应通过 dpiValid 区分
        /// “真实 96 DPI” 与 “没有 DPI、导入时按 96 兜底”。
        float dpiX = 0.0f;
        float dpiY = 0.0f;
        bool dpiValid = false;
        bool isValid = false;
    };

    /// ABI 说明：readImageInfo 接受 UTF-8 路径文本（调用方持有缓冲，只读同步使用），
    /// 不跨 DLL 传递 std::string。返回 ImageInfo 为 POD。
    ///
    /// 目前会解析 PNG pHYs 与 JPEG JFIF APP0 中的物理分辨率；其它格式或缺少
    /// 元数据的图片返回 dpiX/dpiY = 0、dpiValid = false。调用 pixelsToUnit 时
    /// 0 会按 96 DPI 兜底，但调用方应先检查 dpiValid 并明确告知用户。
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

    /// 从内存中编码的位图字节（png/jpg/bmp/tga/gif 等，经 stb_image）解码为 RGBA8 像素数据。
    /// 与 loadImageToRgba 的区别是本接口不落地临时文件，供 SVG 内嵌 base64 位图直接解码。
    /// encoded 必须指向 size 字节的有效编码数据。
    /// @return 是否成功解码（成功时 outW/outH 均 > 0）
    FILEIO_API bool loadImageToRgbaFromMemory(
        const unsigned char* encoded, size_t size, std::vector<unsigned char>& outRgba, int& outW, int& outH);

    FILEIO_API float pixelsToUnit(int pixelSize, float dpi, UnitType targetUnit = UnitType::Millimeter);

    FILEIO_API float inchToMm(float inch);

    FILEIO_API float mmToInch(float mm);
}  // namespace Fio
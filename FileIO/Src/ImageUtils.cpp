#include "FileIO/ImageUtils.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <webp/decode.h>
#include <tiffio.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>

namespace
{
    // Windows 下 fopen 不支持 UTF-8 路径，需要转换为宽字符路径。
    // 此辅助函数在所有平台上接受 UTF-8 路径并正确打开文件。
    FILE* fopenUtf8(const char* utf8Path, const char* mode)
    {
#ifdef _WIN32
        // Windows: 将 UTF-8 转换为宽字符后调用 _wfopen
        std::wstring wPath = std::filesystem::u8path(utf8Path).wstring();
        std::wstring wMode(mode, mode + std::strlen(mode));
        return _wfopen(wPath.c_str(), wMode.c_str());
#else
        // Unix/macOS: 直接使用 fopen（UTF-8 是原生编码）
        return std::fopen(utf8Path, mode);
#endif
    }

    /// 读取 4 字节大端无符号整数（PNG/JPEG 均使用大端）
    uint32_t readBe32(const unsigned char* bytes)
    {
        return (static_cast<uint32_t>(bytes[0]) << 24) |
               (static_cast<uint32_t>(bytes[1]) << 16) |
               (static_cast<uint32_t>(bytes[2]) << 8) |
               static_cast<uint32_t>(bytes[3]);
    }

    /// 读取 2 字节大端无符号整数
    uint16_t readBe16(const unsigned char* bytes)
    {
        return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) |
                                     static_cast<uint16_t>(bytes[1]));
    }

    /**
     * @brief 从 PNG 的 pHYs 块读取物理分辨率（像素/米 → DPI）
     *
     * pHYs 的 unit=1 表示像素/米；unit=0 只描述像素宽高比，无法换算 DPI。
     * 只扫描到 IEND 为止，遇到 IDAT 之前通常就能找到 pHYs。
     */
    bool readPngDpi(FILE* file, float& dpiX, float& dpiY)
    {
        static const unsigned char kSignature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        unsigned char signature[8];
        if (std::fread(signature, 1, 8, file) != 8 ||
            std::memcmp(signature, kSignature, 8) != 0)
        {
            return false;
        }

        for (;;)
        {
            unsigned char header[8];
            if (std::fread(header, 1, 8, file) != 8)
            {
                return false;
            }
            const uint32_t length = readBe32(header);
            const bool isPhys = std::memcmp(header + 4, "pHYs", 4) == 0;
            if (isPhys && length == 9)
            {
                unsigned char payload[9];
                if (std::fread(payload, 1, 9, file) != 9)
                {
                    return false;
                }
                const uint32_t ppmX = readBe32(payload);
                const uint32_t ppmY = readBe32(payload + 4);
                if (payload[8] != 1 || ppmX == 0 || ppmY == 0)
                {
                    return false;
                }
                // 1 米 = 39.3700787 英寸，像素/米 × 0.0254 = DPI
                dpiX = static_cast<float>(ppmX * 0.0254);
                dpiY = static_cast<float>(ppmY * 0.0254);
                return true;
            }
            if (std::memcmp(header + 4, "IEND", 4) == 0)
            {
                return false;
            }
            // 跳过块数据 + CRC；块长超过 int32 时按损坏文件处理
            if (length > 0x7FFFFFF0u ||
                std::fseek(file, static_cast<long>(length) + 4, SEEK_CUR) != 0)
            {
                return false;
            }
        }
    }

    /**
     * @brief 从 JPEG 的 APP0/JFIF 段读取物理分辨率
     *
     * units=1 表示 dots/inch，units=2 表示 dots/cm（×2.54 得到 DPI），
     * units=0 只描述宽高比，无法换算 DPI。
     */
    bool readJpegDpi(FILE* file, float& dpiX, float& dpiY)
    {
        unsigned char soi[2];
        if (std::fread(soi, 1, 2, file) != 2 || soi[0] != 0xFF || soi[1] != 0xD8)
        {
            return false;
        }

        for (;;)
        {
            unsigned char marker[2];
            if (std::fread(marker, 1, 2, file) != 2 || marker[0] != 0xFF)
            {
                return false;
            }
            const unsigned char code = marker[1];
            if (code == 0xD9 || code == 0xDA)
            {
                // 已到 EOI/SOS，仍未见到 JFIF
                return false;
            }
            if (code == 0x01 || code == 0xD8 || (code >= 0xD0 && code <= 0xD7))
            {
                continue;   // 无长度字段的标记
            }

            unsigned char lengthBytes[2];
            if (std::fread(lengthBytes, 1, 2, file) != 2)
            {
                return false;
            }
            const uint16_t segmentLength = readBe16(lengthBytes);
            if (segmentLength < 2)
            {
                return false;
            }

            // JFIF APP0 的最小载荷：标识 5 + 版本 2 + 单位 1 + 密度 4
            // + 缩略图宽高 2 = 14 字节，段长字段含自身 2 字节，故最小段长 16。
            // 旧代码用 >=18 会把最常见的无缩略图 JPEG 直接漏掉；小于 16 的
            // APP0 不可能是 JFIF，必须按普通段跳过，不能多读字节。
            if (code == 0xE0 && segmentLength >= 16)
            {
                unsigned char payload[14];
                const size_t payloadSize = sizeof(payload);
                if (std::fread(payload, 1, payloadSize, file) != payloadSize)
                {
                    return false;
                }
                if (std::memcmp(payload, "JFIF\0", 5) == 0)
                {
                    const unsigned char units = payload[7];
                    const uint16_t densityX = readBe16(payload + 8);
                    const uint16_t densityY = readBe16(payload + 10);
                    if (densityX == 0 || densityY == 0)
                    {
                        return false;
                    }
                    if (units == 1)
                    {
                        dpiX = static_cast<float>(densityX);
                        dpiY = static_cast<float>(densityY);
                        return true;
                    }
                    if (units == 2)
                    {
                        dpiX = static_cast<float>(densityX) * 2.54f;
                        dpiY = static_cast<float>(densityY) * 2.54f;
                        return true;
                    }
                    return false;   // units=0：只有宽高比
                }
                // 不是 JFIF，跳过剩余长度
                const long remain = static_cast<long>(segmentLength) - 2 -
                                    static_cast<long>(payloadSize);
                if (remain > 0 && std::fseek(file, remain, SEEK_CUR) != 0)
                {
                    return false;
                }
                continue;
            }

            if (std::fseek(file, static_cast<long>(segmentLength) - 2, SEEK_CUR) != 0)
            {
                return false;
            }
        }
    }

    /// 按文件头识别格式并读取真实 DPI；读不到返回 false（由调用方兜底）
    bool readPhysicalDpi(const char* utf8Path, float& dpiX, float& dpiY)
    {
        FILE* file = fopenUtf8(utf8Path, "rb");
        if (file == nullptr)
        {
            return false;
        }
        bool ok = readPngDpi(file, dpiX, dpiY);
        if (!ok)
        {
            // PNG 解析失败或无 pHYs 时重新定位到文件头再试 JPEG
            std::rewind(file);
            ok = readJpegDpi(file, dpiX, dpiY);
        }
        std::fclose(file);
        return ok;
    }
}  // namespace

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
            info.isValid = true;

            // 只有文件里真实存在 PNG pHYs / JPEG JFIF 密度时才标记 dpiValid。
            // 缺少元数据时保持 dpiX/dpiY = 0，由调用方显示“按 96 DPI 兜底”的提示；
            // 这里不再把 96 写进 ImageInfo，否则无法区分真实 96 DPI 与兜底值。
            const bool hasDpi = readPhysicalDpi(strUtf8Path, info.dpiX, info.dpiY)
                && info.dpiX > 0.0f && info.dpiY > 0.0f;
            if (hasDpi)
            {
                info.dpiValid = true;
            }
            else
            {
                info.dpiX = 0.0f;
                info.dpiY = 0.0f;
            }
        }

        return info;
    }

    namespace
    {
        /// 判断文件是否为 WebP（RIFF....WEBP）
        bool isWebPFile(const char* strUtf8Path)
        {
            FILE* f = fopenUtf8(strUtf8Path, "rb");
            if (!f)
            {
                return false;
            }
            unsigned char hdr[12];
            const size_t n = std::fread(hdr, 1, sizeof(hdr), f);
            std::fclose(f);
            if (n != sizeof(hdr))
            {
                return false;
            }
            return std::memcmp(hdr, "RIFF", 4) == 0 && std::memcmp(hdr + 8, "WEBP", 4) == 0;
        }

        bool decodeWebPToRgba(const char* strUtf8Path, std::vector<unsigned char>& outRgba, int& outW, int& outH)
        {
            FILE* f = fopenUtf8(strUtf8Path, "rb");
            if (!f)
            {
                return false;
            }
            std::fseek(f, 0, SEEK_END);
            const long sz = std::ftell(f);
            if (sz <= 0)
            {
                std::fclose(f);
                return false;
            }
            std::fseek(f, 0, SEEK_SET);
            std::vector<unsigned char> fileData(static_cast<size_t>(sz));
            if (std::fread(fileData.data(), 1, static_cast<size_t>(sz), f) != static_cast<size_t>(sz))
            {
                std::fclose(f);
                return false;
            }
            std::fclose(f);

            int w = 0, h = 0;
            uint8_t* px = WebPDecodeRGBA(fileData.data(), static_cast<size_t>(sz), &w, &h);
            if (!px || w <= 0 || h <= 0)
            {
                WebPFree(px);
                return false;
            }
            outRgba.assign(px, px + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
            WebPFree(px);
            outW = w;
            outH = h;
            return true;
        }

        bool decodeTiffToRgba(const char* strUtf8Path, std::vector<unsigned char>& outRgba, int& outW, int& outH)
        {
            TIFF* tif = TIFFOpen(strUtf8Path, "r");
            if (!tif)
            {
                return false;
            }

            uint32_t w = 0, h = 0;
            TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
            TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
            if (w == 0 || h == 0 || w > 65536u || h > 65536u)
            {
                TIFFClose(tif);
                return false;
            }

            const size_t npix = static_cast<size_t>(w) * static_cast<size_t>(h);
            std::vector<uint32_t> raster(npix);
            if (!TIFFReadRGBAImageOriented(tif, w, h, raster.data(), ORIENTATION_TOPLEFT, 0))
            {
                TIFFClose(tif);
                return false;
            }
            TIFFClose(tif);

            // TIFFReadRGBAImage 输出为 0xAARRGGBB（小端内存顺序 R,G,B,A），
            // 直接按 RGBA8 拷贝即可。
            outRgba.assign(reinterpret_cast<const unsigned char*>(raster.data()),
                reinterpret_cast<const unsigned char*>(raster.data()) + npix * 4);
            outW = static_cast<int>(w);
            outH = static_cast<int>(h);
            return true;
        }
    }  // namespace

    bool loadImageToRgba(const char* strUtf8Path, std::vector<unsigned char>& outRgba, int& outW, int& outH)
    {
        if (!strUtf8Path || !strUtf8Path[0])
        {
            return false;
        }

        // 1) WebP：libwebp 解码
        if (isWebPFile(strUtf8Path))
        {
            return decodeWebPToRgba(strUtf8Path, outRgba, outW, outH);
        }

        // 2) 其余格式：stb_image（png/jpg/bmp/tga/gif 等）
        int w = 0, h = 0, comp = 0;
        stbi_uc* px = stbi_load(strUtf8Path, &w, &h, &comp, 4);
        if (px && w > 0 && h > 0)
        {
            outRgba.assign(px, px + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
            stbi_image_free(px);
            outW = w;
            outH = h;
            return true;
        }
        stbi_image_free(px);

        // 3) TIFF：libtiff 解码
        return decodeTiffToRgba(strUtf8Path, outRgba, outW, outH);
    }

    bool loadImageToRgbaFromMemory(
        const unsigned char* encoded, size_t size, std::vector<unsigned char>& outRgba, int& outW, int& outH)
    {
        if (!encoded || size == 0)
        {
            return false;
        }
        if (size > static_cast<size_t>(std::numeric_limits<int>::max()))
        {
            return false;
        }

        // stb_image 的 stbi_load_from_memory 覆盖 png/jpg/bmp/tga/gif 等常见 SVG 内嵌格式
        int w = 0, h = 0, comp = 0;
        stbi_uc* px = stbi_load_from_memory(encoded, static_cast<int>(size), &w, &h, &comp, 4);
        if (px && w > 0 && h > 0)
        {
            outRgba.assign(px, px + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
            stbi_image_free(px);
            outW = w;
            outH = h;
            return true;
        }
        stbi_image_free(px);
        return false;
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
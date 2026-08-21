#include "FileIO/ImageUtils.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <webp/decode.h>
#include <tiffio.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace
{
    // [F8-P1 修复] Windows 下 fopen 不支持 UTF-8 路径，需要转换为宽字符路径。
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
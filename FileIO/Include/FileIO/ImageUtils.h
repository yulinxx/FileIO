#pragma once

#include <string>
#include "FileIOAPI.h"

#ifdef _WIN32
#include <windows.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#endif

namespace Fio {

enum class UnitType {
    Millimeter,
    Inch,
    Pixel
};

struct ImageInfo {
    int width = 0;
    int height = 0;
    float dpiX = 96.0f;
    float dpiY = 96.0f;
    bool isValid = false;
};

class GdiplusManager {
public:
    static GdiplusManager& instance();
    bool isInitialized() const { return m_initialized; }
    
private:
    GdiplusManager();
    ~GdiplusManager();
    GdiplusManager(const GdiplusManager&) = delete;
    GdiplusManager& operator=(const GdiplusManager&) = delete;
    
    bool m_initialized;
    ULONG_PTR m_gdiplusToken;
};

FILEIO_API ImageInfo readImageInfo(const std::string& filePath);

FILEIO_API float pixelsToUnit(int pixelSize, float dpi, UnitType targetUnit = UnitType::Millimeter);

FILEIO_API float inchToMm(float inch);

FILEIO_API float mmToInch(float mm);

} // namespace Fio

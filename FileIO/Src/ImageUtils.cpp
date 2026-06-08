#include "FileIO/ImageUtils.h"

#ifdef _WIN32
#include <windows.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#endif

namespace Fio {

const float MM_PER_INCH = 25.4f;

#ifdef _WIN32
GdiplusManager::GdiplusManager() : m_initialized(false), m_gdiplusToken(0) {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::Status status = Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr);
    m_initialized = (status == Gdiplus::Ok);
}

GdiplusManager::~GdiplusManager() {
    if (m_initialized) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
    }
}

GdiplusManager& GdiplusManager::instance() {
    static GdiplusManager instance;
    return instance;
}
#endif

ImageInfo readImageInfo(const std::string& filePath) {
    ImageInfo info;
    
#ifdef _WIN32
    auto& gdiplusMgr = GdiplusManager::instance();
    if (!gdiplusMgr.isInitialized()) {
        return info;
    }
    
    int wcharCount = MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, nullptr, 0);
    if (wcharCount <= 0) {
        return info;
    }
    
    std::wstring widePath(wcharCount, 0);
    MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, &widePath[0], wcharCount);
    
    {
        Gdiplus::Bitmap bitmap(widePath.c_str());
        if (bitmap.GetLastStatus() == Gdiplus::Ok) {
            info.width = bitmap.GetWidth();
            info.height = bitmap.GetHeight();
            info.dpiX = bitmap.GetHorizontalResolution();
            info.dpiY = bitmap.GetVerticalResolution();
            info.isValid = true;
        }
    }
#else
    info.isValid = false;
#endif
    
    if (info.isValid && info.dpiX <= 0) info.dpiX = 96.0f;
    if (info.isValid && info.dpiY <= 0) info.dpiY = 96.0f;
    
    return info;
}

float pixelsToUnit(int pixelSize, float dpi, UnitType targetUnit) {
    if (dpi <= 0) dpi = 96.0f;
    
    float inches = static_cast<float>(pixelSize) / dpi;
    
    switch (targetUnit) {
        case UnitType::Millimeter:
            return inches * MM_PER_INCH;
        case UnitType::Inch:
            return inches;
        case UnitType::Pixel:
        default:
            return static_cast<float>(pixelSize);
    }
}

float inchToMm(float inch) {
    return inch * MM_PER_INCH;
}

float mmToInch(float mm) {
    return mm / MM_PER_INCH;
}

} // namespace Fio

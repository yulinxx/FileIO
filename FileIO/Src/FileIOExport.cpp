#include "FileIO/FileIOExport.h"
#include "FileIO/FileIOManager.h"
#include "FileIO/FileIOError.h"
#include "FileIO/FileFormat.h"
#include "FileIO/FileParserFactory.h"
#include "FileIO/FileWriterFactory.h"
#include "Engine2D/SyEntity/SyEntity.h"

#include <cstring>
#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>
#include <new>

// 跨平台 strdup（标准 C++ 中没有此函数，手动实现）
static char* fio_strdup(const char* s)
{
    if (!s) return nullptr;
    size_t len = std::strlen(s);
    char* d = static_cast<char*>(std::malloc(len + 1));
    if (!d) return nullptr;
    std::memcpy(d, s, len + 1);
    return d;
}

// ============================================================================
// 内部辅助
// ============================================================================

// FioManager 实现: 持有 C++ FileIOManager + 最近导入的实体 + 回调转发
struct FioManagerImpl
{
    Fio::FileIOManager  manager;
    Fio::VecSyEntityPtr entities;

    FioImportCallbackFn import_cb_fn = nullptr;
    void* import_cb_ctx = nullptr;
    FioExportCallbackFn export_cb_fn = nullptr;
    void* export_cb_ctx = nullptr;

    FioManagerImpl()
    {
        manager.setImportCallback([this](const std::string& path, bool started) {
            if (import_cb_fn)
                import_cb_fn(path.c_str(), started ? 1 : 0, import_cb_ctx);
            });
        manager.setExportCallback([this](const std::string& path, bool started) {
            if (export_cb_fn)
                export_cb_fn(path.c_str(), started ? 1 : 0, export_cb_ctx);
            });
    }
};

// ---- Format 双向映射 ----
static FioFileFormat toCFmt(Fio::FileFormat fmt)
{
    switch (fmt)
    {
        case Fio::FileFormat::Unknown:  return FIO_FORMAT_UNKNOWN;
        case Fio::FileFormat::DXF:      return FIO_FORMAT_DXF;
        case Fio::FileFormat::PLT:      return FIO_FORMAT_PLT;
        case Fio::FileFormat::SVG:      return FIO_FORMAT_SVG;
        case Fio::FileFormat::UG:       return FIO_FORMAT_UG;
        case Fio::FileFormat::STEP:     return FIO_FORMAT_STEP;
        case Fio::FileFormat::PDF:      return FIO_FORMAT_PDF;
        case Fio::FileFormat::AI:       return FIO_FORMAT_AI;
        case Fio::FileFormat::Native:   return FIO_FORMAT_NATIVE;
        case Fio::FileFormat::Native3D: return FIO_FORMAT_NATIVE3D;
        case Fio::FileFormat::BMP:      return FIO_FORMAT_BMP;
        case Fio::FileFormat::PNG:      return FIO_FORMAT_PNG;
    }
    return FIO_FORMAT_UNKNOWN;
}

static Fio::FileFormat fromCFmt(FioFileFormat fmt)
{
    switch (fmt)
    {
        case FIO_FORMAT_UNKNOWN:  return Fio::FileFormat::Unknown;
        case FIO_FORMAT_DXF:      return Fio::FileFormat::DXF;
        case FIO_FORMAT_PLT:      return Fio::FileFormat::PLT;
        case FIO_FORMAT_SVG:      return Fio::FileFormat::SVG;
        case FIO_FORMAT_UG:       return Fio::FileFormat::UG;
        case FIO_FORMAT_STEP:     return Fio::FileFormat::STEP;
        case FIO_FORMAT_PDF:      return Fio::FileFormat::PDF;
        case FIO_FORMAT_AI:       return Fio::FileFormat::AI;
        case FIO_FORMAT_NATIVE:   return Fio::FileFormat::Native;
        case FIO_FORMAT_NATIVE3D: return Fio::FileFormat::Native3D;
        case FIO_FORMAT_BMP:      return Fio::FileFormat::BMP;
        case FIO_FORMAT_PNG:      return Fio::FileFormat::PNG;
    }
    return Fio::FileFormat::Unknown;
}

static FioResult makeResult(bool ok, const std::string& msg)
{
    FioResult r;
    r.success = ok ? 1 : 0;
    size_t n = msg.size();
    if (n >= sizeof(r.error_message))
        n = sizeof(r.error_message) - 1;
    std::memcpy(r.error_message, msg.data(), n);
    r.error_message[n] = '\0';
    return r;
}

// ============================================================================
// 管理器生命周期
// ============================================================================

_FILEIO_C_API FioManager* fio_manager_create(void)
{
    void* mem = std::malloc(sizeof(FioManagerImpl));
    if (!mem) return nullptr;
    return new (mem) FioManagerImpl();
}

_FILEIO_C_API void fio_manager_destroy(FioManager* mgr)
{
    if (!mgr) return;
    mgr->~FioManagerImpl();
    std::free(mgr);
}

// ============================================================================
// 文件操作
// ============================================================================

_FILEIO_C_API FioResult fio_manager_import(FioManager* mgr, const char* file_path)
{
    if (!mgr || !file_path)
        return makeResult(false, "null argument");
    std::string path(file_path);
    auto result = mgr->manager.importFile(path, mgr->entities);
    return makeResult(result.success, result.errorMessage);
}

_FILEIO_C_API FioResult fio_manager_import_with_format(
    FioManager* mgr, const char* file_path, FioFileFormat format)
{
    if (!mgr || !file_path)
        return makeResult(false, "null argument");
    auto cppFmt = fromCFmt(format);
    if (cppFmt == Fio::FileFormat::Unknown)
        return makeResult(false, "unknown format");
    auto result = mgr->manager.importFile(std::string(file_path), cppFmt, mgr->entities);
    return makeResult(result.success, result.errorMessage);
}

_FILEIO_C_API FioResult fio_manager_export(
    FioManager* mgr, const char* file_path, FioFileFormat format)
{
    if (!mgr || !file_path)
        return makeResult(false, "null argument");
    std::string path(file_path);
    Fio::WriteResult result;
    if (format == FIO_FORMAT_UNKNOWN)
    {
        result = mgr->manager.exportFile(path, mgr->entities);
    }
    else
    {
        auto cppFmt = fromCFmt(format);
        if (cppFmt == Fio::FileFormat::Unknown)
            return makeResult(false, "unknown format");
        result = mgr->manager.exportFile(path, cppFmt, mgr->entities);
    }
    return makeResult(result.success, result.errorMessage);
}

_FILEIO_C_API FioResult fio_manager_convert(
    FioManager* mgr, const char* src_path, const char* dst_path,
    FioFileFormat dst_format)
{
    if (!mgr || !src_path || !dst_path)
        return makeResult(false, "null argument");
    // 清空之前导入的实体
    mgr->entities.clear();
    // 导入
    Fio::ParseResult parseResult;
    if (dst_format != FIO_FORMAT_UNKNOWN)
    {
        auto fmt = fromCFmt(dst_format);
        if (fmt == Fio::FileFormat::Unknown)
            return makeResult(false, "unknown destination format");
        // 先检测源文件格式
        auto srcFmt = mgr->manager.detectFormat(std::string(src_path));
        if (srcFmt == Fio::FileFormat::Unknown)
            return makeResult(false, "cannot detect source file format");
        parseResult = mgr->manager.importFile(std::string(src_path), srcFmt, mgr->entities);
    }
    else
    {
        parseResult = mgr->manager.importFile(std::string(src_path), mgr->entities);
    }
    if (!parseResult.success)
        return makeResult(false, parseResult.errorMessage);
    // 导出
    auto writeResult = (dst_format == FIO_FORMAT_UNKNOWN)
        ? mgr->manager.exportFile(std::string(dst_path), mgr->entities)
        : mgr->manager.exportFile(std::string(dst_path), fromCFmt(dst_format), mgr->entities);
    return makeResult(writeResult.success, writeResult.errorMessage);
}

// ============================================================================
// 格式检测与查询
// ============================================================================

_FILEIO_C_API FioFileFormat fio_manager_detect_format(
    FioManager* mgr, const char* file_path)
{
    if (!mgr || !file_path) return FIO_FORMAT_UNKNOWN;
    return toCFmt(mgr->manager.detectFormat(std::string(file_path)));
}

_FILEIO_C_API int fio_manager_can_import(FioManager* mgr, const char* file_path)
{
    if (!mgr || !file_path) return 0;
    return mgr->manager.canImport(std::string(file_path)) ? 1 : 0;
}

_FILEIO_C_API int fio_manager_can_export(FioManager* mgr, FioFileFormat format)
{
    if (!mgr) return 0;
    return mgr->manager.canExport(fromCFmt(format)) ? 1 : 0;
}

_FILEIO_C_API char** fio_manager_supported_import_extensions(
    FioManager* mgr, int* out_count)
{
    if (!mgr || !out_count)
    {
        if (out_count) *out_count = 0;
        return nullptr;
    }
    auto exts = mgr->manager.supportedImportExtensions();
    *out_count = static_cast<int>(exts.size());
    char** arr = static_cast<char**>(std::malloc(exts.size() * sizeof(char*)));
    if (!arr)
    {
        *out_count = 0;
        return nullptr;
    }
    for (size_t i = 0; i < exts.size(); ++i)
    {
        arr[i] = fio_strdup(exts[i].c_str());
        if (!arr[i])
        {
            // 内存不足，清理已分配的部分
            for (size_t j = 0; j < i; ++j)
                std::free(arr[j]);
            std::free(arr);
            *out_count = 0;
            return nullptr;
        }
    }
    return arr;
}

_FILEIO_C_API char** fio_manager_supported_export_extensions(
    FioManager* mgr, int* out_count)
{
    if (!mgr || !out_count)
    {
        if (out_count) *out_count = 0;
        return nullptr;
    }
    auto exts = mgr->manager.supportedExportExtensions();
    *out_count = static_cast<int>(exts.size());
    char** arr = static_cast<char**>(std::malloc(exts.size() * sizeof(char*)));
    if (!arr)
    {
        *out_count = 0;
        return nullptr;
    }
    for (size_t i = 0; i < exts.size(); ++i)
    {
        arr[i] = fio_strdup(exts[i].c_str());
        if (!arr[i])
        {
            for (size_t j = 0; j < i; ++j)
                std::free(arr[j]);
            std::free(arr);
            *out_count = 0;
            return nullptr;
        }
    }
    return arr;
}

_FILEIO_C_API void fio_free_string_array(char** arr, int count)
{
    if (!arr) return;
    for (int i = 0; i < count; ++i)
    {
        if (arr[i]) std::free(arr[i]);
    }
    std::free(arr);
}

// ============================================================================
// 回调设置
// ============================================================================

_FILEIO_C_API void fio_manager_set_import_callback(
    FioManager* mgr, FioImportCallbackFn callback, void* user_data)
{
    if (!mgr) return;
    mgr->import_cb_fn = callback;
    mgr->import_cb_ctx = user_data;
    // FileIOManager 的 lambda 已经通过构造函数捕获了 mgr 指针
}

_FILEIO_C_API void fio_manager_set_export_callback(
    FioManager* mgr, FioExportCallbackFn callback, void* user_data)
{
    if (!mgr) return;
    mgr->export_cb_fn = callback;
    mgr->export_cb_ctx = user_data;
}

// ============================================================================
// 实体查询
// ============================================================================

_FILEIO_C_API int fio_entity_count(FioManager* mgr)
{
    if (!mgr) return 0;
    return static_cast<int>(mgr->entities.size());
}

// ============================================================================
// 工具函数
// ============================================================================

_FILEIO_C_API void fio_version(int* out_major, int* out_minor, int* out_patch)
{
    if (out_major) *out_major = 1;
    if (out_minor) *out_minor = 0;
    if (out_patch) *out_patch = 0;
}

_FILEIO_C_API const char* fio_format_string(FioFileFormat format)
{
    switch (format)
    {
        case FIO_FORMAT_DXF:      return "AutoCAD DXF";
        case FIO_FORMAT_PLT:      return "HPGL PLT";
        case FIO_FORMAT_SVG:      return "Scalable Vector Graphics";
        case FIO_FORMAT_UG:       return "Siemens NX/UG";
        case FIO_FORMAT_STEP:     return "ISO 10303 STEP";
        case FIO_FORMAT_PDF:      return "PDF Document";
        case FIO_FORMAT_AI:       return "Adobe Illustrator";
        case FIO_FORMAT_NATIVE:   return "SanYi Native (2D)";
        case FIO_FORMAT_NATIVE3D: return "SanYi Native (3D)";
        case FIO_FORMAT_BMP:      return "Windows Bitmap";
        case FIO_FORMAT_PNG:      return "Portable Network Graphics";
        default:                  return "Unknown Format";
    }
}

_FILEIO_C_API const char* fio_format_extension(FioFileFormat format)
{
    switch (format)
    {
        case FIO_FORMAT_DXF:      return "dxf";
        case FIO_FORMAT_PLT:      return "plt";
        case FIO_FORMAT_SVG:      return "svg";
        case FIO_FORMAT_UG:       return "igs";
        case FIO_FORMAT_STEP:     return "stp";
        case FIO_FORMAT_PDF:      return "pdf";
        case FIO_FORMAT_AI:       return "ai";
        case FIO_FORMAT_NATIVE:   return "sy";
        case FIO_FORMAT_NATIVE3D: return "syx";
        case FIO_FORMAT_BMP:      return "bmp";
        case FIO_FORMAT_PNG:      return "png";
        default:                  return "";
    }
}

_FILEIO_C_API FioFileFormat fio_format_from_extension(const char* ext)
{
    if (!ext || !*ext) return FIO_FORMAT_UNKNOWN;
    // 跳过前导点号
    const char* p = ext;
    if (*p == '.') ++p;
    // 统一转小写比较
    char buf[32];
    size_t i = 0;
    while (*p && i < sizeof(buf) - 1)
    {
        buf[i++] = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
        ++p;
    }
    buf[i] = '\0';
    if (std::strcmp(buf, "dxf") == 0)  return FIO_FORMAT_DXF;
    if (std::strcmp(buf, "plt") == 0)  return FIO_FORMAT_PLT;
    if (std::strcmp(buf, "hpgl") == 0) return FIO_FORMAT_PLT;
    if (std::strcmp(buf, "svg") == 0)  return FIO_FORMAT_SVG;
    if (std::strcmp(buf, "svgz") == 0) return FIO_FORMAT_SVG;
    if (std::strcmp(buf, "prt") == 0)  return FIO_FORMAT_UG;
    if (std::strcmp(buf, "igs") == 0)  return FIO_FORMAT_UG;
    if (std::strcmp(buf, "iges") == 0) return FIO_FORMAT_UG;
    if (std::strcmp(buf, "stp") == 0)  return FIO_FORMAT_STEP;
    if (std::strcmp(buf, "step") == 0) return FIO_FORMAT_STEP;
    if (std::strcmp(buf, "pdf") == 0)  return FIO_FORMAT_PDF;
    if (std::strcmp(buf, "ai") == 0)   return FIO_FORMAT_AI;
    if (std::strcmp(buf, "sy") == 0)   return FIO_FORMAT_NATIVE;
    if (std::strcmp(buf, "syx") == 0)  return FIO_FORMAT_NATIVE3D;
    if (std::strcmp(buf, "bmp") == 0)  return FIO_FORMAT_BMP;
    if (std::strcmp(buf, "png") == 0)  return FIO_FORMAT_PNG;
    return FIO_FORMAT_UNKNOWN;
}
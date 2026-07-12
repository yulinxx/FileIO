#ifndef FILEIO_EXPORT_H
#define FILEIO_EXPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

    // ============================================================================
    // FileIOExport.h — FileIO DLL C 语言接口
    //
    // 用途:
    //   提供与编译器无关的 C 语言 ABI，使 DLL 可以在不同 IDE/编译器之间使用。
    //   （例如：MSVC 构建的 DLL 在 MinGW、Clang、C# P/Invoke 等场景中调用）
    //
    // 使用方式:
    //   1. 隐式链接: 链接 FileIO.lib + #include "FileIOExport.h"
    //   2. 显式链接: LoadLibrary("FileIO.dll") + GetProcAddress
    //
    // 注意:
    //   - 本 API 不暴露 C++ 类型 (std::string, std::vector 等)
    //   - 所有字符串参数为 UTF-8 编码的 null-terminated C 字符串
    //   - 实体数据通过 FileIOManager 句柄进行隐式传递
    // ============================================================================

    // ---- DLL 导出宏 ----
    // 构建 DLL 时需定义 FILEIO_EXPORTS，由 CMake 自动设置
#if defined(_WIN32) || defined(_WIN64)
#  ifdef FILEIO_EXPORTS
#    define _FILEIO_C_API __declspec(dllexport)
#  else
#    define _FILEIO_C_API __declspec(dllimport)
#  endif
#else
#  ifdef FILEIO_EXPORTS
#    define _FILEIO_C_API __attribute__((visibility("default")))
#  else
#    define _FILEIO_C_API
#  endif
#endif

// ---- 文件格式枚举 (对应 Fio::FileFormat) ----
    typedef enum FioFileFormat
    {
        FIO_FORMAT_UNKNOWN = 0,
        FIO_FORMAT_DXF = 1,
        FIO_FORMAT_PLT = 2,
        FIO_FORMAT_SVG = 3,
        FIO_FORMAT_UG = 4,
        FIO_FORMAT_STEP = 5,
        FIO_FORMAT_PDF = 6,
        FIO_FORMAT_AI = 7,
        FIO_FORMAT_NATIVE = 8,
        FIO_FORMAT_NATIVE3D = 9,
        FIO_FORMAT_BMP = 10,
        FIO_FORMAT_PNG = 11
    } FioFileFormat;

    // ---- 通用操作结果 ----
    typedef struct FioResult
    {
        int   success;           // 0 = 失败, 非 0 = 成功
        char  error_message[512]; // 错误描述 (成功时为空字符串)
    } FioResult;

    // ---- 回调函数类型 ----
    // user_data 由 set_import/export_callback 传入
    typedef void (*FioImportCallbackFn)(const char* file_path, int started, void* user_data);
    typedef void (*FioExportCallbackFn)(const char* file_path, int started, void* user_data);

    // ---- 不透明句柄 ----
    typedef struct FioManagerImpl FioManager;

    // ============================================================================
    // 管理器生命周期
    // ============================================================================

    // 创建 FileIOManager 实例，返回不透明句柄
    _FILEIO_C_API FioManager* fio_manager_create(void);

    // 销毁句柄，释放所有资源
    _FILEIO_C_API void fio_manager_destroy(FioManager* mgr);

    // ============================================================================
    // 文件操作
    // ============================================================================

    // 导入文件: 将文件解析为内部实体数据
    // 成功后可调用 fio_manager_export 将实体写出为其他格式
    _FILEIO_C_API FioResult fio_manager_import(FioManager* mgr, const char* file_path);

    // 导入文件 (指定格式): 跳过自动格式检测
    _FILEIO_C_API FioResult fio_manager_import_with_format(
        FioManager* mgr, const char* file_path, FioFileFormat format);

    // 导出文件: 将最近导入的实体数据写出到指定路径
    // format 为导出格式; 若为 FIO_FORMAT_UNKNOWN 则根据 file_path 扩展名自动判断
    _FILEIO_C_API FioResult fio_manager_export(
        FioManager* mgr, const char* file_path, FioFileFormat format);

    // 文件格式转换: import + export 一步完成
    _FILEIO_C_API FioResult fio_manager_convert(
        FioManager* mgr, const char* src_path, const char* dst_path,
        FioFileFormat dst_format);

    // ============================================================================
    // 格式检测与查询
    // ============================================================================

    // 检测文件格式 (基于扩展名)
    _FILEIO_C_API FioFileFormat fio_manager_detect_format(
        FioManager* mgr, const char* file_path);

    // 检查是否支持导入该文件
    _FILEIO_C_API int fio_manager_can_import(FioManager* mgr, const char* file_path);

    // 检查是否支持导出为该格式
    _FILEIO_C_API int fio_manager_can_export(FioManager* mgr, FioFileFormat format);

    // 获取支持的导入扩展名列表 (返回数组需调用 fio_free_string_array 释放)
    _FILEIO_C_API char** fio_manager_supported_import_extensions(
        FioManager* mgr, int* out_count);

    // 获取支持的导出扩展名列表 (返回数组需调用 fio_free_string_array 释放)
    _FILEIO_C_API char** fio_manager_supported_export_extensions(
        FioManager* mgr, int* out_count);

    // 获取当前已导入的实体数量
    _FILEIO_C_API int fio_entity_count(FioManager* mgr);

    // 释放由 fio_manager_supported_*_extensions 返回的字符串数组
    _FILEIO_C_API void fio_free_string_array(char** arr, int count);

    // ============================================================================
    // 回调设置
    // ============================================================================

    // 设置导入进度回调 (started=1 表示开始, 0 表示完成)
    _FILEIO_C_API void fio_manager_set_import_callback(
        FioManager* mgr, FioImportCallbackFn callback, void* user_data);

    // 设置导出进度回调 (started=1 表示开始, 0 表示完成)
    _FILEIO_C_API void fio_manager_set_export_callback(
        FioManager* mgr, FioExportCallbackFn callback, void* user_data);

    // ============================================================================
    // 工具函数
    // ============================================================================

    // 获取 FileIO DLL 版本号
    _FILEIO_C_API void fio_version(int* out_major, int* out_minor, int* out_patch);

    // 将格式枚举转为可读字符串 (返回的字符串为静态内存，无需释放)
    _FILEIO_C_API const char* fio_format_string(FioFileFormat format);

    // 将格式枚举转为标准扩展名 (如 FIO_FORMAT_DXF → "dxf")
    _FILEIO_C_API const char* fio_format_extension(FioFileFormat format);

    // 根据扩展名获取格式枚举 (如 "dxf" → FIO_FORMAT_DXF, ".DXF" 也能处理)
    _FILEIO_C_API FioFileFormat fio_format_from_extension(const char* ext);

#ifdef __cplusplus
}
#endif

#endif // FILEIO_EXPORT_H

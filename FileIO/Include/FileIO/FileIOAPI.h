#pragma once

// ============================================================================
// FileIOAPI.h — DLL 导入/导出宏定义
//
// 跨平台 / 跨编译器 DLL 导出约定:
//   - FILEIO_API:   用于 C++ 类/函数导出 (__declspec / visibility)
//   - FILEIO_C_API: 用于 extern "C" 函数导出 (C 调用约定 + 标准导出)
//
// 构建 DLL 时需定义 FILEIO_EXPORTS，本例已在 CMakeLists.txt 中通过
// target_compile_definitions(${LIB_NAME} PRIVATE ${LIB_EXPORT_MACRO}) 自动定义。
//
// Windows (MSVC / Clang-CL / MinGW):
//   - __declspec(dllexport) 导出, __declspec(dllimport) 导入
//   - 使用 __stdcall 作为 C API 调用约定（标准 Win32 约定）
//
// Linux / macOS (GCC / Clang):
//   - 使用 __attribute__((visibility("default"))) 控制可见性
//   - 在构建时通过 -fvisibility=hidden 隐藏非导出符号
// ============================================================================

// ---- 编译器/平台检测 ----
#if defined(_WIN32) || defined(_WIN64)
    #define FILEIO_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
    #define FILEIO_PLATFORM_APPLE 1
#elif defined(__linux__) || defined(__unix__)
    #define FILEIO_PLATFORM_LINUX 1
#else
    #define FILEIO_PLATFORM_UNKNOWN 1
#endif

// ---- C++ 类/函数导出宏 (FILEIO_API) ----
#if defined(FILEIO_PLATFORM_WINDOWS)
    #ifdef FILEIO_EXPORTS
        #define FILEIO_API __declspec(dllexport)
    #else
        #define FILEIO_API __declspec(dllimport)
    #endif
#elif defined(FILEIO_PLATFORM_APPLE) || defined(FILEIO_PLATFORM_LINUX)
    #ifdef FILEIO_EXPORTS
        #define FILEIO_API __attribute__((visibility("default")))
    #else
        #define FILEIO_API
    #endif
#else
    #define FILEIO_API
#endif

// ---- C 函数导出宏 (FILEIO_C_API) ----
// 专为 extern "C" 函数设计，在所有平台上均导出，且 Windows 上使用 __stdcall。
// 调用者需用 FARPROC / GetProcAddress 或隐式链接导入。
#if defined(FILEIO_PLATFORM_WINDOWS)
    #ifdef FILEIO_EXPORTS
        #define FILEIO_C_API extern "C" __declspec(dllexport) __stdcall
    #else
        #define FILEIO_C_API extern "C" __declspec(dllimport) __stdcall
    #endif
#else
    #ifdef FILEIO_EXPORTS
        #define FILEIO_C_API extern "C" __attribute__((visibility("default")))
    #else
        #define FILEIO_C_API extern "C"
    #endif
#endif

// ---- 辅助宏 ----
// 在需要显式控制调用约定的 Windows 平台上声明 C 函数指针类型
#if defined(FILEIO_PLATFORM_WINDOWS)
    #define FILEIO_CDECL __cdecl
#else
    #define FILEIO_CDECL
#endif

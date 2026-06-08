#pragma once

#if defined(_WIN32) || defined(_WIN64)
#ifdef FILEIO_EXPORTS
#define FILEIO_API __declspec(dllexport)
#else
#define FILEIO_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#ifdef FILEIO_EXPORTS
#define FILEIO_API __attribute__((visibility("default")))
#else
#define FILEIO_API
#endif
#elif defined(__APPLE__)
#ifdef FILEIO_EXPORTS
#define FILEIO_API __attribute__((visibility("default")))
#else
#define FILEIO_API
#endif
#else
#define FILEIO_API
#endif
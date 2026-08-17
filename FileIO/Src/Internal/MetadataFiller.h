#pragma once

// MetadataFiller —— 元数据填充工具（FileIO 内部共享）
//
// 职责:
//   1. 统一填充 SyDocument 的元数据（版本、时间、环境信息等）
//   2. 消除 NativeWriter / NativeWriter3D 之间的重复代码
//   3. 提供可扩展的元数据注入点
//
// 设计原则:
//   - 纯函数式接口，无状态
//   - 通过 SyDocument 引用直接操作，不引入额外抽象层
//   - 平台相关代码集中在此文件，便于维护

#include "FileIO/SyDocument.h"
#include "FileIO/SySerializer.h"

#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

// 平台检测
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <Lmcons.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <unistd.h>
#include <pwd.h>
#endif

namespace Fio
{
    namespace MetadataFiller
    {
        /// 获取当前时间的 ISO 8601 字符串 (本地时间)
        inline std::string currentIsoTime()
        {
            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            oss << std::put_time(std::localtime(&time), "%Y-%m-%dT%H:%M:%S");
            return oss.str();
        }

        /// 获取操作系统名称 (如 "Windows", "macOS", "Linux")
        inline const char* osName()
        {
#ifdef _WIN32
            return "Windows";
#elif defined(__APPLE__)
            return "macOS";
#elif defined(__linux__)
            return "Linux";
#else
            return "Unknown";
#endif
        }

        /// 获取操作系统版本详情 (如 "Windows 11 23H2", "macOS 14.5", "Ubuntu 22.04")
        inline std::string osVersionDetail()
        {
#ifdef _WIN32
            // Windows: 使用 RtlGetVersion 获取精确版本
            using RtlGetVersionPtr = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
            HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
            if (hNtdll)
            {
                auto rtlGetVersion = reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(hNtdll, "RtlGetVersion"));
                if (rtlGetVersion)
                {
                    RTL_OSVERSIONINFOW osvi = {};
                    osvi.dwOSVersionInfoSize = sizeof(osvi);
                    if (rtlGetVersion(&osvi) == 0)
                    {
                        std::ostringstream oss;
                        oss << "Windows " << osvi.dwMajorVersion << "." << osvi.dwMinorVersion
                            << " (Build " << osvi.dwBuildNumber << ")";
                        return oss.str();
                    }
                }
            }
            return "Windows (unknown version)";
#elif defined(__APPLE__)
            // macOS: 通过 /usr/bin/sw_vers 获取
            char buf[256] = {};
            FILE* pipe = popen("/usr/bin/sw_vers -productVersion 2>/dev/null", "r");
            if (pipe)
            {
                fgets(buf, sizeof(buf), pipe);
                pclose(pipe);
                // 去除末尾换行
                size_t len = strlen(buf);
                while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
                {
                    buf[--len] = '\0';
                }
                if (len > 0)
                {
                    return std::string("macOS ") + buf;
                }
            }
            return "macOS (unknown version)";
#elif defined(__linux__)
            // Linux: 读取 /etc/os-release
            char name[128] = {};
            char version[128] = {};
            FILE* f = fopen("/etc/os-release", "r");
            if (f)
            {
                char line[256];
                while (fgets(line, sizeof(line), f))
                {
                    if (strncmp(line, "PRETTY_NAME=", 12) == 0)
                    {
                        char* val = line + 12;
                        // 去除引号和换行
                        size_t len = strlen(val);
                        while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '\r'))
                            val[--len] = '\0';
                        if (len > 0 && val[0] == '"')
                            val++, len--;
                        if (len > 0 && val[len - 1] == '"')
                            val[--len] = '\0';
                        strncpy(name, val, sizeof(name) - 1);
                    }
                    else if (strncmp(line, "VERSION=", 8) == 0)
                    {
                        char* val = line + 8;
                        size_t len = strlen(val);
                        while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '\r'))
                            val[--len] = '\0';
                        if (len > 0 && val[0] == '"')
                            val++, len--;
                        if (len > 0 && val[len - 1] == '"')
                            val[--len] = '\0';
                        strncpy(version, val, sizeof(version) - 1);
                    }
                }
                fclose(f);
                if (name[0] != '\0')
                    return std::string(name);
                if (version[0] != '\0')
                    return std::string("Linux ") + version;
            }
            return "Linux (unknown version)";
#else
            return "Unknown OS";
#endif
        }

        /// 获取当前电脑用户名
        inline std::string computerUsername()
        {
#ifdef _WIN32
            char username[UNLEN + 1] = {};
            DWORD usernameLen = UNLEN + 1;
            if (GetUserNameA(username, &usernameLen))
            {
                return std::string(username);
            }
            // 回退: 环境变量
            if (const char* u = std::getenv("USERNAME"))
                return std::string(u);
            return "unknown";
#elif defined(__APPLE__) || defined(__linux__)
            if (const char* u = std::getenv("USER"))
                return std::string(u);
            struct passwd* pw = getpwuid(getuid());
            if (pw && pw->pw_name)
                return std::string(pw->pw_name);
            return "unknown";
#else
            return "unknown";
#endif
        }

        /// 获取设备序列号 (用于追溯文件来源设备)
        inline std::string deviceSerialNumber()
        {
#ifdef _WIN32
            // Windows: 通过 WMIC 获取 BIOS SerialNumber
            char buf[256] = {};
            FILE* pipe = _popen("wmic bios get serialnumber 2>nul", "r");
            if (pipe)
            {
                fgets(buf, sizeof(buf), pipe);  // 跳过标题行
                fgets(buf, sizeof(buf), pipe);  // 读取序列号
                _pclose(pipe);
                size_t len = strlen(buf);
                while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' '))
                    buf[--len] = '\0';
                if (len > 0 && strcmp(buf, "To Be Filled By O.E.M.") != 0)
                    return std::string(buf);
            }
            // 回退: 使用 MAC 地址作为设备标识
            return "";
#elif defined(__APPLE__)
            // macOS: 通过 ioreg 获取 IOPlatformSerialNumber
            char buf[256] = {};
            FILE* pipe = popen("ioreg -c IOPlatformExpertDevice -d 2 2>/dev/null | grep IOPlatformSerialNumber | sed 's/.*= \\\"\\([^\\\"]*\\)\\\"/\\1/'", "r");
            if (pipe)
            {
                fgets(buf, sizeof(buf), pipe);
                pclose(pipe);
                size_t len = strlen(buf);
                while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
                    buf[--len] = '\0';
                if (len > 0)
                    return std::string(buf);
            }
            return "";
#elif defined(__linux__)
            // Linux: 读取 /sys/class/dmi/id/product_serial
            char buf[256] = {};
            FILE* f = fopen("/sys/class/dmi/id/product_serial", "r");
            if (f)
            {
                if (fgets(buf, sizeof(buf), f))
                {
                    size_t len = strlen(buf);
                    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
                        buf[--len] = '\0';
                    fclose(f);
                    if (len > 0 && strcmp(buf, "To Be Filled By O.E.M.") != 0)
                        return std::string(buf);
                }
                fclose(f);
            }
            return "";
#else
            return "";
#endif
        }

        /// 填充 SyDocument 的标准元数据（软件信息、时间、环境信息）
        ///
        /// @param doc         目标文档
        /// @param softwareName 软件名称 (如 "SanYi CAD 2D")
        /// @param softwareVer  软件版本 (如 "2.0.0")
        /// @param includeEnvInfo 是否填充环境溯源信息 (序列号、用户名、OS版本)
        inline void fillMetadata(SyDocument& doc,
            const char* softwareName,
            const char* softwareVer,
            bool includeEnvInfo = true)
        {
            doc.setMetadataVersion(SyFileConst::FILE_VERSION);
            doc.setMetadataFileVersion(1);
            doc.setSoftwareName(softwareName);
            doc.setSoftwareVersion(softwareVer);

            const std::string now = currentIsoTime();
            doc.setCreatedTime(now.c_str());
            doc.setModifiedTime(now.c_str());

            doc.setOperatingSystem(osName());

            if (includeEnvInfo)
            {
                doc.setSerialNumber(deviceSerialNumber().c_str());
                doc.setComputerUsername(computerUsername().c_str());
                doc.setOsVersion(osVersionDetail().c_str());
            }
        }

        /// 仅更新 modifiedTime（用于覆盖保存时不重写 createdTime）
        inline void updateModifiedTime(SyDocument& doc)
        {
            const std::string now = currentIsoTime();
            doc.setModifiedTime(now.c_str());
        }

    }  // namespace MetadataFiller
}  // namespace Fio

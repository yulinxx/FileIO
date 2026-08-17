#pragma once

#include "FileIO/FileIOAPI.h"
#include "FileIO/FileFormat.h"
#include "FileIO/SyDocument.h"
#include "FileIO/FioTypes.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Fio
{
    class ISyCryptoProvider;

    // ============================================================
    // SySerializer —— 文档序列化/反序列化核心
    //
    // 职责:
    //   1. SyDocument ↔ Protobuf 消息互转
    //   2. Protobuf 消息 ↔ 二进制数据 互转
    //   3. 文件格式解析和生成（Magic Header + 版本 + 加密检测）
    //
    // 设计模式: Facade Pattern (门面模式)
    //   - 对外提供简洁的 saveToFile / loadFromFile 接口
    //   - 内部编排 ProtoConverter + CryptoProvider
    //
    // ABI 说明（2026-07-31 C3 收口）:
    //   - 序列化结果 SerializeResult 改为纯 POD（错误消息固定 char[] 缓冲）
    //   - 文件路径统一 const char*；内存序列化输入/输出改为 BinaryBlob/BinaryBlobOut
    //   - 警告经 C 函数指针回调逐条输出（不使用 std::function / std::vector）
    //   - 实现细节（加密提供者等）通过 PIMPL 隐藏，头文件不含任何 STL
    // ============================================================

    /// 序列化结果（纯 POD，跨 DLL 安全）
    struct FILEIO_API SerializeResult
    {
        bool success = false;
        char errorMessage[512] = { 0 };

        static SerializeResult ok()
        {
            return { true, {} };
        }

        static SerializeResult fail(const char* msg)
        {
            SerializeResult r;
            r.success = false;
            if (msg)
            {
                std::strncpy(r.errorMessage, msg, sizeof(r.errorMessage) - 1);
            }
            return r;
        }
    };

    /// 警告回调（C 函数指针 + void* ctx，不使用 std::function）
    typedef void (*SerializeWarningCallback)(const char* msg, void* ctx);

    ///////////////////////////////////////////////////////////////////////
    // ---------------------------- 文件格式常量 ----------------------------
    namespace SyFileConst
    {
        /// 文件魔数: "SYPB" (SY ProtoBinary) - 2D 格式
        constexpr const char MAGIC_SY[4] = { 'S', 'Y', 'P', 'B' };

        /// 文件魔数: "SXPB" (SYX ProtoBinary) - 3D 格式
        constexpr const char MAGIC_SYX[4] = { 'S', 'X', 'P', 'B' };

        /// 当前文件格式版本
        constexpr uint32_t FILE_VERSION = 1;

        /// 文件头大小: magic(4) + version(4) + flags(4) + data_len(4) = 16 字节
        constexpr size_t HEADER_SIZE = 16;

        /// 文件尾 CRC32 大小
        constexpr size_t FOOTER_SIZE = 4;

        /// 加密标志位
        constexpr uint32_t FLAG_ENCRYPTED = 0x01;
    }  // namespace SyFileConst

    ////////////////////////////////////////////////////////////////////
    // ---------------------------- 序列化器 ----------------------------

    class FILEIO_API SySerializer
    {
    public:
        SySerializer();
        ~SySerializer();

        // 禁止拷贝，允许移动
        SySerializer(const SySerializer&) = delete;
        SySerializer& operator=(const SySerializer&) = delete;
        SySerializer(SySerializer&&) noexcept;
        SySerializer& operator=(SySerializer&&) noexcept;

        // ========== 加密配置 ==========

        /// 设置加密提供者（接管所有权），传入 nullptr 禁用加密
        void setCryptoProvider(ISyCryptoProvider* provider);

        /// 是否有加密提供者
        bool hasCrypto() const;

        // ========== 文件级操作 ==========

        /// 将文档保存到 .sy 文件
        /// @param filePath 文件路径（UTF-8）
        /// @param doc      文档数据
        /// @param encrypt  是否加密 (需要先 setCryptoProvider)
        SerializeResult saveToFile(const char* filePath,
            const SyDocument& doc,
            bool encrypt = false,
            SerializeWarningCallback warningCb = nullptr,
            void* warningCtx = nullptr);

        /// 将文档保存到 .sy/.syx 文件（格式感知版本）
        /// @param filePath 文件路径（UTF-8）
        /// @param doc      文档数据
        /// @param encrypt  是否加密
        /// @param fmt      目标格式，决定魔数选择（Native→SYPB, Native3D→SXPB）
        SerializeResult saveToFile(const char* filePath,
            const SyDocument& doc,
            bool encrypt,
            FileFormat fmt,
            SerializeWarningCallback warningCb = nullptr,
            void* warningCtx = nullptr);

        /// 从 .sy 文件加载文档
        /// @param filePath 文件路径（UTF-8）
        /// @param doc      [出参] 文档数据
        SerializeResult loadFromFile(const char* filePath,
            SyDocument& doc,
            SerializeWarningCallback warningCb = nullptr,
            void* warningCtx = nullptr);

        // ========== 内存级操作 ==========

        /// 将文档序列化为内存中的二进制数据
        /// @param doc 文档数据
        /// @param out 输出块（调用方提供缓冲区；out->data 为 nullptr 时仅查询大小）
        SerializeResult serializeToMemory(const SyDocument& doc,
            BinaryBlobOut* out,
            SerializeWarningCallback warningCb = nullptr,
            void* warningCtx = nullptr);

        /// 从内存中的二进制数据反序列化为文档
        /// @param in  序列化后的二进制数据
        /// @param doc [出参] 文档数据
        SerializeResult deserializeFromMemory(
            BinaryBlob in, SyDocument& doc, SerializeWarningCallback warningCb = nullptr, void* warningCtx = nullptr);

        // ========== 工具方法 ==========

        /// 获取当前文件格式版本
        static uint32_t fileVersion();

        /// 根据文件头魔数判断是否为有效的 .sy 文件
        static bool isValidSyFile(const uint8_t* header, size_t headerSize);

        /// 根据文件头魔数判断是否为有效的 .syx 文件
        static bool isValidSyxFile(const uint8_t* header, size_t headerSize);

    private:
        struct Impl;
        Impl* m_impl;
    };
}  // namespace Fio

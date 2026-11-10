#pragma once

#include "FileIO/FileIOAPI.h"
#include "FileIO/FileFormat.h"
#include "FileIO/AprDocument.h"
#include "FileIO/FioTypes.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Fio
{
    class IAprCryptoProvider;

    // ----
    // ---- AprSerializer —— 文档序列化/反序列化核心 ----
    //
    // 职责:
    //   1. AprDocument ↔ Protobuf 消息互转
    //   2. Protobuf 消息 ↔ 二进制数据 互转
    //   3. 文件格式解析和生成（Magic Header + 版本 + 加密检测）
    //
    // 设计模式: Facade Pattern (门面模式)
    //   - 对外提供简洁的 saveToFile / loadFromFile 接口
    //   - 内部编排 ProtoConverter + CryptoProvider
    //
    // ABI 说明:
    //   - 序列化结果 SerializeResult 为纯 POD（错误消息固定 char[] 缓冲）
    //   - 文件路径统一 const char*；内存序列化输入/输出为 BinaryBlob/BinaryBlobOut
    //   - 警告经 C 函数指针回调逐条输出（不使用 std::function / std::vector）
    //   - 实现细节（加密提供者等）通过 PIMPL 隐藏，头文件不含任何 STL
    // ----

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
                std::snprintf(r.errorMessage, sizeof(r.errorMessage), "%s", msg);
            }
            return r;
        }
    };

    /// 警告回调（C 函数指针 + void* ctx，不使用 std::function）
    typedef void (*SerializeWarningCallback)(const char* msg, void* ctx);

    /// 进度回调（C 函数指针 + void* ctx）
    /// @param progress 进度值 0.0 ~ 1.0
    /// @param stage    阶段名称（如 "serializing", "writing"）
    /// @param ctx      用户上下文指针
    typedef void (*SerializeProgressCallback)(float progress, const char* stage, void* ctx);

    // ----
    // ---- 文件格式常量 ----
    namespace AprFileConst
    {
        /// 文件魔数: "APRB" (APR ProtoBinary) - 2D 格式
        constexpr const char MAGIC_APR[4] = { 'A', 'P', 'R', 'B' };

        /// 文件魔数: "AP3D" (APR 3D ProtoBinary) - 3D 格式
        constexpr const char MAGIC_APR3D[4] = { 'A', 'P', '3', 'D' };

        /// 当前文件格式版本
        constexpr uint32_t FILE_VERSION = 1;

        /// 文件头大小: magic(4) + version(4) + flags(4) + data_len(4) = 16 字节
        constexpr size_t HEADER_SIZE = 16;

        /// 文件尾 CRC32 大小
        constexpr size_t FOOTER_SIZE = 4;

        /// 加密标志位
        constexpr uint32_t FLAG_ENCRYPTED = 0x01;
    }  // namespace AprFileConst

    // ----
    // ---- 序列化器 ----

    class FILEIO_API AprSerializer
    {
    public:
        AprSerializer();
        ~AprSerializer();

        // 禁止拷贝，允许移动
        AprSerializer(const AprSerializer&) = delete;
        AprSerializer& operator=(const AprSerializer&) = delete;
        AprSerializer(AprSerializer&&) noexcept;
        AprSerializer& operator=(AprSerializer&&) noexcept;

        // ---- 加密配置 ----

        /// 设置加密提供者（接管所有权），传入 nullptr 禁用加密
        void setCryptoProvider(IAprCryptoProvider* provider);

        /// 是否有加密提供者
        bool hasCrypto() const;

        // ---- 文件级操作 ----

        /// 将文档保存到 .apr 文件
        /// @param filePath 文件路径（UTF-8）
        /// @param doc      文档数据
        /// @param encrypt  是否加密 (需要先 setCryptoProvider)
        SerializeResult saveToFile(const char* filePath,
            const AprDocument& doc,
            bool encrypt = false,
            SerializeWarningCallback warningCb = nullptr,
            void* warningCtx = nullptr,
            SerializeProgressCallback progressCb = nullptr,
            void* progressCtx = nullptr);

        /// 将文档保存到 .apr/.apr3d 文件（格式感知版本）
        /// @param filePath 文件路径（UTF-8）
        /// @param doc      文档数据
        /// @param encrypt  是否加密
        /// @param fmt      目标格式，决定魔数选择（Native→APRB, Native3D→AP3D）
        SerializeResult saveToFile(const char* filePath,
            const AprDocument& doc,
            bool encrypt,
            FileFormat fmt,
            SerializeWarningCallback warningCb = nullptr,
            void* warningCtx = nullptr,
            SerializeProgressCallback progressCb = nullptr,
            void* progressCtx = nullptr);

        /// 从 .apr 文件加载文档
        /// @param filePath 文件路径（UTF-8）
        /// @param doc      [出参] 文档数据
        SerializeResult loadFromFile(const char* filePath,
            AprDocument& doc,
            SerializeWarningCallback warningCb = nullptr,
            void* warningCtx = nullptr);

        // ---- 内存级操作 ----

        /// 将文档序列化为内存中的二进制数据
        /// @param doc 文档数据
        /// @param out 输出块（调用方提供缓冲区；out->data 为 nullptr 时仅查询大小）
        SerializeResult serializeToMemory(const AprDocument& doc,
            BinaryBlobOut* out,
            SerializeWarningCallback warningCb = nullptr,
            void* warningCtx = nullptr);

        /// 从内存中的二进制数据反序列化为文档
        /// @param in  序列化后的二进制数据
        /// @param doc [出参] 文档数据
        SerializeResult deserializeFromMemory(
            BinaryBlob in, AprDocument& doc, SerializeWarningCallback warningCb = nullptr, void* warningCtx = nullptr);

        // ---- 工具方法 ----

        /// 获取当前文件格式版本
        static uint32_t fileVersion();

        /// 根据文件头魔数判断是否为有效的 .apr 文件
        static bool isValidAprFile(const uint8_t* header, size_t headerSize);

        /// 根据文件头魔数判断是否为有效的 .apr3d 文件
        static bool isValidApr3DFile(const uint8_t* header, size_t headerSize);

    private:
        struct AprSerializerImpl;
        AprSerializerImpl* m_impl;
    };
}  // namespace Fio

#pragma once

#include "FileIO/FileIOAPI.h"
#include "FileIO/SyDocument.h"
#include "FileIO/SyCryptoProvider.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Fio
{
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
    // ============================================================

    /// 序列化结果
    struct SerializeResult
    {
        bool        success = false;
        std::string errorMessage;
        std::vector<std::string> warnings;

        static SerializeResult ok()
        {
            return { true, {}, {} };
        }

        static SerializeResult ok(const std::vector<std::string>& warns)
        {
            return { true, {}, warns };
        }

        static SerializeResult fail(const std::string& msg)
        {
            return { false, msg, {} };
        }
    };

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
    } // namespace SyFileConst

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
        SySerializer(SySerializer&&) = default;
        SySerializer& operator=(SySerializer&&) = default;

        // ========== 加密配置 ==========

        /// 设置加密提供者，传入 nullptr 禁用加密
        void setCryptoProvider(CryptoProviderPtr provider);

        /// 是否有加密提供者
        bool hasCrypto() const;

        // ========== 文件级操作 ==========

        /// 将文档保存到 .sy 文件
        /// @param filePath 文件路径
        /// @param doc      文档数据
        /// @param encrypt  是否加密 (需要先 setCryptoProvider)
        SerializeResult saveToFile(const std::string& filePath,
            const SyDocument& doc,
            bool                       encrypt = false);

        /// 从 .sy 文件加载文档
        /// @param filePath 文件路径
        /// @param doc      [出参] 文档数据
        SerializeResult loadFromFile(const std::string& filePath,
            SyDocument& doc);

        // ========== 内存级操作 ==========

        /// 将文档序列化为内存中的二进制数据
        /// @param doc  文档数据
        /// @param data [出参] 序列化后的二进制数据 (protobuf 序列化结果)
        SerializeResult serializeToMemory(const SyDocument& doc,
            std::vector<uint8_t>& data);

        /// 从内存中的二进制数据反序列化为文档
        /// @param data protobuf 序列化后的二进制数据
        /// @param doc  [出参] 文档数据
        SerializeResult deserializeFromMemory(const std::vector<uint8_t>& data,
            SyDocument& doc);

        // ========== 工具方法 ==========

        /// 获取当前文件格式版本
        static uint32_t fileVersion();

        /// 根据文件头魔数判断是否为有效的 .sy 文件
        static bool isValidSyFile(const std::vector<uint8_t>& header);

        /// 根据文件头魔数判断是否为有效的 .syx 文件
        static bool isValidSyxFile(const std::vector<uint8_t>& header);

    private:
        /// 写入文件头
        bool writeFileHeader(std::vector<uint8_t>& buffer,
            uint32_t              dataLen,
            uint32_t              flags,
            const char            magic[4]);

        /// 读取并解析文件头
        struct FileHeaderResult
        {
            bool     valid = false;
            uint32_t version = 0;
            uint32_t flags = 0;
            uint32_t dataLen = 0;
            bool     isSyx = false;  // true = SYX 格式, false = SY 格式
        };
        FileHeaderResult readFileHeader(const std::vector<uint8_t>& buffer);

    private:
        CryptoProviderPtr m_cryptoProvider;
    };
} // namespace Fio
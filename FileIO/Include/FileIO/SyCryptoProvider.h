#pragma once

#include "FileIO/FileIOAPI.h"
#include "FileIO/FioTypes.h"  // for BinaryBlob

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace Fio
{
    // ============================================================
    // SyCryptoProvider —— 加密策略抽象层
    //
    // 设计模式: Strategy Pattern (策略模式)
    //   - 加密算法可插拔，不影响序列化核心逻辑
    //   - 默认提供 XOR 实现，可替换为 AES/ChaCha20 等
    //   - 派生类通过工厂注册即可无缝集成
    // ============================================================

    /// 加密结果（POD 安全，固定缓冲区 + BinaryBlob）
    struct CryptoResult
    {
        bool success = false;
        char errorMessage[256] = {};  // 固定缓冲区替代 std::string
        // 数据指针（由加密提供者分配，调用方通过 freeCryptoData() 释放）
        uint8_t* data = nullptr;
        size_t dataSize = 0;

        static CryptoResult ok(uint8_t* d, size_t sz)
        {
            CryptoResult r;
            r.success = true;
            r.data = d;
            r.dataSize = sz;
            return r;
        }

        static CryptoResult fail(const char* msg)
        {
            CryptoResult r;
            r.success = false;
            if (msg)
            {
                std::strncpy(r.errorMessage, msg, sizeof(r.errorMessage) - 1);
            }
            return r;
        }
    };

    ////////////////////////////////////////////////////////////////////
    // ---------------------------- 抽象接口 ----------------------------

    class FILEIO_API ISyCryptoProvider
    {
    public:
        // 析构定义移至 .cpp 避免 STL 跨 DLL 边界
        virtual ~ISyCryptoProvider();

        /// 算法名称（buffer 模式替代 std::string 返回）
        virtual size_t algorithmName(char* buffer, size_t bufferSize) const = 0;

        /// 加密（POD 指针+长度替代 std::vector）
        /// @param plaintext 明文数据指针
        /// @param plaintextSize 明文数据长度
        /// @return 加密结果（包含密文或错误信息）
        virtual CryptoResult encrypt(const uint8_t* plaintext, size_t plaintextSize) = 0;

        /// 解密（POD 指针+长度替代 std::vector）
        /// @param ciphertext 密文数据指针
        /// @param ciphertextSize 密文数据长度
        /// @return 解密结果（包含明文或错误信息）
        virtual CryptoResult decrypt(const uint8_t* ciphertext, size_t ciphertextSize) = 0;

        /// 释放 encrypt/decrypt 返回的 data 指针
        virtual void freeCryptoData(uint8_t* data) = 0;

        /// 是否需要密码/密钥
        virtual bool requiresKey() const
        {
            return true;
        }

        /// 设置密钥（const char* 替代 std::string）
        virtual bool setKey(const char* /*key*/)
        {
            return false;
        }
    };

    /// 加密提供者智能指针（内部使用，不推荐跨 DLL 传递）
    /// @deprecated 跨 DLL 使用 raw pointer + 工厂/释放模式
    using CryptoProviderPtr = std::unique_ptr<ISyCryptoProvider>;

    /////////////////////////////////////////////////////////////////////////
    // ---------------------------- 默认 XOR 实现 ----------------------------

    /// [B4-P1 警告] 基于 XOR + 密钥轮转的"加密"——实质为可逆混淆，非真正加密。
    /// 默认密钥 "SanYiDefaultKey2024" 硬编码在公共头文件中，trivially 可逆。
    /// 仅用于防止意外修改（如配置文件误改），不提供任何安全性。
    /// 如需真正加密，请使用 AesCryptoProvider（需 OpenSSL）。
    class FILEIO_API XorCryptoProvider : public ISyCryptoProvider
    {
    public:
        explicit XorCryptoProvider(const char* key = "SanYiDefaultKey2024");

        size_t algorithmName(char* buffer, size_t bufferSize) const override;
        CryptoResult encrypt(const uint8_t* plaintext, size_t plaintextSize) override;
        CryptoResult decrypt(const uint8_t* ciphertext, size_t ciphertextSize) override;
        void freeCryptoData(uint8_t* data) override;

        bool requiresKey() const override
        {
            return true;
        }

        bool setKey(const char* key) override;

    private:
        std::vector<uint8_t> m_key;
    };
}  // namespace Fio
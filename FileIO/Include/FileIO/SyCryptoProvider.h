#pragma once

#include "FileIO/FileIOAPI.h"

#include <cstdint>
#include <memory>
#include <string>
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

    /// 加密结果
    struct CryptoResult
    {
        bool        success = false;
        std::string errorMessage;
        std::vector<uint8_t> data;   // 加密/解密后的数据

        static CryptoResult ok(std::vector<uint8_t> d)
        {
            CryptoResult r;
            r.success = true;
            r.data = std::move(d);
            return r;
        }

        static CryptoResult fail(const std::string& msg)
        {
            CryptoResult r;
            r.success = false;
            r.errorMessage = msg;
            return r;
        }
    };

    // ---------------------------- 抽象接口 ----------------------------

    class FILEIO_API ISyCryptoProvider
    {
    public:
        virtual ~ISyCryptoProvider() = default;

        /// 算法名称（如 "XOR", "AES-128-CBC"）
        virtual std::string algorithmName() const = 0;

        /// 加密
        /// @param plaintext 明文数据
        /// @return 加密结果（包含密文或错误信息）
        virtual CryptoResult encrypt(const std::vector<uint8_t>& plaintext) = 0;

        /// 解密
        /// @param ciphertext 密文数据
        /// @return 解密结果（包含明文或错误信息）
        virtual CryptoResult decrypt(const std::vector<uint8_t>& ciphertext) = 0;

        /// 是否需要密码/密钥
        virtual bool requiresKey() const
        {
            return true;
        }

        /// 设置密钥
        virtual bool setKey(const std::string& key)
        {
            (void)key;
            return false;
        }
    };

    /// 加密提供者智能指针
    using CryptoProviderPtr = std::unique_ptr<ISyCryptoProvider>;

    // ---------------------------- 默认 XOR 实现 ----------------------------

    /// 基于 XOR + 密钥轮转的轻量加密
    /// 用于基本文件保护，性能极高，无外部依赖
    class FILEIO_API XorCryptoProvider : public ISyCryptoProvider
    {
    public:
        explicit XorCryptoProvider(const std::string& key = "SanYiDefaultKey2024");

        std::string algorithmName() const override
        {
            return "XOR";
        }
        CryptoResult encrypt(const std::vector<uint8_t>& plaintext) override;
        CryptoResult decrypt(const std::vector<uint8_t>& ciphertext) override;
        bool requiresKey() const override
        {
            return true;
        }
        bool setKey(const std::string& key) override;

    private:
        std::vector<uint8_t> m_key;
        void process(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);
    };

    // ---------------------------- 占位: AES 实现 (待用户扩展) ----------------------------

    /// 未来 AES 加密实现示例接口
    /// 用户可以在独立 .cpp 中实现并注册
    // class AesCryptoProvider : public ISyCryptoProvider { ... };
} // namespace Fio
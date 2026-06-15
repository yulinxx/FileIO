#include "FileIO/SyCryptoProvider.h"

#include <algorithm>
#include <cstring>

namespace Fio
{
    // ============================================================
    // XorCryptoProvider 实现
    // ============================================================

    XorCryptoProvider::XorCryptoProvider(const std::string& key)
    {
        setKey(key);
    }

    bool XorCryptoProvider::setKey(const std::string& key)
    {
        if (key.empty())
            return false;

        m_key.assign(key.begin(), key.end());
        return true;
    }

    CryptoResult XorCryptoProvider::encrypt(const std::vector<uint8_t>& plaintext)
    {
        if (m_key.empty())
        {
            return CryptoResult::fail("XOR encryption key is not set");
        }

        std::vector<uint8_t> output;
        process(plaintext, output);
        return CryptoResult::ok(std::move(output));
    }

    CryptoResult XorCryptoProvider::decrypt(const std::vector<uint8_t>& ciphertext)
    {
        // XOR 加密和解密是对称的
        return encrypt(ciphertext);
    }

    void XorCryptoProvider::process(const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output)
    {
        const size_t keyLen = m_key.size();
        output.resize(input.size());

        for (size_t i = 0; i < input.size(); ++i)
        {
            // 使用密钥轮转 + 位置混淆增强安全性
            const uint8_t keyByte = m_key[i % keyLen];
            const uint8_t posByte = static_cast<uint8_t>(i & 0xFF);
            output[i] = input[i] ^ keyByte ^ posByte;
        }
    }
} // namespace Fio
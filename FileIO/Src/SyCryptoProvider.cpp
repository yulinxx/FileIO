#include "FileIO/SyCryptoProvider.h"

#include <algorithm>
#include <cstring>

namespace Fio
{
    // ============================================================
    // XorCryptoProvider 实现
    // ============================================================

    XorCryptoProvider::XorCryptoProvider(const char* key)
    {
        setKey(key);
    }

    bool XorCryptoProvider::setKey(const char* key)
    {
        if (key == nullptr || key[0] == '\0')
            return false;

        const size_t keyLen = std::strlen(key);
        m_key.assign(key, key + keyLen);
        return true;
    }

    CryptoResult XorCryptoProvider::encrypt(const uint8_t* plaintext, size_t plaintextSize)
    {
        if (m_key.empty())
        {
            return CryptoResult::fail("XOR encryption key is not set");
        }

        // 分配输出缓冲区（调用方通过 freeCryptoData 释放）
        uint8_t* output = new uint8_t[plaintextSize];
        const size_t keyLen = m_key.size();

        for (size_t i = 0; i < plaintextSize; ++i)
        {
            // 使用密钥轮转 + 位置混淆增强安全性
            const uint8_t keyByte = m_key[i % keyLen];
            const uint8_t posByte = static_cast<uint8_t>(i & 0xFF);
            output[i] = plaintext[i] ^ keyByte ^ posByte;
        }

        return CryptoResult::ok(output, plaintextSize);
    }

    CryptoResult XorCryptoProvider::decrypt(const uint8_t* ciphertext, size_t ciphertextSize)
    {
        // XOR 加密和解密是对称的
        return encrypt(ciphertext, ciphertextSize);
    }

    size_t XorCryptoProvider::algorithmName(char* buffer, size_t bufferSize) const
    {
        const char* name = "XOR";
        const size_t len = std::strlen(name);
        if (buffer != nullptr && bufferSize > len)
            std::strcpy(buffer, name);
        return len;
    }

    void XorCryptoProvider::freeCryptoData(uint8_t* data)
    {
        delete[] data;
    }
} // namespace Fio
#include "FileIO/Parsers/NativeParser.h"
#include "FileIO/SySerializer.h"
#include "FileIO/SyDocument.h"
#include "SyDocumentData.h"

#include "Engine/SyEntity/SyEntity.h"

namespace Fio
{
    // ============================================================
    // NativeParser::Impl - PIMPL 隐藏实现细节
    // ============================================================
    class NativeParser::Impl
    {
    public:
        SySerializer serializer;
    };

    NativeParser::NativeParser()
        : m_impl(std::make_unique<Impl>())
    {
    }

    NativeParser::~NativeParser() = default;

    FileFormat NativeParser::format() const
    {
        return FileFormat::Native;
    }

    size_t NativeParser::formatName(char* buffer, size_t bufferSize) const
    {
        const char* name = "SanYi Native (Protobuf)";
        const size_t len = std::strlen(name);
        if (buffer != nullptr && bufferSize > len)
            std::strcpy(buffer, name);
        return len;
    }

    void NativeParser::forEachSupportedExtension(void(*visitor)(const char*, void*), void* ctx) const
    {
        visitor("sy", ctx);
    }

    ParseResult NativeParser::parse(const char* filePath,
        VecSyEntityPtr& outEntities)
    {
        SyDocument doc;
        auto result = m_impl->serializer.loadFromFile(filePath, doc);

        if (!result.success)
        {
            return ParseResult::fail(result.errorMessage);
        }

        // 将 SyDocument 中的图元移动到 outEntities
        outEntities.reserve(syDocumentData(doc).entities.size());
        for (auto& entity : syDocumentData(doc).entities)
        {
            outEntities.push_back(std::move(entity));
        }

        return ParseResult::ok();
    }
} // namespace Fio
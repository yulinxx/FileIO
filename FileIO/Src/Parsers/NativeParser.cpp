#include "FileIO/Parsers/NativeParser.h"
#include "FileIO/SySerializer.h"
#include "FileIO/SyDocument.h"

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

    std::string NativeParser::formatName() const
    {
        return "SanYi Native (Protobuf)";
    }

    std::vector<std::string> NativeParser::supportedExtensions() const
    {
        return { "sy" };
    }

    ParseResult NativeParser::parse(const std::string& filePath,
        VecSyEntityPtr& outEntities)
    {
        SyDocument doc;
        auto result = m_impl->serializer.loadFromFile(filePath, doc);

        if (!result.success)
        {
            return ParseResult::fail(result.errorMessage, result.warnings);
        }

        // 将 SyDocument 中的图元移动到 outEntities
        outEntities.reserve(doc.entities.size());
        for (auto& entity : doc.entities)
        {
            outEntities.push_back(std::move(entity));
        }

        return ParseResult::ok();
    }
} // namespace Fio
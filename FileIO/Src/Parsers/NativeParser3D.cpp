#include "FileIO/Parsers/NativeParser3D.h"
#include "FileIO/SySerializer.h"

#include "Engine/SyEntity/SyEntity.h"

namespace Fio
{
    class NativeParser3D::Impl
    {
    public:
        SySerializer serializer;
    };

    NativeParser3D::NativeParser3D()
        : m_impl(std::make_unique<Impl>())
    {
    }

    NativeParser3D::~NativeParser3D() = default;

    // ---- IFileParser 接口 ----

    FileFormat NativeParser3D::format() const
    {
        return FileFormat::Native3D;
    }

    std::string NativeParser3D::formatName() const
    {
        return "SanYi 3D Native (Protobuf)";
    }

    std::vector<std::string> NativeParser3D::supportedExtensions() const
    {
        return { "syx" };
    }

    ParseResult NativeParser3D::parse(const std::string& filePath,
        VecSyEntityPtr& outEntities)
    {
        SyDocument doc;
        auto result = parseDocument(filePath, doc);
        if (!result.success)
        {
            return ParseResult::fail(result.errorMessage, result.warnings);
        }

        // 返回 2D 实体
        outEntities.reserve(doc.entities.size());
        for (auto& entity : doc.entities)
        {
            outEntities.push_back(std::move(entity));
        }

        // meshEntities 通过 parseDocument 获取
        return ParseResult::ok(result.warnings);
    }

    // ---- 3D 完整文档读取 ----

    ParseResult NativeParser3D::parseDocument(const std::string& filePath,
        SyDocument& outDoc)
    {
        outDoc.clear();
        auto result = m_impl->serializer.loadFromFile(filePath, outDoc);
        if (!result.success)
        {
            return ParseResult::fail(result.errorMessage, result.warnings);
        }
        return ParseResult::ok(result.warnings);
    }
} // namespace Fio
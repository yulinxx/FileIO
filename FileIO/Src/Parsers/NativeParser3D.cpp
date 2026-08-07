#include "FileIO/Parsers/NativeParser3D.h"
#include "FileIO/SySerializer.h"
#include "SyDocumentData.h"

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

    size_t NativeParser3D::formatName(char* buffer, size_t bufferSize) const
    {
        const char* name = "SanYi 3D Native (Protobuf)";
        const size_t len = std::strlen(name);
        if (buffer != nullptr && bufferSize > len)
            std::strcpy(buffer, name);
        return len;
    }

    void NativeParser3D::forEachSupportedExtension(void(*visitor)(const char*, void*), void* ctx) const
    {
        visitor("syx", ctx);
    }

    ParseResult NativeParser3D::parse(const char* filePath,
        VecSyEntityPtr& outEntities)
    {
        SyDocument doc;
        auto result = parseDocument(filePath, doc);
        if (!result.success)
        {
            return result;
        }

        // 返回 2D 图元
        outEntities.reserve(syDocumentData(doc).entities.size());
        for (auto& entity : syDocumentData(doc).entities)
        {
            outEntities.push_back(std::move(entity));
        }

        // meshEntities 通过 parseDocument 获取
        return result;
    }

    // ---- 3D 完整文档读取 ----

    namespace
    {
        struct WarningCollector
        {
            std::vector<std::string> warnings;
        };

        void collectWarning(const char* msg, void* ctx)
        {
            static_cast<WarningCollector*>(ctx)->warnings.emplace_back(msg);
        }
    }

    ParseResult NativeParser3D::parseDocument(const char* filePath,
        SyDocument& outDoc)
    {
        outDoc.clear();
        WarningCollector collector;
        auto result = m_impl->serializer.loadFromFile(filePath, outDoc,
            collectWarning, &collector);

        if (!result.success)
            return ParseResult::fail(result.errorMessage, collector.warnings);

        return ParseResult::ok(collector.warnings);
    }
} // namespace Fio
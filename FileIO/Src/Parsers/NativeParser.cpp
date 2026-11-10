#include "FileIO/Parsers/NativeParser.h"
#include "FileIO/AprSerializer.h"
#include "FileIO/AprDocument.h"
#include "AprDocumentData.h"

#include "Engine/AprEntity/AprEntity.h"
#include "Log/AprLogger.h"

#include <cstdio>
#include <cstring>

namespace Fio
{
    // ----
    // ---- NativeParser::Impl —— PIMPL 隐藏实现细节 ----
    // ----
    class NativeParser::Impl
    {
    public:
        explicit Impl(FileFormat fmt)
            : targetFormat(fmt)
        {
        }

        /// 目标文件格式
        FileFormat targetFormat;

        /// 反序列化器实例
        AprSerializer serializer;
    };

    // ----
    // ---- 构造 / 析构 ----
    // ----

    NativeParser::NativeParser(FileFormat fmt)
        : m_impl(std::make_unique<Impl>(fmt))
    {
        APR_INFOF("[NativeParser] Created for format=%d (%s)",
            static_cast<int>(fmt),
            (fmt == FileFormat::Native3D) ? "3D" : "2D");
    }

    NativeParser::~NativeParser() = default;

    // ----
    // ---- IFileParser 接口 ----
    // ----

    FileFormat NativeParser::format() const
    {
        return m_impl->targetFormat;
    }

    size_t NativeParser::formatName(char* buffer, size_t bufferSize) const
    {
        const char* name =
            (m_impl->targetFormat == FileFormat::Native3D) ? "April 3D Native (Protobuf)" : "April Native (Protobuf)";
        const size_t len = std::strlen(name);
        if (buffer != nullptr && bufferSize > len)
        {
            std::snprintf(buffer, bufferSize, "%s", name);
        }
        return len;
    }

    void NativeParser::forEachSupportedExtension(void (*visitor)(const char*, void*), void* ctx) const
    {
        if (m_impl->targetFormat == FileFormat::Native3D)
        {
            visitor("apr3d", ctx);
        }
        else
        {
            visitor("apr", ctx);
        }
    }

    // ----
    // ---- ILegacyParser 接口 ----
    //
    // 仅返回 2D 图元，适用于不区分 2D/3D 的调用方。
    // 3D 场景使用 parseDocument() 获取完整 AprDocument。
    // ----

    ParseResult NativeParser::parse(const char* filePath, VecAprEntityPtr& outEntities)
    {
        APR_INFOF("[NativeParser] parse(): path=%s, format=%d",
            filePath ? filePath : "(null)",
            static_cast<int>(m_impl->targetFormat));

        AprDocument doc;
        auto result = parseDocument(filePath, doc);

        if (!result.success)
        {
            return result;
        }

        // 将 AprDocument 中的图元移动到 outEntities
        auto& docData = syDocumentData(doc);
        outEntities.reserve(docData.entities.size());
        for (auto& entity : docData.entities)
        {
            outEntities.push_back(std::move(entity));
        }

        APR_INFOF("[NativeParser] parse(): extracted %zu 2D entities", outEntities.size());
        return result;
    }

    // ----
    // ---- 完整文档读取（推荐路径） ----
    //
    // 支持同时读取 2D 图元和 3D 网格。
    // AprSerializer 根据文件头魔数自动检测格式。
    // ----

    ParseResult NativeParser::parseDocument(const char* filePath, AprDocument& outDoc)
    {
        if (!filePath || !*filePath)
        {
            APR_ERRORF("[NativeParser] parseDocument(): empty file path");
            return ParseResult::fail("Empty file path");
        }

        APR_INFOF("[NativeParser] parseDocument(): path=%s", filePath);

        outDoc.clear();

        // 警告收集回调
        struct WarningCollector
        {
            std::vector<std::string> warnings;
        };

        auto collectWarning = [](const char* msg, void* ctx) {
            static_cast<WarningCollector*>(ctx)->warnings.emplace_back(msg);
        };

        WarningCollector collector;
        auto result = m_impl->serializer.loadFromFile(filePath, outDoc, collectWarning, &collector);

        if (!result.success)
        {
            APR_ERRORF("[NativeParser] parseDocument() failed: %s", result.errorMessage);
            return ParseResult::fail(result.errorMessage, collector.warnings);
        }

        APR_INFOF("[NativeParser] parseDocument() succeeded: %zu entities, %zu warnings",
            syDocumentData(outDoc).entities.size(),
            collector.warnings.size());

        return ParseResult::ok(collector.warnings);
    }
}  // namespace Fio

#include "FileIO/FileParserFactory.h"

namespace Fio
{
    namespace
    {
        static struct NativeRegistrar
        {
            NativeRegistrar()
            {
                FileParserFactory::instance().registerParser(FileFormat::Native, []() -> IFileParser* {
                    return new NativeParser(FileFormat::Native);
                });
                FileParserFactory::instance().registerParser(FileFormat::Native3D, []() -> IFileParser* {
                    return new NativeParser(FileFormat::Native3D);
                });
            }
        } s_nativeRegistrar;
    }  // namespace
}  // namespace Fio
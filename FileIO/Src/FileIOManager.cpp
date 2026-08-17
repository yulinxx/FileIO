#include "FileIO/FileIOManager.h"
#include "FileIO/FileParserFactory.h"
#include "FileIO/FileWriterFactory.h"
#include "FileIO/Parsers/SvgParser.h"
#include "FileIO/IFileParser.h"
#include "FileIO/IFileWriter.h"
#include "FileIOInternal.h"
#include "Log/SyLogger.h"
#include "Engine/SyEntity/SyEntity.h"

#include <filesystem>
#include <cstring>
#include <string>
#include <vector>
#include <memory>

namespace Fio
{
    namespace
    {
        void writeError(char* buffer, size_t bufferSize, const std::string& message)
        {
            if (!buffer || bufferSize == 0)
            {
                return;
            }
            size_t n = message.size();
            if (n >= bufferSize)
            {
                n = bufferSize - 1;
            }
            std::memcpy(buffer, message.data(), n);
            buffer[n] = '\0';
        }

        size_t writeExtensions(const std::vector<std::string>& exts, char* buffer, size_t bufferSize)
        {
            std::string joined;
            for (size_t i = 0; i < exts.size(); ++i)
            {
                if (i > 0)
                {
                    joined += ' ';
                }
                joined += exts[i];
            }
            size_t needed = joined.size() + 1;  // 含结尾 '\0'
            if (buffer && bufferSize > 0)
            {
                size_t n = joined.size();
                if (n >= bufferSize)
                {
                    n = bufferSize - 1;
                }
                std::memcpy(buffer, joined.data(), n);
                buffer[n] = '\0';
            }
            return needed;
        }
    }  // namespace

    FileIOManager::FileIOManager()
    {
        FileParserFactory::instance().initDefaults();
        FileWriterFactory::instance().initDefaults();
    }

    FileIOManager::~FileIOManager() = default;

    void FileIOManager::setImportCallback(ImportCallback callback, void* ctx)
    {
        m_importCallback = callback;
        m_importCtx = ctx;
    }

    void FileIOManager::setExportCallback(ExportCallback callback, void* ctx)
    {
        m_exportCallback = callback;
        m_exportCtx = ctx;
    }

    bool FileIOManager::importFile(const char* filePath,
        FileFormat format,
        Eg::SyEntity*** outEntities,
        size_t* outCount,
        char* errorBuffer,
        size_t errorBufferSize)
    {
        return importFile(
            filePath, format, outEntities, outCount, errorBuffer, errorBufferSize, nullptr, nullptr, nullptr);
    }

    bool FileIOManager::importFile(const char* filePath,
        FileFormat format,
        Eg::SyEntity*** outEntities,
        size_t* outCount,
        char* errorBuffer,
        size_t errorBufferSize,
        WarningCallback warningCb,
        void* warningCtx,
        size_t* outLayerCount)
    {
        if (outEntities)
        {
            *outEntities = nullptr;
        }
        if (outCount)
        {
            *outCount = 0;
        }
        if (outLayerCount)
        {
            *outLayerCount = 0;
        }

        SY_INFOF("[FileIO] Importing file: %s (format=%d)", filePath ? filePath : "", static_cast<int>(format));
        if (format == FileFormat::STEP)
        {
            SY_INFOF("[FileIO] STEP/STP import requested: %s", filePath ? filePath : "");
        }

        // [F9-P1 修复] 移除操作开始前的"成功"回调，仅在操作完成后触发。

        auto& factory = FileParserFactory::instance();
        if (!factory.hasParser(format))
        {
            if (m_importCallback)
            {
                m_importCallback(filePath, false, m_importCtx);
            }
            writeError(errorBuffer, errorBufferSize, "No parser registered for this format");
            return false;
        }

        IFileParser* parser = factory.createParser(format);
        if (!parser)
        {
            if (m_importCallback)
            {
                m_importCallback(filePath, false, m_importCtx);
            }
            writeError(errorBuffer, errorBufferSize, "Failed to create parser");
            return false;
        }

        // 旧版 parse() 已内迁至 ILegacyParser（FileIO.dll 内部接口）
        ILegacyParser* legacyParser = dynamic_cast<ILegacyParser*>(parser);
        if (!legacyParser)
        {
            if (m_importCallback)
            {
                m_importCallback(filePath, false, m_importCtx);
            }
            factory.destroyParser(parser);
            writeError(errorBuffer, errorBufferSize, "Parser does not support legacy parse interface");
            return false;
        }

        VecSyEntityPtr entities;
        ParseResult result;
        try
        {
            result = legacyParser->parse(filePath, entities);
        }
        catch (...)
        {
            factory.destroyParser(parser);
            throw;
        }
        factory.destroyParser(parser);

        if (m_importCallback)
        {
            m_importCallback(filePath, result.success, m_importCtx);
        }

        if (!result.success)
        {
            writeError(errorBuffer, errorBufferSize, result.errorMessage);
            return false;
        }

        if (warningCb)
        {
            for (const auto& w : result.warnings)
            {
                warningCb(w.c_str(), warningCtx);
            }
        }
        if (outLayerCount)
        {
            *outLayerCount = result.dxfLayers.size();
        }

        // 转移所有权到裸指针数组
        size_t count = entities.size();
        auto* arr = new Eg::SyEntity*[count == 0 ? 1 : count];
        for (size_t i = 0; i < count; ++i)
        {
            arr[i] = entities[i].release();
        }

        if (outEntities)
        {
            *outEntities = arr;
        }
        if (outCount)
        {
            *outCount = count;
        }

        SY_INFOF("[FileIO] Imported %zu entities", count);
        return true;
    }

    bool FileIOManager::importFile(
        const char* filePath, Eg::SyEntity*** outEntities, size_t* outCount, char* errorBuffer, size_t errorBufferSize)
    {
        FileFormat fmt = detectFormat(filePath);
        if (fmt == FileFormat::Unknown)
        {
            writeError(
                errorBuffer, errorBufferSize, std::string("Unsupported file format: ") + (filePath ? filePath : ""));
            return false;
        }
        return importFile(filePath, fmt, outEntities, outCount, errorBuffer, errorBufferSize);
    }

    bool FileIOManager::importToIR(
        const char* filePath, FileFormat format, FioParseResult* outResult, char* errorBuffer, size_t errorBufferSize)
    {
        if (outResult)
        {
            *outResult = FioParseResult{};
        }

        SY_INFOF("[FileIO] Importing to IR: %s (format=%d)", filePath ? filePath : "", static_cast<int>(format));

        // [F9-P1 修复] 移除操作开始前的"成功"回调。旧代码在解析前就报 success=true，
        // 导致调用方无法区分"操作开始"和"操作完成成功"。回调仅在操作完成后触发。

        auto& factory = FileParserFactory::instance();
        if (!factory.hasParser(format))
        {
            if (m_importCallback)
            {
                m_importCallback(filePath, false, m_importCtx);
            }
            writeError(errorBuffer, errorBufferSize, "No parser registered for this format");
            return false;
        }

        IFileParser* parser = factory.createParser(format);
        if (!parser)
        {
            if (m_importCallback)
            {
                m_importCallback(filePath, false, m_importCtx);
            }
            writeError(errorBuffer, errorBufferSize, "Failed to create parser");
            return false;
        }

        // SVG 导入开关：是否把纯填充色块也导入为轮廓线
        if (auto* svgParser = dynamic_cast<SvgParser*>(parser))
        {
            svgParser->setImportFillAsOutline(m_svgImportFillAsOutline);
        }

        FioParseResult result;
        try
        {
            result = parser->parseToIR(filePath);
        }
        catch (...)
        {
            factory.destroyParser(parser);
            if (m_importCallback)
            {
                m_importCallback(filePath, false, m_importCtx);
            }
            writeError(errorBuffer, errorBufferSize, "Exception during IR parse");
            return false;
        }
        factory.destroyParser(parser);

        // parseToIR 返回空结果（entityCount == 0）视为解析失败，由调用方回退旧路径
        if (result.entityCount == 0)
        {
            if (m_importCallback)
            {
                m_importCallback(filePath, false, m_importCtx);
            }
            writeError(errorBuffer, errorBufferSize, "IR parse produced no entities");
            return false;
        }

        if (m_importCallback)
        {
            m_importCallback(filePath, true, m_importCtx);
        }

        if (outResult)
        {
            *outResult = result;
        }

        SY_INFOF("[FileIO] Imported IR: %u entities, %u layers", result.entityCount, result.layerCount);
        return true;
    }

    void FileIOManager::deleteEntities(Eg::SyEntity** entities, size_t count)
    {
        if (!entities)
        {
            return;
        }
        for (size_t i = 0; i < count; ++i)
        {
            delete entities[i];
        }
        delete[] entities;
    }

    void FileIOManager::freeEntityArray(Eg::SyEntity** entities)
    {
        delete[] entities;
    }

    bool FileIOManager::exportFile(const char* filePath,
        FileFormat format,
        const Eg::SyEntity* const* entities,
        size_t entityCount,
        char* errorBuffer,
        size_t errorBufferSize)
    {
        SY_INFOF("[FileIO] Exporting file: %s (format=%d, entities=%zu)",
            filePath ? filePath : "",
            static_cast<int>(format),
            entityCount);

        // [F9-P1 修复] 移除操作开始前的"成功"回调，仅在操作完成后触发。

        auto& factory = FileWriterFactory::instance();
        if (!factory.hasWriter(format))
        {
            if (m_exportCallback)
            {
                m_exportCallback(filePath, false, m_exportCtx);
            }
            writeError(errorBuffer, errorBufferSize, "No writer registered for this format");
            return false;
        }

        IFileWriter* writer = factory.createWriter(format);
        if (!writer)
        {
            if (m_exportCallback)
            {
                m_exportCallback(filePath, false, m_exportCtx);
            }
            writeError(errorBuffer, errorBufferSize, "Failed to create writer");
            return false;
        }

        // 旧版 write() 已内迁至 ILegacyWriter（FileIO.dll 内部接口）
        ILegacyWriter* legacyWriter = dynamic_cast<ILegacyWriter*>(writer);
        if (!legacyWriter)
        {
            if (m_exportCallback)
            {
                m_exportCallback(filePath, false, m_exportCtx);
            }
            factory.destroyWriter(writer);
            writeError(errorBuffer, errorBufferSize, "Writer does not support legacy write interface");
            return false;
        }

        // 借用裸指针包装为内部 VecSyEntityPtr（不转移所有权，写完归还）
        VecSyEntityPtr borrowed;
        borrowed.reserve(entityCount);
        for (size_t i = 0; i < entityCount; ++i)
        {
            borrowed.emplace_back(const_cast<Eg::SyEntity*>(entities[i]));
        }

        WriteResult result;
        try
        {
            result = legacyWriter->write(filePath, borrowed);
        }
        catch (...)
        {
            // 防止异常展开时误删调用方仍持有的图元
            for (auto& p : borrowed)
            {
                p.release();
            }
            factory.destroyWriter(writer);
            throw;
        }
        factory.destroyWriter(writer);

        // 归还所有权（writer 只读，不持有）
        for (auto& p : borrowed)
        {
            p.release();
        }

        if (m_exportCallback)
        {
            m_exportCallback(filePath, result.success, m_exportCtx);
        }

        if (!result.success)
        {
            writeError(errorBuffer, errorBufferSize, result.errorMessage);
            return false;
        }
        return true;
    }

    bool FileIOManager::exportFile(const char* filePath,
        const Eg::SyEntity* const* entities,
        size_t entityCount,
        char* errorBuffer,
        size_t errorBufferSize)
    {
        FileFormat fmt = detectFormat(filePath);
        if (fmt == FileFormat::Unknown)
        {
            writeError(errorBuffer,
                errorBufferSize,
                "Cannot determine format from file extension: " + std::string(filePath ? filePath : ""));
            return false;
        }
        return exportFile(filePath, fmt, entities, entityCount, errorBuffer, errorBufferSize);
    }

    FileFormat FileIOManager::detectFormat(const char* filePath) const
    {
        if (!filePath || !*filePath)
        {
            return FileFormat::Unknown;
        }

        std::filesystem::path path = std::filesystem::u8path(filePath);
        std::string ext = path.extension().string();
        if (!ext.empty() && ext[0] == '.')
        {
            ext = ext.substr(1);
        }

        FileFormat fmt = FileParserFactory::instance().detectFormat(ext.c_str());
        if (fmt != FileFormat::Unknown)
        {
            return fmt;
        }

        if (ext == "sy")
        {
            return FileFormat::Native;
        }
        if (ext == "bmp")
        {
            return FileFormat::BMP;
        }
        if (ext == "png")
        {
            return FileFormat::PNG;
        }
        if (ext == "igs" || ext == "iges")
        {
            return FileFormat::UG;
        }
        if (ext == "stp" || ext == "step")
        {
            return FileFormat::STEP;
        }

        return FileFormat::Unknown;
    }

    size_t FileIOManager::supportedImportExtensions(char* buffer, size_t bufferSize) const
    {
        std::vector<std::string> exts;
        FileParserFactory::instance().forEachSupportedExtension(
            [](const char* ext, void* ctx) {
                static_cast<std::vector<std::string>*>(ctx)->emplace_back(ext);
            },
            &exts);
        return writeExtensions(exts, buffer, bufferSize);
    }

    size_t FileIOManager::supportedExportExtensions(char* buffer, size_t bufferSize) const
    {
        std::vector<std::string> exts;
        FileWriterFactory::instance().forEachSupportedExtension(
            [](const char* ext, void* ctx) {
                static_cast<std::vector<std::string>*>(ctx)->emplace_back(ext);
            },
            &exts);
        return writeExtensions(exts, buffer, bufferSize);
    }

    bool FileIOManager::canImport(const char* filePath) const
    {
        FileFormat fmt = detectFormat(filePath);
        return fmt != FileFormat::Unknown && FileParserFactory::instance().hasParser(fmt);
    }

    bool FileIOManager::canExport(FileFormat format) const
    {
        return FileWriterFactory::instance().hasWriter(format);
    }
}  // namespace Fio
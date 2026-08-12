#pragma once

#include "FileIO/IFileParser.h"
#include "FileIO/FileFormat.h"
#include "FileIO/FioTypes.h"

namespace Fio
{
    class FILEIO_API StlParser : public IFileParser
    {
    public:
        StlParser() = default;
        ~StlParser() override = default;

        FileFormat format() const override
        {
            return FileFormat::STL;
        }

        void forEachSupportedExtension(void(*visitor)(const char*, void*), void* ctx) const override;

        size_t formatName(char* buffer, size_t bufferSize) const override;

        FioParseResult parseToIR(const char* filePath) override;
    };
} // namespace Fio

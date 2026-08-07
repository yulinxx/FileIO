#pragma once

#include "FileIO/IFileParser.h"
#include "FileIOInternal.h"

#include <cstring>

namespace Fio
{
    class UgParser : public IFileParser, public ILegacyParser
    {
    public:
        UgParser() = default;
        ~UgParser() override = default;

    public:
        FileFormat format() const override
        {
            return FileFormat::UG;
        }
        size_t formatName(char* buffer, size_t bufferSize) const override
        {
            const char* name = "Siemens NX / Unigraphics";
            const size_t len = std::strlen(name);
            if (buffer != nullptr && bufferSize > len)
                std::strcpy(buffer, name);
            return len;
        }
        void forEachSupportedExtension(void(*visitor)(const char*, void*), void* ctx) const override
        {
            visitor("prt", ctx);
            visitor("igs", ctx);
            visitor("iges", ctx);
        }

        ParseResult parse(const char* filePath, VecSyEntityPtr& outEntities) override;
    };
} // namespace Fio

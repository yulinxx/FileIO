#pragma once

#include "FileIO/IFileParser.h"

namespace Fio
{
    class UgParser : public IFileParser
    {
    public:
        UgParser() = default;
        ~UgParser() override = default;

    public:
        FileFormat format() const override
        {
            return FileFormat::UG;
        }
        std::string formatName() const override
        {
            return "Siemens NX / Unigraphics";
        }
        std::vector<std::string> supportedExtensions() const override
        {
            return { "prt", "igs", "iges", "stp", "step" };
        }

        ParseResult parse(const std::string& filePath, VecSyEntityPtr& outEntities) override;
    };
} // namespace Fio

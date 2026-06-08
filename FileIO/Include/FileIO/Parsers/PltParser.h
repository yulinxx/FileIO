#pragma once

#include "FileIO/IFileParser.h"

namespace Fio
{
    class PltParser : public IFileParser
    {
    public:
        PltParser() = default;
        ~PltParser() override = default;

    public:
        FileFormat format() const override;
        std::string formatName() const override;
        std::vector<std::string> supportedExtensions() const override;

        ParseResult parse(const std::string& filePath, VecSyEntityPtr& outEntities) override;
    };
} // namespace Fio

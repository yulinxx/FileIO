#pragma once

#include "FileIO/IFileParser.h"

namespace Fio
{
    class DxfParser : public IFileParser
    {
    public:
        DxfParser() = default;
        ~DxfParser() override = default;

        FileFormat format() const override;
        std::string formatName() const override;
        std::vector<std::string> supportedExtensions() const override;

        ParseResult parse(const std::string& filePath, VecSyEntityPtr& outEntities) override;
    };
} // namespace Fio

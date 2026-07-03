#pragma once

#include "FileIO/IFileParser.h"

namespace Fio
{
    /// ISO-10303 STEP/STP（含 Free3D / Open CASCADE 导出的 B-Rep 模型）
    class StepParser : public IFileParser
    {
    public:
        StepParser() = default;
        ~StepParser() override = default;

        FileFormat format() const override
        {
            return FileFormat::STEP;
        }

        std::string formatName() const override
        {
            return "STEP (ISO-10303)";
        }

        std::vector<std::string> supportedExtensions() const override
        {
            return { "stp", "step" };
        }

        ParseResult parse(const std::string& filePath, VecSyEntityPtr& outEntities) override;
    };
} // namespace Fio

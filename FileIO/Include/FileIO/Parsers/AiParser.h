#pragma once

#include "FileIO/IFileParser.h"

namespace Fio
{
    /**
     * @brief Adobe Illustrator (AI) 文件解析器
     *
     * 现代 AI 文件（AI 8+）本质上是 PDF 格式。
     * 使用 pdftocairo 将 AI 转换为 SVG，然后用 SvgParser 解析。
     * 需要用户安装 poppler-utils (pdftocairo)。
     */
    class AiParser : public IFileParser
    {
    public:
        AiParser() = default;
        ~AiParser() override = default;

    public:
        FileFormat format() const override;
        std::string formatName() const override;
        std::vector<std::string> supportedExtensions() const override;

        ParseResult parse(const std::string& filePath, VecSyEntityPtr& outEntities) override;
    };
} // namespace Fio

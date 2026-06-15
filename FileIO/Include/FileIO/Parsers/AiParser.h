#pragma once

#include "FileIO/Parsers/PdfBasedParser.h"

namespace Fio
{
    /**
     * @brief Adobe Illustrator (AI) 文件解析器
     *
     * 现代 AI 文件（AI 8+）本质上是 PDF 格式，旧版（AI 7-）为 PostScript 格式。
     * 使用 pdftocairo（和 Ghostscript）将 AI 转换为 SVG，然后用 SvgParser 解析。
     * 需要用户安装 poppler-utils (pdftocairo)，PostScript 格式还需 Ghostscript。
     */
    class AiParser : public PdfBasedParser
    {
    public:
        AiParser() = default;
        ~AiParser() override = default;

    public:
        FileFormat format() const override;
        std::string formatName() const override;
        std::vector<std::string> supportedExtensions() const override;

    protected:
        /// AI 合法性：接受 PDF 格式(AI 8+) 和 PostScript 格式(AI 7-)
        bool isValidSourceFormat(const std::string& filePath) const override;

        /// PostScript 格式的 AI 文件需要额外检查 Ghostscript
        std::string extraToolCheckError(const std::string& filePath) const override;
    };
} // namespace Fio
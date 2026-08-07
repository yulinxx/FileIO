#pragma once

#include "FileIO/Parsers/PdfBasedParser.h"

namespace Fio
{
    /**
     * @brief PDF 文件解析器
     *
     * 使用 pdftocairo 将 PDF 转换为 SVG，然后用 SvgParser 解析。
     * 需要用户安装 poppler-utils (pdftocairo)。
     */
    class PdfParser : public PdfBasedParser
    {
    public:
        PdfParser() = default;
        ~PdfParser() override = default;

    public:
        FileFormat format() const override;
        size_t formatName(char* buffer, size_t bufferSize) const override;
        void forEachSupportedExtension(void(*visitor)(const char*, void*), void* ctx) const override;

    protected:
        /// PDF 合法性：读取文件头 %PDF-
        bool isValidSourceFormat(const char* filePath) const override;
    };
} // namespace Fio
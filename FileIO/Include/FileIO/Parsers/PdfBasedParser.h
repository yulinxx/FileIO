#pragma once

#include "FileIO/IFileParser.h"
#include "FileIO/Parsers/PdfToSvgConverter.h"
#include "FileIO/Parsers/SvgParser.h"

#include <filesystem>
#include <chrono>

namespace Fio
{
    /**
     * @brief PDF 族解析器公共基类 —— 模板方法模式
     *
     * PDF 和 AI(AI 8+) 文件本质上都是 PDF 格式，两者的解析流程完全一致：
     *   1. 验证源文件合法
     *   2. 检查外部工具(pdftocairo)可用
     *   3. 转为临时 SVG
     *   4. 用 SvgParser 解析 SVG
     *
     * 子类只需覆写三个钩子方法，无需重复实现整个管道。
     */
    class PdfBasedParser : public IFileParser
    {
    public:
        ~PdfBasedParser() override = default;

        /// 模板方法：封装完整的 PDF→SVG→解析 管道，子类不可重写
        ParseResult parse(const std::string& filePath, VecSyEntityPtr& outEntities) override final
        {
            auto t0 = std::chrono::steady_clock::now();

            if (!std::filesystem::exists(filePath))
                return ParseResult::fail(formatName() + " file not found: " + filePath);

            // 子类自定义格式校验（PDF 只检查 PDF，AI 还检查 PostScript）
            if (!isValidSourceFormat(filePath))
                return ParseResult::fail("File is not a valid " + formatName() + ": " + filePath);

            // 检查 pdftocairo 外部工具
            if (!PdfToSvgConverter::isPdftocairoAvailable())
            {
                std::string hint = PdfToSvgConverter::getInstallHint();
                return ParseResult::fail("pdftocairo not found.\n\n" + hint);
            }

            // AI 文件如果是 PS 格式还需要 GhostScript（由子类返回提示）
            {
                std::string extraError = extraToolCheckError(filePath);
                if (!extraError.empty())
                    return ParseResult::fail(extraError);
            }

            // PDF→SVG 转换
            std::string tempSvg = PdfToSvgConverter::convertToTempSvg(filePath, 1);
            if (tempSvg.empty())
                return ParseResult::fail("Failed to convert " + formatName() + " to SVG: " + filePath);

            // 委托给 SvgParser 解析
            SvgParser svgParser;
            ParseResult result = svgParser.parse(tempSvg, outEntities);

            // 解析耗时统计（可用于性能分析或日志）
            // auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            //     std::chrono::steady_clock::now() - t0).count();

            return result;
        }

    protected:
        /// 验证源文件格式是否合法，子类必须实现
        virtual bool isValidSourceFormat(const std::string& filePath) const = 0;

        /// 额外工具检查（如 GhostScript），默认不需要
        /// @return 错误提示字符串，返回空表示检查通过
        virtual std::string extraToolCheckError(const std::string& /*filePath*/) const
        {
            return {};
        }
    };
} // namespace Fio
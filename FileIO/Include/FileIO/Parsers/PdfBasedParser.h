#pragma once

#include "FileIO/IFileParser.h"
#include "FileIO/Parsers/PdfToSvgConverter.h"
#include "FileIO/Parsers/SvgParser.h"

#include "Log/SyLogger.h"

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
            SY_INFOF("[PdfBasedParser] Start parsing %s: %s", formatName().c_str(), filePath.c_str());

            if (!std::filesystem::exists(filePath))
            {
                SY_ERRORF("[PdfBasedParser] File not found: %s", filePath.c_str());
                return ParseResult::fail(formatName() + " file not found: " + filePath);
            }

            // 子类自定义格式校验（PDF 只检查 PDF，AI 还检查 PostScript）
            if (!isValidSourceFormat(filePath))
            {
                SY_ERRORF("[PdfBasedParser] Format validation failed: %s", filePath.c_str());
                return ParseResult::fail("File is not a valid " + formatName() + ": " + filePath);
            }

            // 检查 pdftocairo 外部工具
            if (!PdfToSvgConverter::isPdftocairoAvailable())
            {
                std::string hint = PdfToSvgConverter::getInstallHint();
                SY_ERRORF("[PdfBasedParser] pdftocairo not found: %s", hint.c_str());
                return ParseResult::fail("pdftocairo not found.\n\n" + hint);
            }
            SY_INFO("[PdfBasedParser] pdftocairo available");

            // AI 文件如果是 PS 格式还需要 GhostScript（由子类返回提示）
            {
                std::string extraError = extraToolCheckError(filePath);
                if (!extraError.empty())
                {
                    SY_ERRORF("[PdfBasedParser] Extra tool check failed: %s", extraError.c_str());
                    return ParseResult::fail(extraError);
                }
            }

            // PDF→SVG 转换
            std::string tempSvg = PdfToSvgConverter::convertToTempSvg(filePath, 1);
            if (tempSvg.empty())
            {
                SY_ERRORF("[PdfBasedParser] PDF→SVG conversion failed: %s", filePath.c_str());
                return ParseResult::fail("Failed to convert " + formatName() + " to SVG: " + filePath);
            }
            SY_INFOF("[PdfBasedParser] PDF→SVG conversion completed: %s", tempSvg.c_str());

            // 委托给 SvgParser 解析
            SvgParser svgParser;
            ParseResult result = svgParser.parse(tempSvg, outEntities);

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            SY_INFOF("[PdfBasedParser] Parse completed: %lld ms, %zu entities", elapsed, outEntities.size());

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
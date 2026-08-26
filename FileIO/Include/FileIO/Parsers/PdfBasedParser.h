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
     * 仅实现 IR 路径（parseToIR），不保留旧 parse 路径。
     */
    class PdfBasedParser : public IFileParser
    {
    public:
        ~PdfBasedParser() override = default;

        /// 模板方法：封装完整的 PDF→SVG→解析 管道，子类不可重写
        // 输出中立 IR，跨 DLL 安全（委托给 SvgParser::parseToIR）
        FioParseResult parseToIR(const char* filePath) override final
        {
            auto t0 = std::chrono::steady_clock::now();

            // 日志一律带上具体格式名：PDF 与 AI 共用这套管道，只写 [PdfBasedParser]
            // 会导致两种导入在日志里无法区分
            char fmtNameBuf[128] = {};
            formatName(fmtNameBuf, sizeof(fmtNameBuf));
            const char* path = filePath ? filePath : "(null path)";

            SY_INFOF("[PdfBasedParser:%s] parseToIR START: %s", fmtNameBuf, path);

            if (!filePath || !std::filesystem::exists(filePath))
            {
                SY_ERRORF("[PdfBasedParser:%s] File not found: %s", fmtNameBuf, path);
                return FioParseResult{};
            }

            // 子类自定义格式校验（PDF 只检查 PDF，AI 还检查 PostScript）
            if (!isValidSourceFormat(filePath))
            {
                SY_ERRORF("[PdfBasedParser:%s] Format validation failed: %s", fmtNameBuf, path);
                return FioParseResult{};
            }

            // 检查 pdftocairo 外部工具
            if (!PdfToSvgConverter::isPdftocairoAvailable())
            {
                std::string hint = PdfToSvgConverter::getInstallHint();
                SY_ERRORF("[PdfBasedParser:%s] pdftocairo not found: %s", fmtNameBuf, hint.c_str());
                return FioParseResult{};
            }
            SY_INFOF("[PdfBasedParser:%s] pdftocairo available", fmtNameBuf);

            // AI 文件如果是 PS 格式还需要 GhostScript（由子类返回提示）
            {
                std::string extraError = extraToolCheckError(filePath);
                if (!extraError.empty())
                {
                    SY_ERRORF("[PdfBasedParser:%s] Extra tool check failed: %s", fmtNameBuf, extraError.c_str());
                    return FioParseResult{};
                }
            }

            // PDF→SVG 转换
            std::string tempSvg = PdfToSvgConverter::convertToTempSvg(filePath, 1);
            if (tempSvg.empty())
            {
                SY_ERRORF("[PdfBasedParser:%s] PDF->SVG conversion failed: %s", fmtNameBuf, path);
                return FioParseResult{};
            }
            SY_INFOF("[PdfBasedParser:%s] PDF->SVG conversion completed: %s", fmtNameBuf, tempSvg.c_str());

            // 委托给 SvgParser 解析
            SvgParser svgParser;
            FioParseResult result = svgParser.parseToIR(tempSvg.c_str());

            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

            if (result.entityCount == 0)
            {
                // 中间 SVG 解析不出图元：多为矢量内容被光栅化（扫描件 / 位图 PDF），这类文件本工具链无法导入
                SY_WARNF("[PdfBasedParser:%s] parseToIR END: no entity from intermediate SVG (%s), %lld ms: %s",
                    fmtNameBuf,
                    tempSvg.c_str(),
                    static_cast<long long>(elapsed),
                    path);
                return result;
            }

            SY_INFOF("[PdfBasedParser:%s] parseToIR END: %u entities, %u layers, %u warnings, %lld ms: %s",
                fmtNameBuf,
                result.entityCount,
                result.layerCount,
                result.warningCount,
                static_cast<long long>(elapsed),
                path);

            return result;
        }

    protected:
        /// 验证源文件格式是否合法，子类必须实现
        virtual bool isValidSourceFormat(const char* filePath) const = 0;

        /// 额外工具检查（如 GhostScript），默认不需要
        /// @return 错误提示字符串，返回空表示检查通过
        virtual std::string extraToolCheckError(const char* /*filePath*/) const
        {
            return {};
        }
    };
}  // namespace Fio

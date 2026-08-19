#pragma once

#include "FileIO/IFileParser.h"

namespace Fio
{
    /// SVG 解析器 — 仅实现 IR 路径（parseToIR），不保留旧 parse 路径
    // SVG path 经贝塞尔自适应采样后离散为 Polyline
    // 顶点数据（double 序列 x0,y0,x1,y1,...）存入 extensionBlob
    class SvgParser : public IFileParser
    {
    public:
        SvgParser() = default;
        ~SvgParser() override = default;

    public:
        FileFormat format() const override;
        size_t formatName(char* buffer, size_t bufferSize) const override;
        void forEachSupportedExtension(void (*visitor)(const char*, void*), void* ctx) const override;

        // 输出中立 IR，跨 DLL 安全
        // SVG path 采样为 Polyline，顶点数据存入 extensionBlob
        FioParseResult parseToIR(const char* filePath) override;

        /// 配置：是否把纯填充(fill-only)色块也导入为闭合轮廓线。
        /// 默认 true = 纯填充色块（无描边，常见于 Illustrator 导出的图标/Logo）按
        /// 其填充色绘制轮廓（闭合曲线），避免整图被跳过、导入无图元。
        /// 关闭后，仅保留带描边的线条，跳过纯填充色块。
        void setImportFillAsOutline(bool enable)
        {
            m_importFillAsOutline = enable;
        }

        bool importFillAsOutline() const
        {
            return m_importFillAsOutline;
        }

    private:
        bool m_importFillAsOutline = true;
    };
}  // namespace Fio

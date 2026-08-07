#pragma once

#include "FileIO/IFileParser.h"

#include <cstring>

namespace Fio
{
    /// ISO-10303 STEP/STP（含 Free3D / Open CASCADE 导出的 B-Rep 模型）
    // 仅实现 IR 路径（parseToIR），不保留旧 parse 路径
    // STEP 逻辑简单（投影 + Polyline），IR 路径可完全覆盖
    class StepParser : public IFileParser
    {
    public:
        StepParser() = default;
        ~StepParser() override = default;

        FileFormat format() const override
        {
            return FileFormat::STEP;
        }

        size_t formatName(char* buffer, size_t bufferSize) const override
        {
            const char* name = "STEP (ISO-10303)";
            const size_t len = std::strlen(name);
            if (buffer != nullptr && bufferSize > len)
                std::strcpy(buffer, name);
            return len;
        }

        void forEachSupportedExtension(void(*visitor)(const char*, void*), void* ctx) const override
        {
            visitor("stp", ctx);
            visitor("step", ctx);
        }

        // 输出中立 IR，跨 DLL 安全
        // STEP 投影为 2D Polyline，顶点数据存入 extensionBlob
        FioParseResult parseToIR(const char* filePath) override;
    };
} // namespace Fio

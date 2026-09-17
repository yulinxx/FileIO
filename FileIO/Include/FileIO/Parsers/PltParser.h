#pragma once

#include "FileIO/IFileParser.h"

namespace Fio
{
    /// HPGL Plot (PLT) 解析器 — 仅实现 IR 路径
    // PltHpglInterpreter 直接输出 EntityInfo（POD），不依赖 Engine2D 类型
    // PLT 逻辑简单（线段/弧/圆），IR 路径可完全覆盖，无需旧 parse 回退
    class PltParser : public IFileParser
    {
    public:
        PltParser() = default;
        ~PltParser() override = default;

        FileFormat format() const override;
        size_t formatName(char* buffer, size_t bufferSize) const override;
        void forEachSupportedExtension(void (*visitor)(const char*, void*), void* ctx) const override;

        // 输出中立 IR，跨 DLL 安全
        // PLT 几何（Line/Arc/Circle）全部用 EntityInfo 内联字段承载，无需 extensionBlob
        FioParseResult parseToIR(const char* filePath) override;

        // 带进度：按已读取字节数 / 文件总大小上报，逐行解析大 PLT 时进度条平滑推进
        FioParseResult parseToIRWithProgress(
            const char* filePath, ParseProgressCallback onProgress, void* progressCtx) override;

    private:
        FioParseResult parseToIRImpl(const char* filePath, ParseProgressCallback onProgress, void* progressCtx);
    };
}  // namespace Fio

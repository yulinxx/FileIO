#include "FileIO/IFileParser.h"

#include "Log/SyLogger.h"

namespace Fio
{
    // 析构定义在 DLL 内，避免 STL 成员跨 DLL 边界销毁
    IFileParser::~IFileParser() = default;

    // parseToIR 默认实现：该格式尚未接入中立 IR 链路。
    // 这里必须留一条 ERROR：否则调用方只看到"空结果"，无法区分
    //   1) 文件真的没有内容
    //   2) 这个 parser 压根没实现 IR 路径
    // 目前落到此分支的只有 NativeParser（.sy / .syx，仍走 legacy 路径）。
    FioParseResult IFileParser::parseToIR(const char* filePath)
    {
        SY_ERRORF("[IFileParser] parseToIR not implemented for format=%d, returning empty IR: %s",
            static_cast<int>(format()),
            filePath ? filePath : "(null path)");
        return FioParseResult{};
    }
}  // namespace Fio

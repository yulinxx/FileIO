#include "FileIO/IFileParser.h"

namespace Fio
{
    // 析构定义在 DLL 内，避免 STL 成员跨 DLL 边界销毁
    IFileParser::~IFileParser() = default;

    // parseToIR 默认实现：回退到旧版 parse()
    FioParseResult IFileParser::parseToIR(const char* /*filePath*/)
    {
        // 默认实现返回空结果，子类应重写此方法
        return FioParseResult{};
    }
}  // namespace Fio
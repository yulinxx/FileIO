#include "FileIO/SyCryptoProvider.h"

namespace Fio
{
    // 析构定义在 DLL 内，避免 STL 成员跨 DLL 边界销毁
    ISyCryptoProvider::~ISyCryptoProvider() = default;
} // namespace Fio
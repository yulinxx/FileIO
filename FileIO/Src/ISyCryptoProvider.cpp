#include "FileIO/AprCryptoProvider.h"

namespace Fio
{
    // 析构定义在 DLL 内，避免 STL 成员跨 DLL 边界销毁
    IAprCryptoProvider::~IAprCryptoProvider() = default;
}  // namespace Fio
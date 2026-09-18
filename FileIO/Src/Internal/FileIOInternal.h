#pragma once

// 向后兼容 wrapper：原 FileIOInternal.h 的类型已迁至公共头 ILegacyTypes.h。
// 所有 Src/ 下的 .cpp 文件仍可 include 此头文件，无需改动。
// 新代码应直接 #include "FileIO/ILegacyTypes.h"。

#include "FileIO/ILegacyTypes.h"

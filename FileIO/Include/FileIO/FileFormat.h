#pragma once

#include <vector>
#include <memory>

namespace Eg
{
    struct SyEntity;
}

namespace Fio
{
    /// 实体指针向量（使用智能指针管理内存）
    using VecSyEntityPtr = std::vector<std::unique_ptr<Eg::SyEntity>>;

    enum class FileFormat
    {
        Unknown,

        DXF,
        PLT,
        SVG,
        UG,
        STEP,     // .stp / .step - ISO-10303 (Free3D, Open CASCADE, etc.)
        PDF,
        AI,

        Native,     // .sy  - 2D 原生格式
        Native3D,   // .syx - 3D 原生格式

        BMP,
        PNG,
    };
} // namespace Fio
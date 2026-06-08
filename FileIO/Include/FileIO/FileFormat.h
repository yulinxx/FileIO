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
        PDF,
        AI,

        Native,

        BMP,
        PNG,
    };
} // namespace Fio
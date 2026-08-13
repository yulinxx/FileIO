#pragma once

#include <cstdint>

namespace Fio
{
    enum class FileFormat
    {
        Unknown,

        DXF,
        PLT,
        SVG,
        UG,
        STEP,  // .stp / .step - ISO-10303 (Free3D, Open CASCADE, etc.)
        PDF,
        AI,

        Native,    // .sy  - 2D 原生格式
        Native3D,  // .syx - 3D 原生格式

        BMP,
        PNG,

        // 3D 格式
        OBJ,  // .obj - Wavefront OBJ
        STL,  // .stl - Stereolithography
    };
}  // namespace Fio
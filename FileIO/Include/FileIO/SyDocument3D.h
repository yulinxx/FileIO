#pragma once

#include "FileIO/FileIOAPI.h"
#include "Engine3D/SyEntity/SyMeshEntity.h"
#include <memory>
#include <string>
#include <vector>

namespace Fio
{
    // ============================================================
    // SyDocument3D —— 文档的 3D 网格扩展
    //
    // 单独拆出来避免 Engine2D 等模块被强制依赖 Engine3D。
    // 需要 3D 支持的模块（如 Render3D）可以额外包含此头文件。
    // ============================================================

    struct FILEIO_API SyDocument3D
    {
        std::vector<std::unique_ptr<Eg::SyMeshEntity>> meshEntities;  // 3D 网格图元

        SyDocument3D() = default;
        ~SyDocument3D();
        SyDocument3D(const SyDocument3D&) = delete;
        SyDocument3D& operator=(const SyDocument3D&) = delete;
        SyDocument3D(SyDocument3D&&) = default;
        SyDocument3D& operator=(SyDocument3D&&) = default;

        void clear();
    };
} // namespace Fio

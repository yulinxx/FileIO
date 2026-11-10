#pragma once

#include "FileIO/FileIOAPI.h"

#include <cstddef>

namespace Eg
{
    struct AprMeshEntity;
}

namespace Fio
{
    // ----
    // ---- AprDocument3D —— 文档的 3D 网格扩展 ----
    //
    // 单独拆出来避免 Engine2D 等模块被强制依赖 Engine3D。
    // 需要 3D 支持的模块（如 Render3D）可以额外包含此头文件。
    //
    // 全 PIMPL：公开头不包含 Engine3D 头文件，也不暴露任何 std 类型。
    // 网格图元数据存放于内部数据模型。
    // ----

    struct AprDocument3DData;  // 内部数据模型（前向声明，仅在 DLL 内部定义）

    class FILEIO_API AprDocument3D
    {
    public:
        AprDocument3D();
        ~AprDocument3D();
        AprDocument3D(AprDocument3D&&) noexcept;
        AprDocument3D& operator=(AprDocument3D&&) noexcept;
        AprDocument3D(const AprDocument3D&) = delete;
        AprDocument3D& operator=(const AprDocument3D&) = delete;

        void clear();

        size_t meshEntityCount() const;
        /// 获取第 index 个 3D 网格图元（借用指针，不转移所有权）
        Eg::AprMeshEntity* meshEntityAt(size_t index) const;
        /// 添加 3D 网格图元（接管所有权，传入的指针由文档负责释放）
        void addMeshEntity(Eg::AprMeshEntity* entity);

    private:
        friend AprDocument3DData& syDocument3DData(AprDocument3D& doc);
        friend const AprDocument3DData& syDocument3DData(const AprDocument3D& doc);

        AprDocument3DData* m_data;
    };
}  // namespace Fio

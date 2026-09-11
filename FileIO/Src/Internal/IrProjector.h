#pragma once

// ============================================================================
// IrProjector —— FileIO 内部 IR 到跨 DLL IR 的**唯一**投影点
//
// 背景：
//   在此之前，每个解析器都自己声明一组 `thread_local std::vector<...>` 缓冲区，
//   自己拼 FioParseResult，自己决定 sourceFormat / sourceUnit 填不填。结果是
//   同一套内存契约被复制了 6 份、群组通道没人填、单位口径各写各的。
//
// 本文件把这件事收敛成两个入口，2D / 3D 解析器共用同一套语义：
//
//   1. IrPublisher —— 缓冲区持有者 + 发布器。
//      适用于「直接构造 EntityInfo」的解析器（DXF / SVG / PLT / STL / STEP / IGES）。
//      解析器向 entities()/layers()/groups()/blob() 追加数据，最后调用 publish()
//      拿到 FioParseResult。所有 thread_local 缓冲区只在本文件里声明一次。
//
//   2. IrProjector::project —— ParseData → FioParseResult 的结构化投影。
//      适用于产出内部富 IR（ParsedGeometry 树）的解析器（OBJ / 未来的 SVG 重构）。
//      内部同样走 IrPublisher，因此两条路的内存契约完全一致。
//
// 内存契约（与 FioTypes.h 中 FioParseResult 的注释必须保持一致）：
//   - 返回的 FioParseResult 里所有指针都指向本文件的 thread_local 缓冲区；
//   - 调用方只读、绝不释放；
//   - 有效期到「同一线程下一次调用任意解析入口」为止；
//   - 每个线程一套缓冲区，因此并发解析（每线程一个文件）是安全的。
// ============================================================================

#include "FileIO/FioTypes.h"
#include "ParsedGeometry.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Fio
{
    /// 跨 DLL IR 缓冲区持有者与发布器（每线程一份）
    ///
    /// 典型用法：
    /// ```
    /// auto& pub = IrPublisher::threadLocal();
    /// pub.reset();                       // 必须先 reset，作废上一次解析的缓冲区
    /// EntityInfo info{};
    /// info.type = EntityType::Polyline;
    /// info.extensionDataOffset = pub.appendBlob(verts.data(), bytes);
    /// info.extensionDataSize   = static_cast<uint32_t>(bytes);
    /// pub.entities().push_back(info);
    /// return pub.publish("DXF", "mm");
    /// ```
    class IrPublisher
    {
    public:
        /// 取当前线程的发布器实例
        static IrPublisher& threadLocal();

        /// 清空所有缓冲区。**每次解析入口开头必须调用**，否则会拼接上一次的结果。
        void reset();

        std::vector<EntityInfo>& entities() { return m_entities; }
        std::vector<IrLayerInfo>& layers() { return m_layers; }
        std::vector<IrGroupInfo>& groups() { return m_groups; }
        std::vector<uint8_t>& blob() { return m_blob; }
        std::vector<std::string>& warnings() { return m_warnings; }

        /// 向扩展数据块追加一段字节，返回追加前的偏移（即 EntityInfo::extensionDataOffset）
        ///
        /// 偏移与长度都是 uint32_t，超过 4GiB 的扩展数据无法表达；本函数在越界时
        /// 返回 kInvalidOffset 且不追加任何数据，调用方须据此跳过该图元并记录 warning。
        uint32_t appendBlob(const void* data, std::size_t bytes);

        /// 追加失败（扩展数据块将超过 uint32_t 可表达范围）时 appendBlob 的返回值
        static constexpr uint32_t kInvalidOffset = 0xFFFFFFFFu;

        /// 追加一个图层，返回分配到的 sourceId（1-based，0 保留为「未分配图层」哨兵）
        ///
        /// 同名图层只登记一次（DXF 的 LAYER 表可能出现重复记录），重复调用直接返回已有 id。
        /// 图层数达到 kMaxLayers 时不再登记并返回 0（未分配），只在首次触顶时记一条 warning。
        uint32_t addLayer(const std::string& name, uint32_t argbColor, bool visible, bool locked = false);

        /// 追加一个群组，返回分配到的 sourceId（1-based，0 保留为「无群组」哨兵）
        /// parentSourceId 传 0 表示顶层群组。
        ///
        /// 群组数达到 kMaxGroups 时不再新建，直接返回 parentSourceId：
        /// 层级退化成挂在父群组下，但图元不会丢。只在首次触顶时记一条 warning。
        uint64_t addGroup(const std::string& name, uint64_t parentSourceId);

        /// 按图层名查已登记的图层 sourceId；未找到返回 0。O(1) 哈希查找。
        uint32_t findLayer(const std::string& name) const;

        /// 图层数上限。与 Engine 侧 LayerManager::kMaxLayerCount 对齐：
        /// 超出部分在下游也只会塌回默认图层，不如在解析侧就挡住并告警。
        static constexpr std::size_t kMaxLayers = 1024;

        /// 群组数上限。DXF 的 INSERT 阵列「一次引用 = 一个群组」，
        /// cols/rows 各自可达 4096，空块阵列不推进图元计数，故必须单独设闸。
        static constexpr std::size_t kMaxGroups = 65536;

        /// 组装 FioParseResult 并返回。不清空缓冲区（指针要继续有效）。
        ///
        /// @param sourceFormat 格式名，如 "DXF"、"OBJ"；写入 FioParseResult::sourceFormat
        /// @param sourceUnit   来源单位，如 "mm"、"inch"；nullptr 表示文件未声明单位
        FioParseResult publish(const char* sourceFormat, const char* sourceUnit = nullptr);

    private:
        std::vector<EntityInfo> m_entities;
        std::vector<IrLayerInfo> m_layers;
        std::vector<IrGroupInfo> m_groups;
        std::vector<uint8_t> m_blob;
        std::vector<std::string> m_warnings;

        /// 图层名（已按 IrLayerInfo::name 长度截断）→ sourceId。
        /// 键必须用截断后的名字：否则长名图层登记与查找会失配。
        std::unordered_map<std::string, uint32_t> m_layerIndex;

        bool m_layerLimitWarned = false;
        bool m_groupLimitWarned = false;
    };

    /// ParseData（FileIO 内部富 IR） → FioParseResult（跨 DLL POD IR）
    namespace IrProjector
    {
        /// 把一份完整的 ParseData 投影成跨 DLL IR。
        ///
        /// 投影规则：
        ///   - 图层：ParsedLayer 原样搬运，name 截断到 255 字节；
        ///   - 群组：只保留 sourceId / parentSourceId / name；成员关系取
        ///     ParsedGeometry::groupSourceId（权威），ParsedGroup::entitySourceIds 忽略；
        ///     父 id 指向不存在的群组时降级为顶层并记 warning；
        ///   - 图元：按 ParsedGeometryType 逐类映射到 EntityInfo，复杂几何写入扩展数据块，
        ///     布局与 FioEntityConverter 的读取顺序严格对应（见 .cpp 内各分支注释）；
        ///   - data.success == false 时返回空的 FioParseResult{}（与各解析器失败路径一致）。
        ///
        /// @return 指向 thread_local 缓冲区的 FioParseResult，只读、不可释放。
        FioParseResult project(const ParseData& data, const char* sourceFormat, const char* sourceUnit = nullptr);
    }  // namespace IrProjector
}  // namespace Fio

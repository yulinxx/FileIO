#pragma once

#include "FileIO/FileIOAPI.h"
#include "Engine/EngineAPI.h"

// 完整引入 Entity 类型，因为 unique_ptr 需析构完整类型
#include "Engine2D/SyEntity/SyEntity.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Fio
{
    // ============================================================
    // SyDocument —— 文档数据模型（纯 C++ 类型，不依赖 Protobuf）
    //
    // 设计说明:
    //   - SyDocument 是序列化系统的核心数据载体
    //   - 它是 Protobuf 消息与 C++ 领域对象之间的 "适配层"
    //   - 公开接口完全不暴露 Protobuf 类型，降低耦合
    //   - 所有层次都支持 custom_properties 键值对扩展
    // ============================================================

    // ---------------------------- 属性键值对 ----------------------------

    using PropertyMap = std::map<std::string, std::string>;

    // ---------------------------- 文档元信息 ----------------------------

    struct FILEIO_API DocumentMetadata
    {
        int32_t  version = 1;               // SY 文件格式版本
        int32_t  fileVersion = 1;           // 应用文件版本号
        std::string author;                 // 作者
        std::string softwareName;           // 软件名称
        std::string softwareVersion;        // 软件版本
        std::string createdTime;            // 创建时间 (ISO 8601)
        std::string modifiedTime;           // 修改时间 (ISO 8601)
        std::string operatingSystem;        // 操作系统
        std::string description;            // 描述
        PropertyMap customProperties;       // 扩展属性
    };

    ////////////////////////////////////////////////////////////////////
    // ---------------------------- 图层信息 ----------------------------

    struct FILEIO_API LayerInfo
    {
        uint32_t    id = 0;      // 图层 ID
        std::string name;             // 图层名称
        uint32_t    color = 0;      // ARGB 颜色
        bool        visible = true;   // 可见性
        bool        locked = false;  // 锁定
        PropertyMap customProperties; // 扩展属性
    };

    ////////////////////////////////////////////////////////////////////
    // ---------------------------- 硬件信息 ----------------------------

    struct FILEIO_API HardwareInfo
    {
        std::string laserType;           // 激光类型
        std::string controllerModel;     // 控制器型号
        double      maxPower = 0.0;   // 最大功率 (W)
        double      workAreaWidth = 0.0;   // 工作区宽度 (mm)
        double      workAreaHeight = 0.0;   // 工作区高度 (mm)
        PropertyMap customProperties;    // 扩展属性
    };

    ////////////////////////////////////////////////////////////////////
    // ---------------------------- 群组信息 ----------------------------

    struct GroupInfo
    {
        uint64_t id = 0;                    // 群组 ID
        std::string name;                   // 群组名称
        std::vector<uint64_t> entityIds;    // 直接成员图元 ID 列表
        std::vector<uint64_t> subGroupIds;  // 子群组 ID 列表
        uint64_t parentGroupId = 0;         // 父群组 ID（0 表示顶层）

        GroupInfo() = default;
        ~GroupInfo() = default;
        GroupInfo(const GroupInfo&) = default;
        GroupInfo& operator=(const GroupInfo&) = default;
        GroupInfo(GroupInfo&&) = default;
        GroupInfo& operator=(GroupInfo&&) = default;

        bool isEmpty() const
        {
            return entityIds.empty() && subGroupIds.empty();
        }
    };

    ////////////////////////////////////////////////////////////////////
    // ---------------------------- 文档 ----------------------------

    struct FILEIO_API SyDocument
    {
        DocumentMetadata metadata;
        std::vector<LayerInfo> layers;
        std::vector<std::unique_ptr<Eg::SyEntity>> entities;
        std::vector<GroupInfo> groups;                                // 群组信息
        HardwareInfo hardware;

        /// 解析/转换过程中产生的警告信息
        std::vector<std::string> warnings;

        // ---- 构造函数 ----

        SyDocument() = default;
        ~SyDocument() = default;
        SyDocument(const SyDocument&) = delete;
        SyDocument& operator=(const SyDocument&) = delete;
        SyDocument(SyDocument&&) = default;
        SyDocument& operator=(SyDocument&&) = default;

        // ---- 便捷方法 ----

        /// 清空所有数据
        void clear();

        /// 是否为有效文档（至少有一个图元或图层）
        bool isValid() const;
    };

    /// 智能指针别名
    using SyDocumentPtr = std::unique_ptr<SyDocument>;
} // namespace Fio
#pragma once

// FileIO 内部文档数据模型 —— SyDocument 的 PIMPL 内部实现
// 仅 FileIO.dll 内部使用，std 容器可自由使用，不跨 DLL 边界。
//
// C3 收口 (2026-07-31)：原 SyDocument.h 公开的所有 STL 数据成员
// （metadata / layers / entities / groups / hardware / entityLayerMap / warnings）
// 内迁至此。公开头 FileIO/SyDocument.h 不再暴露任何 std 类型，
// 字符串经 const char* 出入，图层/硬件经 POD 视图（SyLayerInfo/SyHardwareInfo）传输。

#include "FileIO/SyDocument.h"
#include "Engine/SyEntity/SyEntity.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Fio
{
    using PropertyMap = std::map<std::string, std::string>;

    struct DocumentMetadata
    {
        int32_t version = 1;           // SY 文件格式版本
        int32_t fileVersion = 1;       // 应用文件版本号
        std::string author;            // 作者
        std::string softwareName;      // 软件名称
        std::string softwareVersion;   // 软件版本
        std::string createdTime;       // 创建时间 (ISO 8601)
        std::string modifiedTime;      // 修改时间 (ISO 8601)
        std::string operatingSystem;   // 操作系统
        std::string description;       // 描述
        PropertyMap customProperties;  // 扩展属性

        // === 环境溯源信息 ===
        std::string serialNumber;      // 设备序列号
        std::string computerUsername;   // 电脑用户名
        std::string osVersion;         // 操作系统版本详情

        // === 长度单位（v2）===
        // 文档声明的长度单位。几何坐标恒为毫米，本字段只记录
        // 「用户以为自己用什么单位」，用于界面显示与导入校验。
        int32_t units = 0;             // 0 = mm，1 = inch（保留未实现）
    };

    struct LayerInfo
    {
        uint32_t id = 0;               // 图层 ID
        std::string name;              // 图层名称
        uint32_t color = 0xFF000000;   // ARGB 颜色
        bool visible = true;           // 可见性
        bool locked = false;           // 锁定
        uint32_t layerType = 0;        // 图层类型 (0=VECTOR, 1=BITMAP)
        bool fill = false;             // 是否填充图层
        uint32_t fillColor = 0xFFFFFFFF; // 填充颜色 ARGB
        PropertyMap customProperties;  // 扩展属性

        // === v2：加工语义（与显示语义严格分离）===
        bool output = true;             // 是否参与加工输出
        uint32_t defaultProcessId = 0; // 默认工艺 ID，0 = 未指定
    };

    /// 工艺对象（内部模型）
    struct ProcessInfo
    {
        uint32_t id = 0;               // 工艺 ID，文档内唯一，>= 1
        std::string name;              // 工艺名称
        int32_t order = 0;             // 工序顺序，小者先执行
        bool output = true;            // 是否输出到设备

        // 加工参数（与 Eg::ProcessParams 一一对应）
        bool paramsEnabled = true;
        double powerPercent = 50.0;
        double speedMmPerSec = 100.0;
        double frequencyHz = 20000.0;
        double pulseWidthUs = 0.0;
        int32_t waveformIndex = 0;
        int32_t passes = 1;
        bool fillEnabled = false;
        double fillIntervalMm = 0.1;
        double fillAngleDeg = 0.0;
        int32_t fillMode = 1;
        double kerfMm = 0.0;
        double rapidSpeedMmPerSec = 0.0;

        /// 显式目标图层（图层反向绑定走 LayerInfo::defaultProcessId，不重复）
        std::vector<uint32_t> targetLayerIds;

        /// 显式目标图元（图元级覆盖）
        std::vector<uint64_t> targetEntityIds;

        PropertyMap customProperties;  // 扩展属性
    };

    struct HardwareInfo
    {
        std::string laserType;         // 激光类型
        std::string controllerModel;   // 控制器型号
        double maxPower = 0.0;         // 最大功率 (W)
        double workAreaWidth = 0.0;    // 工作区宽度 (mm)
        double workAreaHeight = 0.0;   // 工作区高度 (mm)
        PropertyMap customProperties;  // 扩展属性
    };

    struct GroupInfo
    {
        uint64_t id = 0;                    // 群组 ID
        std::string name;                   // 群组名称
        std::vector<uint64_t> entityIds;    // 直接成员图元 ID 列表
        std::vector<uint64_t> subGroupIds;  // 子群组 ID 列表
        uint64_t parentGroupId = 0;         // 父群组 ID（0 表示顶层）

        bool isEmpty() const
        {
            return entityIds.empty() && subGroupIds.empty();
        }
    };

    struct SyDocumentData
    {
        DocumentMetadata metadata;
        std::vector<LayerInfo> layers;
        std::vector<std::unique_ptr<Eg::SyEntity>> entities;

        /// 借出图元（不拥有）。仅用于「序列化输出」路径：调用方（如 NativeWriter）
        /// 已持有图元且承诺在序列化期间存活，无需再深拷贝一份到文档里。
        /// serializeToProto 会先输出 entities，再依次输出 borrowedEntities。
        std::vector<const Eg::SyEntity*> borrowedEntities;

        std::vector<GroupInfo> groups;  // 群组信息
        HardwareInfo hardware;

        // === v2：工艺（加工参数的一等载体）===
        std::vector<ProcessInfo> processes;

        /// 机器档案引用（仅追溯，不参与坐标换算）
        std::string machineProfileRef;
        /// 工程默认工艺 ID（0 = 无）
        uint32_t defaultProcessId = 0;

        /// entityId -> layerId 映射（反序列化时填充）
        std::map<uint64_t, uint32_t> entityLayerMap;

        /// 解析/转换过程中产生的警告信息
        std::vector<std::string> warnings;
    };

    // 内部访问器（friend of SyDocument，仅 DLL 内部使用）
    inline SyDocumentData& syDocumentData(SyDocument& doc)
    {
        if (!doc.m_data)
        {
            doc.m_data = new SyDocumentData();
        }
        return *doc.m_data;
    }

    inline const SyDocumentData& syDocumentData(const SyDocument& doc)
    {
        return *doc.m_data;
    }
}  // namespace Fio

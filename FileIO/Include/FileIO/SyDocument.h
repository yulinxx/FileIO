#pragma once

#include "FileIO/FileIOAPI.h"

#include <cstddef>
#include <cstdint>

namespace Eg
{
    struct SyEntity;
}

namespace Fio
{
    // ============================================================
    // SyDocument —— 文档数据模型（全 PIMPL，跨 DLL 安全）
    //
    // C3 收口 (2026-07-31)：
    //   - 原公开结构体 DocumentMetadata / LayerInfo / HardwareInfo /
    //     GroupInfo / PropertyMap 以及全部 std 容器成员已内迁至内部
    //     数据模型（SyDocumentData.h），本头文件不再暴露任何 std 类型。
    //   - 字符串经 const char* 出入参（返回的内部缓冲区在文档存活且
    //     未被修改期间有效）。
    //   - 图层 / 硬件经 POD 视图（SyLayerInfo / SyHardwareInfo）传输。
    //   - 图元仅暴露借用指针（entityAt），所有权始终由文档持有。
    // ============================================================

    struct SyDocumentData;  // 内部数据模型（前向声明，仅在 DLL 内部定义）

    // ---------------------------- POD 视图（跨 DLL 安全） ----------------------------

    /// 图层信息视图（POD，固定长度缓冲区）
    struct FILEIO_API SyLayerInfo
    {
        uint32_t id = 0;              // 图层 ID
        char name[256] = {};          // 图层名称
        uint32_t color = 0xFF000000;  // ARGB 颜色
        bool visible = true;          // 可见性
        bool locked = false;          // 锁定
    };

    /// 硬件信息视图（POD，固定长度缓冲区）
    struct FILEIO_API SyHardwareInfo
    {
        char laserType[128] = {};        // 激光类型
        char controllerModel[128] = {};  // 控制器型号
        double maxPower = 0.0;           // 最大功率 (W)
        double workAreaWidth = 0.0;      // 工作区宽度 (mm)
        double workAreaHeight = 0.0;     // 工作区高度 (mm)
    };

    ////////////////////////////////////////////////////////////////////
    // ---------------------------- 文档 ----------------------------

    class FILEIO_API SyDocument
    {
    public:
        SyDocument();
        ~SyDocument();
        SyDocument(SyDocument&&) noexcept;
        SyDocument& operator=(SyDocument&&) noexcept;
        SyDocument(const SyDocument&) = delete;
        SyDocument& operator=(const SyDocument&) = delete;

        /// 清空所有数据
        void clear();

        /// 是否为有效文档（至少有一个图元或图层）
        bool isValid() const;

        // ---- 元数据 ----

        int32_t metadataVersion() const;
        void setMetadataVersion(int32_t version);
        int32_t metadataFileVersion() const;
        void setMetadataFileVersion(int32_t fileVersion);
        const char* author() const;
        void setAuthor(const char* author);
        const char* softwareName() const;
        void setSoftwareName(const char* name);
        const char* softwareVersion() const;
        void setSoftwareVersion(const char* version);
        const char* createdTime() const;
        void setCreatedTime(const char* time);
        const char* modifiedTime() const;
        void setModifiedTime(const char* time);
        const char* operatingSystem() const;
        void setOperatingSystem(const char* os);
        const char* description() const;
        void setDescription(const char* desc);

        // ---- 环境溯源信息 ----

        const char* serialNumber() const;
        void setSerialNumber(const char* serial);
        const char* computerUsername() const;
        void setComputerUsername(const char* username);
        const char* osVersion() const;
        void setOsVersion(const char* version);

        // ---- 图层 ----

        size_t layerCount() const;
        /// 拷贝第 index 个图层到 out；越界返回 false
        bool getLayerAt(size_t index, SyLayerInfo& out) const;
        void addLayer(const SyLayerInfo& layer);
        void clearLayers();

        // ---- 图元 ----

        size_t entityCount() const;
        /// 获取第 index 个图元（借用指针，不转移所有权）
        Eg::SyEntity* entityAt(size_t index) const;
        /// 添加图元（接管所有权，传入的指针由文档负责释放）
        void addEntity(Eg::SyEntity* entity);
        void clearEntities();

        // ---- 硬件 ----

        void getHardware(SyHardwareInfo& out) const;
        void setHardware(const SyHardwareInfo& hardware);

    private:
        friend SyDocumentData& syDocumentData(SyDocument& doc);
        friend const SyDocumentData& syDocumentData(const SyDocument& doc);

        SyDocumentData* m_data;
    };
}  // namespace Fio

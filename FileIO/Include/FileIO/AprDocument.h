#pragma once

#include "FileIO/FileIOAPI.h"

#include <cstddef>
#include <cstdint>

namespace Eg
{
    struct AprEntity;
}

namespace Fio
{
    // ============================================================
    // AprDocument —— 文档数据模型（全 PIMPL，跨 DLL 安全）
    //
    // C3 收口 (2026-07-31)：
    //   - 原公开结构体 DocumentMetadata / LayerInfo / HardwareInfo /
    //     GroupInfo / PropertyMap 以及全部 std 容器成员已内迁至内部
    //     数据模型（AprDocumentData.h），本头文件不再暴露任何 std 类型。
    //   - 字符串经 const char* 出入参（返回的内部缓冲区在文档存活且
    //     未被修改期间有效）。
    //   - 图层 / 硬件经 POD 视图（AprLayerInfo / AprHardwareInfo）传输。
    //   - 图元仅暴露借用指针（entityAt），所有权始终由文档持有。
    // ============================================================

    struct AprDocumentData;  // 内部数据模型（前向声明，仅在 DLL 内部定义）

    // ---------------------------- POD 视图（跨 DLL 安全） ----------------------------

    /// 图层信息视图（POD，固定长度缓冲区）
    struct FILEIO_API AprLayerInfo
    {
        uint32_t id = 0;              // 图层 ID
        char name[256] = {};          // 图层名称
        uint32_t color = 0xFF000000;  // ARGB 颜色
        bool visible = true;          // 可见性
        bool locked = false;          // 锁定
        bool output = true;           // 是否参与加工输出（与 visible 严格分离）
        uint32_t defaultProcessId = 0; // 默认工艺 ID（0 = 未指定）
        int32_t hardwareProfileId = 0; // 图层关联的硬件 Profile ID（0 = 默认 profile）
    };

    /// 扩展图层视图（POD，跨 DLL 安全）。
    ///
    /// 为什么另起一个结构体而不是往 `AprLayerInfo` 加字段：`AprLayerInfo` 是冻结的
    /// 跨 DLL POD，加字段会改 sizeof 进而破坏 ABI。原生 .apr 需要把「图层表 +
    /// 硬件 Profile 绑定」完整来回，但又不能动旧结构体，所以用另一个 POD 承载
    /// 全部字段，`addLayerDetail()` / `getLayerDetailAt()` 走它。
    struct FILEIO_API AprLayerDetailInfo
    {
        uint32_t id = 0;
        char name[256] = {};
        uint32_t color = 0xFF000000;      // ARGB，高 8 位为 alpha
        bool visible = true;
        bool locked = false;
        bool output = true;               // 是否参与加工输出（与 visible 分离）
        uint32_t defaultProcessId = 0;   // 0 = 未指定
        uint32_t layerType = 0;          // 0=VECTOR, 1=BITMAP
        bool fill = false;
        uint32_t fillColor = 0xFFFFFFFF;
        int32_t hardwareProfileId = 0;        // 0 = 默认 Profile
        int32_t fillHardwareProfileId = -1;   // -1 = 未设置，回退到 hardwareProfileId
    };

    /// 硬件 Profile 值集视图（POD，跨 DLL 安全）。
    /// 单位与 HardwareProfileManager 一致：frequency=kHz、focusOffset=mm。
    struct FILEIO_API AprHardwareProfileInfo
    {
        int32_t id = -1;
        char name[128] = {};
        double power = 100.0;
        double speed = 1000.0;
        double frequency = 50.0;
        double focusOffset = 0.0;
        int32_t passes = 1;
        // ---- 尾部追加（M1）：割缝与引入线 ----
        double kerfMm = 0.0;            // 单边割缝补偿（mm）
        double leadInLengthMm = 0.0;    // 引入线长度（mm）
        int32_t leadInMode = 0;         // 0=切线, 1=法向, 2=斜线
        double leadInAngleDeg = 0.0;    // 斜线引入角度（度）
        // ---- 尾部追加（M2）：轮廓加工顺序；0 = 文档顺序（既有行为） ----
        int32_t contourOrdering = 0;
    };

    /// 硬件信息视图（POD，固定长度缓冲区）
    struct FILEIO_API AprHardwareInfo
    {
        char laserType[128] = {};        // 激光类型
        char controllerModel[128] = {};  // 控制器型号
        double maxPower = 0.0;           // 最大功率 (W)
        double workAreaWidth = 0.0;      // 工作区宽度 (mm)
        double workAreaHeight = 0.0;     // 工作区高度 (mm)
    };

    /// 工艺参数视图（POD，固定长度缓冲区，跨 DLL 安全）
    ///
    /// 字段与 `Eg::ProcessParams` 一一对应（Engine/Common/Include/Engine/Process/）。
    /// 单位：长度 mm、角度 度、速度 mm/s、频率 Hz、时间 微秒。
    struct FILEIO_API AprProcessParamsInfo
    {
        bool enabled = true;             // 是否参与加工
        double powerPercent = 50.0;      // 出光功率百分比 0-100
        double speedMmPerSec = 100.0;    // 进给速度（必须 > 0）
        double frequencyHz = 20000.0;    // 脉冲频率
        double pulseWidthUs = 0.0;       // 脉宽
        int32_t waveformIndex = 0;       // 波形序号（MOPA）
        int32_t passes = 1;              // 加工遍数
        bool fillEnabled = false;        // 是否填充
        double fillIntervalMm = 0.1;     // 填充行距
        double fillAngleDeg = 0.0;       // 填充角度
        int32_t fillMode = 1;            // 0=轮廓 1=双向 2=光栅
        double kerfMm = 0.0;             // 割缝补偿半径
        double rapidSpeedMmPerSec = 0.0; // 空移速度（0 = 用工程默认值）
        // ---- 尾部追加（M1）：引入线；默认关闭 ----
        bool leadInEnabled = false;
        double leadInLengthMm = 0.0;
        int32_t leadInMode = 0;          // 0=切线, 1=法向, 2=斜线
        double leadInAngleDeg = 0.0;
        double focusOffsetMm = 0.0;      // 焦距补偿（mm）
        // ---- 尾部追加（M2）：轮廓加工顺序；0 = 文档顺序（既有行为） ----
        int32_t contourOrdering = 0;
    };

    /// 工艺对象视图（POD，固定长度缓冲区，跨 DLL 安全）
    struct FILEIO_API AprProcessInfo
    {
        uint32_t id = 0;             // 工艺 ID，文档内唯一，>= 1
        char name[128] = {};         // 工艺名称
        AprProcessParamsInfo params;  // 加工参数
        int32_t order = 0;           // 工序顺序，小者先执行
        bool output = true;          // 是否输出到设备
    };

    /// 群组信息视图（POD，固定长度缓冲区，跨 DLL 安全）
    struct FILEIO_API AprGroupInfo
    {
        uint64_t id = 0;             // 群组 ID
        char name[256] = {};         // 群组名称
        uint64_t parentGroupId = 0;  // 父群组 ID（0 = 顶层）
    };

    ////////////////////////////////////////////////////////////////////
    // ---------------------------- 文档 ----------------------------

    class FILEIO_API AprDocument
    {
    public:
        AprDocument();
        ~AprDocument();
        AprDocument(AprDocument&&) noexcept;
        AprDocument& operator=(AprDocument&&) noexcept;
        AprDocument(const AprDocument&) = delete;
        AprDocument& operator=(const AprDocument&) = delete;

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
        bool getLayerAt(size_t index, AprLayerInfo& out) const;
        void addLayer(const AprLayerInfo& layer);
        void clearLayers();

        /// 完整图层表出入参（含显示属性、加工语义与硬件 Profile 绑定）。
        /// 原生 .apr 的保存/打开走这两个方法；`addLayer()` 只覆盖老 POD 的字段，
        /// 会把 layerType/fill/fillColor/硬件 Profile 绑定留成默认值，不能用于完整往返。
        bool getLayerDetailAt(size_t index, AprLayerDetailInfo& out) const;
        void addLayerDetail(const AprLayerDetailInfo& layer);

        // ---- 硬件 Profile 值集 ----

        size_t hardwareProfileCount() const;
        /// 拷贝第 index 个 Profile；越界返回 false
        bool getHardwareProfileAt(size_t index, AprHardwareProfileInfo& out) const;
        void addHardwareProfile(const AprHardwareProfileInfo& profile);
        void clearHardwareProfiles();

        // ---- 工艺 ----

        size_t processCount() const;
        /// 拷贝第 index 个工艺到 out；越界返回 false
        bool getProcessAt(size_t index, AprProcessInfo& out) const;
        void addProcess(const AprProcessInfo& process);
        /// 给已添加的工艺追加一个显式目标图层（写入 target_layer_ids）。
        /// 单独开一个方法是为了不往冻结的 AprProcessInfo 里塞变长数组。
        /// @return false = 该工艺 ID 不存在
        bool addProcessTargetLayer(uint32_t processId, uint32_t layerId);
        void clearProcesses();

        /// 工艺的显式目标图层（工艺 ID → 图层 ID 列表）
        /// 图层反向绑定走 LayerInfo::defaultProcessId，不在这里重复。
        bool getProcessTargetLayers(uint32_t processId, uint32_t* outLayerIds, size_t capacity, size_t* outCount) const;

        /// 工程级设置：机器档案引用（仅用于追溯「这份图是为哪台机器出的」）
        const char* machineProfileRef() const;
        void setMachineProfileRef(const char* ref);
        /// 工程默认工艺 ID（0 = 无）
        uint32_t defaultProcessId() const;
        void setDefaultProcessId(uint32_t processId);

        /// 文档长度单位（0 = mm，恒等；1 = inch，保留未实现）
        int32_t documentUnits() const;
        void setDocumentUnits(int32_t units);

        // ---- 群组 ----

        size_t groupCount() const;
        /// 拷贝第 index 个群组到 out；越界返回 false
        bool getGroupAt(size_t index, AprGroupInfo& out) const;

        // ---- 图元 ----

        size_t entityCount() const;
        /// 获取第 index 个图元（借用指针，不转移所有权）
        Eg::AprEntity* entityAt(size_t index) const;
        /// 添加图元（接管所有权，传入的指针由文档负责释放）
        void addEntity(Eg::AprEntity* entity);

        /// 添加借出图元（不接管所有权）。仅用于序列化输出路径：调用方须保证
        /// 该指针在文档被序列化的整个期间内存活，且不在此之后被文档访问。
        /// 目的是免去「为了写入而先深拷贝到文档」的开销。
        void addBorrowedEntity(const Eg::AprEntity* entity);

        void clearEntities();

        // ---- 图元-图层/群组映射 ----

        /// 获取图元的图层 ID（反序列化时填充），不存在返回 0
        uint32_t entityLayerId(uint64_t entityId) const;
        /// 获取图元的群组 ID（反序列化时填充），不存在返回 0
        uint64_t entityGroupId(uint64_t entityId) const;

        // ---- 硬件 ----

        void getHardware(AprHardwareInfo& out) const;
        void setHardware(const AprHardwareInfo& hardware);

    private:
        friend AprDocumentData& syDocumentData(AprDocument& doc);
        friend const AprDocumentData& syDocumentData(const AprDocument& doc);

        AprDocumentData* m_data;
    };
}  // namespace Fio

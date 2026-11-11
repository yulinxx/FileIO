#include "FileIO/AprDocument.h"
#include "AprDocumentData.h"
#include "ColorArgbUtils.h"

#include "Engine/AprEntity/AprEntity.h"

#include <cstring>
#include <utility>

namespace Fio
{
    AprDocument::AprDocument()
        : m_data(new AprDocumentData())
    {
    }

    AprDocument::~AprDocument()
    {
        delete m_data;
    }

    AprDocument::AprDocument(AprDocument&& other) noexcept
        : m_data(other.m_data)
    {
        other.m_data = nullptr;
    }

    AprDocument& AprDocument::operator=(AprDocument&& other) noexcept
    {
        if (this != &other)
        {
            delete m_data;
            m_data = other.m_data;
            other.m_data = nullptr;
        }

        return *this;
    }

    void AprDocument::clear()
    {
        if (m_data)
        {
            *m_data = AprDocumentData();
        }
        else
        {
            m_data = new AprDocumentData();
        }
    }

    bool AprDocument::isValid() const
    {
        return m_data && (!m_data->entities.empty() || !m_data->borrowedEntities.empty() || !m_data->layers.empty());
    }

    // ---- 元数据 ----

    int32_t AprDocument::metadataVersion() const
    {
        return m_data ? m_data->metadata.version : 0;
    }

    void AprDocument::setMetadataVersion(int32_t version)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }
        m_data->metadata.version = version;
    }

    int32_t AprDocument::metadataFileVersion() const
    {
        return m_data ? m_data->metadata.fileVersion : 0;
    }

    void AprDocument::setMetadataFileVersion(int32_t fileVersion)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }
        m_data->metadata.fileVersion = fileVersion;
    }

    const char* AprDocument::author() const
    {
        return m_data ? m_data->metadata.author.c_str() : "";
    }

    void AprDocument::setAuthor(const char* author)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }
        m_data->metadata.author = author ? author : "";
    }

    const char* AprDocument::softwareName() const
    {
        return m_data ? m_data->metadata.softwareName.c_str() : "";
    }

    void AprDocument::setSoftwareName(const char* name)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }
        m_data->metadata.softwareName = name ? name : "";
    }

    const char* AprDocument::softwareVersion() const
    {
        return m_data ? m_data->metadata.softwareVersion.c_str() : "";
    }

    void AprDocument::setSoftwareVersion(const char* version)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }
        m_data->metadata.softwareVersion = version ? version : "";
    }

    const char* AprDocument::createdTime() const
    {
        return m_data ? m_data->metadata.createdTime.c_str() : "";
    }

    void AprDocument::setCreatedTime(const char* time)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }
        m_data->metadata.createdTime = time ? time : "";
    }

    const char* AprDocument::modifiedTime() const
    {
        return m_data ? m_data->metadata.modifiedTime.c_str() : "";
    }

    void AprDocument::setModifiedTime(const char* time)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }
        m_data->metadata.modifiedTime = time ? time : "";
    }

    const char* AprDocument::operatingSystem() const
    {
        return m_data ? m_data->metadata.operatingSystem.c_str() : "";
    }

    void AprDocument::setOperatingSystem(const char* os)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }
        m_data->metadata.operatingSystem = os ? os : "";
    }

    const char* AprDocument::description() const
    {
        return m_data ? m_data->metadata.description.c_str() : "";
    }

    void AprDocument::setDescription(const char* desc)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }
        m_data->metadata.description = desc ? desc : "";
    }

    // ---- 环境溯源信息 ----

    const char* AprDocument::serialNumber() const
    {
        return m_data ? m_data->metadata.serialNumber.c_str() : "";
    }

    void AprDocument::setSerialNumber(const char* serial)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }
        m_data->metadata.serialNumber = serial ? serial : "";
    }

    const char* AprDocument::computerUsername() const
    {
        return m_data ? m_data->metadata.computerUsername.c_str() : "";
    }

    void AprDocument::setComputerUsername(const char* username)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }
        m_data->metadata.computerUsername = username ? username : "";
    }

    const char* AprDocument::osVersion() const
    {
        return m_data ? m_data->metadata.osVersion.c_str() : "";
    }

    void AprDocument::setOsVersion(const char* version)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }
        m_data->metadata.osVersion = version ? version : "";
    }

    // ---- 图层 ----

    size_t AprDocument::layerCount() const
    {
        return m_data ? m_data->layers.size() : 0;
    }

    bool AprDocument::getLayerAt(size_t index, AprLayerInfo& out) const
    {
        if (!m_data || index >= m_data->layers.size())
        {
            return false;
        }

        const auto& l = m_data->layers[index];
        out.id = l.id;
        std::strncpy(out.name, l.name.c_str(), sizeof(out.name) - 1);
        out.name[sizeof(out.name) - 1] = '\0';
        out.color = l.color;
        out.visible = l.visible;
        out.locked = l.locked;
        out.output = l.output;
        out.defaultProcessId = l.defaultProcessId;
        return true;
    }

    void AprDocument::addLayer(const AprLayerInfo& layer)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }

        LayerInfo l;
        l.id = layer.id;
        l.name = layer.name;
        l.color = layer.color;
        l.visible = layer.visible;
        l.locked = layer.locked;
        l.output = layer.output;
        l.defaultProcessId = layer.defaultProcessId;
        m_data->layers.push_back(std::move(l));
    }

    void AprDocument::clearLayers()
    {
        if (m_data)
        {
            m_data->layers.clear();
        }
    }

    // ---- 完整图层表 / 硬件 Profile 值集 ----
    //
    // 老 POD AprLayerInfo 是冻结的跨 DLL ABI，不能加字段；完整往返改走
    // AprLayerDetailInfo。读端对老字段沿用同一套兜底语义（alpha=0 → 不透明），
    // 避免新结构体引入第二套默认值。

    namespace
    {
        // 名字带 aprAprDocument 前缀：FileIO 也开 unity build，同一 TU 里
        // 多个匿名命名空间的同名辅助函数会互相重定义。
        void aprAprDocumentCopyLayerName(char (&dst)[256], const std::string& src)
        {
            std::strncpy(dst, src.c_str(), sizeof(dst) - 1);
            dst[sizeof(dst) - 1] = '\0';
        }

        void aprAprDocumentCopyProfileName(char (&dst)[128], const std::string& src)
        {
            std::strncpy(dst, src.c_str(), sizeof(dst) - 1);
            dst[sizeof(dst) - 1] = '\0';
        }
    }  // namespace

    bool AprDocument::getLayerDetailAt(size_t index, AprLayerDetailInfo& out) const
    {
        if (!m_data || index >= m_data->layers.size())
        {
            return false;
        }

        const auto& l = m_data->layers[index];
        out.id = l.id;
        aprAprDocumentCopyLayerName(out.name, l.name);
        out.color = detail::normalizeLegacyArgb(l.color);
        out.visible = l.visible;
        out.locked = l.locked;
        out.output = l.output;
        out.defaultProcessId = l.defaultProcessId;
        out.layerType = l.layerType;
        out.fill = l.fill;
        out.fillColor = detail::normalizeLegacyArgb(l.fillColor);
        out.hardwareProfileId = l.hardwareProfileId;
        out.fillHardwareProfileId = l.fillHardwareProfileId;
        return true;
    }

    void AprDocument::addLayerDetail(const AprLayerDetailInfo& layer)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }

        LayerInfo l;
        l.id = layer.id;
        l.name = layer.name;
        l.color = layer.color;
        l.visible = layer.visible;
        l.locked = layer.locked;
        l.layerType = layer.layerType;
        l.fill = layer.fill;
        l.fillColor = layer.fillColor;
        l.output = layer.output;
        l.defaultProcessId = layer.defaultProcessId;
        l.hardwareProfileId = layer.hardwareProfileId;
        l.fillHardwareProfileId = layer.fillHardwareProfileId;
        m_data->layers.push_back(std::move(l));
    }

    size_t AprDocument::hardwareProfileCount() const
    {
        return m_data ? m_data->hardwareProfiles.size() : 0;
    }

    bool AprDocument::getHardwareProfileAt(size_t index, AprHardwareProfileInfo& out) const
    {
        if (!m_data || index >= m_data->hardwareProfiles.size())
        {
            return false;
        }

        const auto& p = m_data->hardwareProfiles[index];
        out.id = p.id;
        aprAprDocumentCopyProfileName(out.name, p.name);
        out.power = p.power;
        out.speed = p.speed;
        out.frequency = p.frequency;
        out.focusOffset = p.focusOffset;
        out.passes = p.passes;
        out.kerfMm = p.kerfMm;
        out.leadInLengthMm = p.leadInLengthMm;
        out.leadInMode = p.leadInMode;
        out.leadInAngleDeg = p.leadInAngleDeg;
        return true;
    }

    void AprDocument::addHardwareProfile(const AprHardwareProfileInfo& profile)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }

        HardwareProfileInfo p;
        p.id = profile.id;
        p.name = profile.name;
        p.power = profile.power;
        p.speed = profile.speed;
        p.frequency = profile.frequency;
        p.focusOffset = profile.focusOffset;
        p.passes = profile.passes;
        p.kerfMm = profile.kerfMm;
        p.leadInLengthMm = profile.leadInLengthMm;
        p.leadInMode = profile.leadInMode;
        p.leadInAngleDeg = profile.leadInAngleDeg;
        m_data->hardwareProfiles.push_back(std::move(p));
    }

    void AprDocument::clearHardwareProfiles()
    {
        if (m_data)
        {
            m_data->hardwareProfiles.clear();
        }
    }

    // ---- 工艺 ----

    size_t AprDocument::processCount() const
    {
        return m_data ? m_data->processes.size() : 0;
    }

    bool AprDocument::getProcessAt(size_t index, AprProcessInfo& out) const
    {
        if (!m_data || index >= m_data->processes.size())
        {
            return false;
        }

        const auto& p = m_data->processes[index];
        out.id = p.id;
        std::strncpy(out.name, p.name.c_str(), sizeof(out.name) - 1);
        out.name[sizeof(out.name) - 1] = '\0';
        out.order = p.order;
        out.output = p.output;

        AprProcessParamsInfo& params = out.params;
        params.enabled = p.paramsEnabled;
        params.powerPercent = p.powerPercent;
        params.speedMmPerSec = p.speedMmPerSec;
        params.frequencyHz = p.frequencyHz;
        params.pulseWidthUs = p.pulseWidthUs;
        params.waveformIndex = p.waveformIndex;
        params.passes = p.passes;
        params.fillEnabled = p.fillEnabled;
        params.fillIntervalMm = p.fillIntervalMm;
        params.fillAngleDeg = p.fillAngleDeg;
        params.fillMode = p.fillMode;
        params.kerfMm = p.kerfMm;
        params.rapidSpeedMmPerSec = p.rapidSpeedMmPerSec;
        return true;
    }

    void AprDocument::addProcess(const AprProcessInfo& process)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }

        ProcessInfo p;
        p.id = process.id;
        p.name = process.name;
        p.order = process.order;
        p.output = process.output;

        const AprProcessParamsInfo& src = process.params;
        p.paramsEnabled = src.enabled;
        p.powerPercent = src.powerPercent;
        p.speedMmPerSec = src.speedMmPerSec;
        p.frequencyHz = src.frequencyHz;
        p.pulseWidthUs = src.pulseWidthUs;
        p.waveformIndex = src.waveformIndex;
        p.passes = src.passes;
        p.fillEnabled = src.fillEnabled;
        p.fillIntervalMm = src.fillIntervalMm;
        p.fillAngleDeg = src.fillAngleDeg;
        p.fillMode = src.fillMode;
        p.kerfMm = src.kerfMm;
        p.rapidSpeedMmPerSec = src.rapidSpeedMmPerSec;

        m_data->processes.push_back(std::move(p));
    }

    void AprDocument::clearProcesses()
    {
        if (m_data)
        {
            m_data->processes.clear();
        }
    }

    bool AprDocument::getProcessTargetLayers(
        uint32_t processId, uint32_t* outLayerIds, size_t capacity, size_t* outCount) const
    {
        if (!m_data)
        {
            if (outCount)
            {
                *outCount = 0;
            }
            return false;
        }

        for (const auto& p : m_data->processes)
        {
            if (p.id != processId)
            {
                continue;
            }
            const size_t n = p.targetLayerIds.size();
            if (outCount)
            {
                *outCount = n;
            }
            if (outLayerIds && capacity > 0)
            {
                const size_t copy = n < capacity ? n : capacity;
                for (size_t i = 0; i < copy; ++i)
                {
                    outLayerIds[i] = p.targetLayerIds[i];
                }
            }
            return true;
        }

        if (outCount)
        {
            *outCount = 0;
        }
        return false;
    }

    const char* AprDocument::machineProfileRef() const
    {
        return m_data ? m_data->machineProfileRef.c_str() : "";
    }

    void AprDocument::setMachineProfileRef(const char* ref)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }
        m_data->machineProfileRef = ref ? ref : "";
    }

    uint32_t AprDocument::defaultProcessId() const
    {
        return m_data ? m_data->defaultProcessId : 0;
    }

    void AprDocument::setDefaultProcessId(uint32_t processId)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }
        m_data->defaultProcessId = processId;
    }

    int32_t AprDocument::documentUnits() const
    {
        return m_data ? m_data->metadata.units : 0;
    }

    void AprDocument::setDocumentUnits(int32_t units)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }
        m_data->metadata.units = units;
    }

    // ---- 图元 ----

    size_t AprDocument::entityCount() const
    {
        return m_data ? m_data->entities.size() : 0;
    }

    Eg::AprEntity* AprDocument::entityAt(size_t index) const
    {
        if (!m_data || index >= m_data->entities.size())
        {
            return nullptr;
        }
        return m_data->entities[index].get();
    }

    void AprDocument::addEntity(Eg::AprEntity* entity)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }
        m_data->entities.emplace_back(entity);
    }

    void AprDocument::addBorrowedEntity(const Eg::AprEntity* entity)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }
        m_data->borrowedEntities.push_back(entity);
    }

    void AprDocument::clearEntities()
    {
        if (m_data)
        {
            m_data->entities.clear();
            m_data->borrowedEntities.clear();
        }
    }

    // ---- 群组 ----

    size_t AprDocument::groupCount() const
    {
        return m_data ? m_data->groups.size() : 0;
    }

    bool AprDocument::getGroupAt(size_t index, AprGroupInfo& out) const
    {
        if (!m_data || index >= m_data->groups.size())
        {
            return false;
        }

        const auto& g = m_data->groups[index];
        out.id = g.id;
        std::strncpy(out.name, g.name.c_str(), sizeof(out.name) - 1);
        out.name[sizeof(out.name) - 1] = '\0';
        out.parentGroupId = g.parentGroupId;
        return true;
    }

    // ---- 图元-图层/群组映射 ----

    uint32_t AprDocument::entityLayerId(uint64_t entityId) const
    {
        if (!m_data)
        {
            return 0;
        }
        auto it = m_data->entityLayerMap.find(entityId);
        return it != m_data->entityLayerMap.end() ? it->second : 0;
    }

    uint64_t AprDocument::entityGroupId(uint64_t entityId) const
    {
        // 从群组的 entityIds 反向查找
        if (!m_data)
        {
            return 0;
        }
        for (const auto& group : m_data->groups)
        {
            for (uint64_t eid : group.entityIds)
            {
                if (eid == entityId)
                {
                    return group.id;
                }
            }
        }
        return 0;
    }

    // ---- 硬件 ----

    void AprDocument::getHardware(AprHardwareInfo& out) const
    {
        if (!m_data)
        {
            return;
        }

        const auto& h = m_data->hardware;
        std::strncpy(out.laserType, h.laserType.c_str(), sizeof(out.laserType) - 1);
        out.laserType[sizeof(out.laserType) - 1] = '\0';
        std::strncpy(out.controllerModel, h.controllerModel.c_str(), sizeof(out.controllerModel) - 1);
        out.controllerModel[sizeof(out.controllerModel) - 1] = '\0';
        out.maxPower = h.maxPower;
        out.workAreaWidth = h.workAreaWidth;
        out.workAreaHeight = h.workAreaHeight;
    }

    void AprDocument::setHardware(const AprHardwareInfo& hardware)
    {
        if (!m_data)
        {
            m_data = new AprDocumentData();
        }

        auto& h = m_data->hardware;
        h.laserType = hardware.laserType;
        h.controllerModel = hardware.controllerModel;
        h.maxPower = hardware.maxPower;
        h.workAreaWidth = hardware.workAreaWidth;
        h.workAreaHeight = hardware.workAreaHeight;
    }
}  // namespace Fio
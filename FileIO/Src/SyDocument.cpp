#include "FileIO/SyDocument.h"
#include "SyDocumentData.h"

#include "Engine/SyEntity/SyEntity.h"

#include <cstring>

namespace Fio
{
    SyDocument::SyDocument()
        : m_data(new SyDocumentData())
    {
    }

    SyDocument::~SyDocument()
    {
        delete m_data;
    }

    SyDocument::SyDocument(SyDocument&& other) noexcept
        : m_data(other.m_data)
    {
        other.m_data = nullptr;
    }

    SyDocument& SyDocument::operator=(SyDocument&& other) noexcept
    {
        if (this != &other)
        {
            delete m_data;
            m_data = other.m_data;
            other.m_data = nullptr;
        }

        return *this;
    }

    void SyDocument::clear()
    {
        if (m_data)
            *m_data = SyDocumentData();
        else
            m_data = new SyDocumentData();
    }

    bool SyDocument::isValid() const
    {
        return m_data && (!m_data->entities.empty() || !m_data->layers.empty());
    }

    // ---- 元数据 ----

    int32_t SyDocument::metadataVersion() const
    {
        return m_data ? m_data->metadata.version : 0;
    }

    void SyDocument::setMetadataVersion(int32_t version)
    {
        if (!m_data)
            m_data = new SyDocumentData();
        m_data->metadata.version = version;
    }

    int32_t SyDocument::metadataFileVersion() const
    {
        return m_data ? m_data->metadata.fileVersion : 0;
    }

    void SyDocument::setMetadataFileVersion(int32_t fileVersion)
    {
        if (!m_data)
            m_data = new SyDocumentData();
        m_data->metadata.fileVersion = fileVersion;
    }

    const char* SyDocument::author() const
    {
        return m_data ? m_data->metadata.author.c_str() : "";
    }

    void SyDocument::setAuthor(const char* author)
    {
        if (!m_data)
            m_data = new SyDocumentData();
        m_data->metadata.author = author ? author : "";
    }

    const char* SyDocument::softwareName() const
    {
        return m_data ? m_data->metadata.softwareName.c_str() : "";
    }

    void SyDocument::setSoftwareName(const char* name)
    {
        if (!m_data)
            m_data = new SyDocumentData();
        m_data->metadata.softwareName = name ? name : "";
    }

    const char* SyDocument::softwareVersion() const
    {
        return m_data ? m_data->metadata.softwareVersion.c_str() : "";
    }

    void SyDocument::setSoftwareVersion(const char* version)
    {
        if (!m_data)
            m_data = new SyDocumentData();
        m_data->metadata.softwareVersion = version ? version : "";
    }

    const char* SyDocument::createdTime() const
    {
        return m_data ? m_data->metadata.createdTime.c_str() : "";
    }

    void SyDocument::setCreatedTime(const char* time)
    {
        if (!m_data)
            m_data = new SyDocumentData();
        m_data->metadata.createdTime = time ? time : "";
    }

    const char* SyDocument::modifiedTime() const
    {
        return m_data ? m_data->metadata.modifiedTime.c_str() : "";
    }

    void SyDocument::setModifiedTime(const char* time)
    {
        if (!m_data)
            m_data = new SyDocumentData();
        m_data->metadata.modifiedTime = time ? time : "";
    }

    const char* SyDocument::operatingSystem() const
    {
        return m_data ? m_data->metadata.operatingSystem.c_str() : "";
    }

    void SyDocument::setOperatingSystem(const char* os)
    {
        if (!m_data)
            m_data = new SyDocumentData();
        m_data->metadata.operatingSystem = os ? os : "";
    }

    const char* SyDocument::description() const
    {
        return m_data ? m_data->metadata.description.c_str() : "";
    }

    void SyDocument::setDescription(const char* desc)
    {
        if (!m_data)
            m_data = new SyDocumentData();
        m_data->metadata.description = desc ? desc : "";
    }

    // ---- 图层 ----

    size_t SyDocument::layerCount() const
    {
        return m_data ? m_data->layers.size() : 0;
    }

    bool SyDocument::getLayerAt(size_t index, SyLayerInfo& out) const
    {
        if (!m_data || index >= m_data->layers.size())
            return false;

        const auto& l = m_data->layers[index];
        out.id = l.id;
        std::strncpy(out.name, l.name.c_str(), sizeof(out.name) - 1);
        out.name[sizeof(out.name) - 1] = '\0';
        out.color = l.color;
        out.visible = l.visible;
        out.locked = l.locked;
        return true;
    }

    void SyDocument::addLayer(const SyLayerInfo& layer)
    {
        if (!m_data)
            m_data = new SyDocumentData();

        LayerInfo l;
        l.id = layer.id;
        l.name = layer.name;
        l.color = layer.color;
        l.visible = layer.visible;
        l.locked = layer.locked;
        m_data->layers.push_back(std::move(l));
    }

    void SyDocument::clearLayers()
    {
        if (m_data)
            m_data->layers.clear();
    }

    // ---- 图元 ----

    size_t SyDocument::entityCount() const
    {
        return m_data ? m_data->entities.size() : 0;
    }

    Eg::SyEntity* SyDocument::entityAt(size_t index) const
    {
        if (!m_data || index >= m_data->entities.size())
            return nullptr;
        return m_data->entities[index].get();
    }

    void SyDocument::addEntity(Eg::SyEntity* entity)
    {
        if (!m_data)
            m_data = new SyDocumentData();
        m_data->entities.emplace_back(entity);
    }

    void SyDocument::clearEntities()
    {
        if (m_data)
            m_data->entities.clear();
    }

    // ---- 硬件 ----

    void SyDocument::getHardware(SyHardwareInfo& out) const
    {
        if (!m_data)
            return;

        const auto& h = m_data->hardware;
        std::strncpy(out.laserType, h.laserType.c_str(), sizeof(out.laserType) - 1);
        out.laserType[sizeof(out.laserType) - 1] = '\0';
        std::strncpy(out.controllerModel, h.controllerModel.c_str(), sizeof(out.controllerModel) - 1);
        out.controllerModel[sizeof(out.controllerModel) - 1] = '\0';
        out.maxPower = h.maxPower;
        out.workAreaWidth = h.workAreaWidth;
        out.workAreaHeight = h.workAreaHeight;
    }

    void SyDocument::setHardware(const SyHardwareInfo& hardware)
    {
        if (!m_data)
            m_data = new SyDocumentData();

        auto& h = m_data->hardware;
        h.laserType = hardware.laserType;
        h.controllerModel = hardware.controllerModel;
        h.maxPower = hardware.maxPower;
        h.workAreaWidth = hardware.workAreaWidth;
        h.workAreaHeight = hardware.workAreaHeight;
    }
} // namespace Fio
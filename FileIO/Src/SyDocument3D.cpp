#include "FileIO/SyDocument3D.h"
#include "Engine3D/SyEntity/SyMeshEntity.h"

#include <memory>
#include <vector>

namespace Fio
{
    struct SyDocument3DData
    {
        std::vector<std::unique_ptr<Eg::SyMeshEntity>> meshEntities; // 3D 网格图元
    };

    // 内部访问器（friend of SyDocument3D，仅 DLL 内部使用）
    SyDocument3DData& syDocument3DData(SyDocument3D& doc)
    {
        if (!doc.m_data)
            doc.m_data = new SyDocument3DData();
        return *doc.m_data;
    }

    const SyDocument3DData& syDocument3DData(const SyDocument3D& doc)
    {
        return *doc.m_data;
    }

    SyDocument3D::SyDocument3D()
        : m_data(new SyDocument3DData())
    {
    }

    SyDocument3D::~SyDocument3D()
    {
        delete m_data;
    }

    SyDocument3D::SyDocument3D(SyDocument3D&& other) noexcept
        : m_data(other.m_data)
    {
        other.m_data = nullptr;
    }

    SyDocument3D& SyDocument3D::operator=(SyDocument3D&& other) noexcept
    {
        if (this != &other)
        {
            delete m_data;
            m_data = other.m_data;
            other.m_data = nullptr;
        }
        return *this;
    }

    void SyDocument3D::clear()
    {
        if (m_data)
            m_data->meshEntities.clear();
    }

    size_t SyDocument3D::meshEntityCount() const
    {
        return m_data ? m_data->meshEntities.size() : 0;
    }

    Eg::SyMeshEntity* SyDocument3D::meshEntityAt(size_t index) const
    {
        if (!m_data || index >= m_data->meshEntities.size())
            return nullptr;
        return m_data->meshEntities[index].get();
    }

    void SyDocument3D::addMeshEntity(Eg::SyMeshEntity* entity)
    {
        if (!m_data)
            m_data = new SyDocument3DData();
        m_data->meshEntities.emplace_back(entity);
    }
} // namespace Fio
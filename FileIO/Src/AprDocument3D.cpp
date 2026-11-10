#include "FileIO/AprDocument3D.h"
#include "Engine3D/AprEntity/AprMeshEntity.h"

#include <memory>
#include <vector>

namespace Fio
{
    struct AprDocument3DData
    {
        std::vector<std::unique_ptr<Eg::AprMeshEntity>> meshEntities;  // 3D 网格图元
    };

    // 内部访问器（friend of AprDocument3D，仅 DLL 内部使用）
    AprDocument3DData& syDocument3DData(AprDocument3D& doc)
    {
        if (!doc.m_data)
        {
            doc.m_data = new AprDocument3DData();
        }
        return *doc.m_data;
    }

    const AprDocument3DData& syDocument3DData(const AprDocument3D& doc)
    {
        return *doc.m_data;
    }

    AprDocument3D::AprDocument3D()
        : m_data(new AprDocument3DData())
    {
    }

    AprDocument3D::~AprDocument3D()
    {
        delete m_data;
    }

    AprDocument3D::AprDocument3D(AprDocument3D&& other) noexcept
        : m_data(other.m_data)
    {
        other.m_data = nullptr;
    }

    AprDocument3D& AprDocument3D::operator=(AprDocument3D&& other) noexcept
    {
        if (this != &other)
        {
            delete m_data;
            m_data = other.m_data;
            other.m_data = nullptr;
        }
        return *this;
    }

    void AprDocument3D::clear()
    {
        if (m_data)
        {
            m_data->meshEntities.clear();
        }
    }

    size_t AprDocument3D::meshEntityCount() const
    {
        return m_data ? m_data->meshEntities.size() : 0;
    }

    Eg::AprMeshEntity* AprDocument3D::meshEntityAt(size_t index) const
    {
        if (!m_data || index >= m_data->meshEntities.size())
        {
            return nullptr;
        }
        return m_data->meshEntities[index].get();
    }

    void AprDocument3D::addMeshEntity(Eg::AprMeshEntity* entity)
    {
        if (!m_data)
        {
            m_data = new AprDocument3DData();
        }
        m_data->meshEntities.emplace_back(entity);
    }
}  // namespace Fio
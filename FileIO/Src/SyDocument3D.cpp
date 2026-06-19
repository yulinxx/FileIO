#include "FileIO/SyDocument3D.h"

namespace Fio
{
    SyDocument3D::~SyDocument3D() = default;

    void SyDocument3D::clear()
    {
        meshEntities.clear();
    }
} // namespace Fio
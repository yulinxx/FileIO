#include "FileIO/SyDocument.h"
#include "Engine2D/SyEntity/SyEntity.h"

namespace Fio
{
    void SyDocument::clear()
    {
        metadata = DocumentMetadata();
        layers.clear();
        entities.clear();
        groups.clear();
        hardware = HardwareInfo();
        warnings.clear();
    }

    bool SyDocument::isValid() const
    {
        return !entities.empty() || !layers.empty();
    }
} // namespace Fio
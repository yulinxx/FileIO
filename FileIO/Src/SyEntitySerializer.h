#pragma once

#include "FileIO/SyDocument.h"
#include "Engine/SyEntity/SyEntity.h"

#include "SanYiDocument.pb.h"

namespace Fio
{
    namespace SyEntitySerializer
    {
        FILEIO_API void serializeEntity(const Eg::SyEntity& entity, sanyi::proto::EntityData* out);
        FILEIO_API std::unique_ptr<Eg::SyEntity> deserializeEntity(const sanyi::proto::EntityData& protoEntity);
    }  // namespace SyEntitySerializer
}  // namespace Fio

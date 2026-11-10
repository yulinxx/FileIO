#pragma once

#include "FileIO/AprDocument.h"
#include "Engine/AprEntity/AprEntity.h"

#include "AprDocument.pb.h"

namespace Fio
{
    namespace AprEntitySerializer
    {
        FILEIO_API void serializeEntity(const Eg::AprEntity& entity, apr::proto::EntityData* out);
        FILEIO_API std::unique_ptr<Eg::AprEntity> deserializeEntity(const apr::proto::EntityData& protoEntity);
    }  // namespace AprEntitySerializer
}  // namespace Fio

#pragma once

#include "FileIO/SyDocument.h"
#include "Engine/SyEntity/SyEntity.h"

#include "SanYiDocument.pb.h"

namespace Fio
{
    namespace SyEntitySerializer
    {
        void serializeEntity(const Eg::SyEntity& entity, sanyi::proto::EntityData* out);
        std::unique_ptr<Eg::SyEntity> deserializeEntity(const sanyi::proto::EntityData& protoEntity);
    }
}

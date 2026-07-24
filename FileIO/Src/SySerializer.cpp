#include "FileIO/SySerializer.h"
#include "SyEntitySerializer.h"

#include "SanYiDocument.pb.h"
#include "Engine/Layer/SyLayer.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace Fio
{
    namespace
    {
        uint32_t crc32Table[256];
        bool     crc32TableInitialized = false;

        void initCrc32Table()
        {
            if (crc32TableInitialized)
                return;

            const uint32_t polynomial = 0xEDB88320;
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t crc = i;
                for (int j = 0; j < 8; ++j)
                {
                    if (crc & 1)
                        crc = (crc >> 1) ^ polynomial;
                    else
                        crc >>= 1;
                }
                crc32Table[i] = crc;
            }
            crc32TableInitialized = true;
        }

        uint32_t computeCrc32(const uint8_t* data, size_t len)
        {
            initCrc32Table();
            uint32_t crc = 0xFFFFFFFF;
            for (size_t i = 0; i < len; ++i)
            {
                crc = (crc >> 8) ^ crc32Table[(crc ^ data[i]) & 0xFF];
            }
            return crc ^ 0xFFFFFFFF;
        }

        Ut::Vec2d fromProtoVec2(const sanyi::proto::Vec2d& p)
        {
            return Ut::Vec2d(p.x(), p.y());
        }

        void toProtoProperties(const PropertyMap& props,
            sanyi::proto::SanYiDocument& doc,
            google::protobuf::RepeatedPtrField<sanyi::proto::PropertyEntry>* entries)
        {
            (void)doc;
            for (const auto& kv : props)
            {
                auto* entry = entries->Add();
                entry->set_key(kv.first);
                entry->set_value(kv.second);
            }
        }

        PropertyMap fromProtoProperties(
            const google::protobuf::RepeatedPtrField<sanyi::proto::PropertyEntry>& entries)
        {
            PropertyMap props;
            for (const auto& entry : entries)
            {
                props[entry.key()] = entry.value();
            }
            return props;
        }

        std::string currentIsoTime()
        {
            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);

            std::ostringstream oss;
            oss << std::put_time(std::localtime(&time), "%Y-%m-%dT%H:%M:%S");
            return oss.str();
        }

        std::string currentOS()
        {
#ifdef _WIN32
            return "Windows";
#elif defined(__linux__)
            return "Linux";
#elif defined(__APPLE__)
            return "macOS";
#else
            return "Unknown";
#endif
        }

        void ensureMetadataDefaults(DocumentMetadata& meta)
        {
            if (meta.version == 0)
                meta.version = SyFileConst::FILE_VERSION;

            if (meta.softwareName.empty())
                meta.softwareName = "SanYi CAD";

            if (meta.softwareVersion.empty())
                meta.softwareVersion = "1.0.0";

            if (meta.createdTime.empty())
                meta.createdTime = currentIsoTime();

            meta.modifiedTime = currentIsoTime();

            if (meta.operatingSystem.empty())
                meta.operatingSystem = currentOS();
        }

        bool serializeToProto(const SyDocument& doc, std::vector<uint8_t>& out)
        {
            sanyi::proto::SanYiDocument protoDoc;

            {
                auto* meta = protoDoc.mutable_metadata();
                meta->set_version(doc.metadata.version);
                meta->set_file_version(doc.metadata.fileVersion);
                meta->set_author(doc.metadata.author);
                meta->set_software_name(doc.metadata.softwareName);
                meta->set_software_version(doc.metadata.softwareVersion);
                meta->set_created_time(doc.metadata.createdTime);
                meta->set_modified_time(doc.metadata.modifiedTime);
                meta->set_operating_system(doc.metadata.operatingSystem);
                meta->set_description(doc.metadata.description);

                toProtoProperties(doc.metadata.customProperties, protoDoc,
                    meta->mutable_custom_properties());
            }

            for (const auto& layer : doc.layers)
            {
                auto* l = protoDoc.add_layers();
                l->set_id(layer.id);
                l->set_name(layer.name);
                l->set_color(layer.color);
                l->set_visible(layer.visible);
                l->set_locked(layer.locked);
                toProtoProperties(layer.customProperties, protoDoc,
                    l->mutable_custom_properties());
            }

            for (const auto& entity : doc.entities)
            {
                if (!entity)
                    continue;
                auto* e = protoDoc.add_entities();
                SyEntitySerializer::serializeEntity(*entity, e);
            }

            for (const auto& groupInfo : doc.groups)
            {
                if (groupInfo.isEmpty())
                    continue;

                auto* g = protoDoc.add_groups();
                g->set_id(groupInfo.id);
                if (!groupInfo.name.empty())
                    g->set_name(groupInfo.name);
                if (groupInfo.parentGroupId != 0)
                    g->set_parent_group_id(groupInfo.parentGroupId);

                for (uint64_t entityId : groupInfo.entityIds)
                    g->add_entity_ids(entityId);

                for (uint64_t subGroupId : groupInfo.subGroupIds)
                    g->add_sub_group_ids(subGroupId);
            }

            {
                auto* hw = protoDoc.mutable_hardware();
                hw->set_laser_type(doc.hardware.laserType);
                hw->set_controller_model(doc.hardware.controllerModel);
                hw->set_max_power(doc.hardware.maxPower);
                hw->set_work_area_width(doc.hardware.workAreaWidth);
                hw->set_work_area_height(doc.hardware.workAreaHeight);
                toProtoProperties(doc.hardware.customProperties, protoDoc,
                    hw->mutable_custom_properties());
            }

            const size_t byteSize = protoDoc.ByteSizeLong();
            out.resize(byteSize);
            return protoDoc.SerializeToArray(out.data(), static_cast<int>(byteSize));
        }

        bool deserializeFromProto(const std::vector<uint8_t>& data, SyDocument& doc)
        {
            sanyi::proto::SanYiDocument protoDoc;
            if (!protoDoc.ParseFromArray(data.data(), static_cast<int>(data.size())))
            {
                return false;
            }

            if (protoDoc.has_metadata())
            {
                const auto& meta = protoDoc.metadata();
                doc.metadata.version = meta.version();
                doc.metadata.fileVersion = meta.file_version();
                doc.metadata.author = meta.author();
                doc.metadata.softwareName = meta.software_name();
                doc.metadata.softwareVersion = meta.software_version();
                doc.metadata.createdTime = meta.created_time();
                doc.metadata.modifiedTime = meta.modified_time();
                doc.metadata.operatingSystem = meta.operating_system();
                doc.metadata.description = meta.description();
                doc.metadata.customProperties = fromProtoProperties(meta.custom_properties());
            }

            for (int i = 0; i < protoDoc.layers_size(); ++i)
            {
                const auto& l = protoDoc.layers(i);
                LayerInfo layer;
                layer.id = l.id();
                layer.name = l.name();
                layer.color = l.color();
                layer.visible = l.visible();
                layer.locked = l.locked();
                layer.customProperties = fromProtoProperties(l.custom_properties());
                doc.layers.push_back(std::move(layer));
            }

            for (int i = 0; i < protoDoc.entities_size(); ++i)
            {
                const auto& ed = protoDoc.entities(i);
                std::unique_ptr<Eg::SyEntity> entity = SyEntitySerializer::deserializeEntity(ed);

                if (!entity)
                {
                    doc.warnings.push_back(
                        "Unknown entity type: " + std::to_string(static_cast<int>(ed.type())));
                    continue;
                }

                entity->id = static_cast<Eg::EntityId>(ed.id());
                entity->basePoint = fromProtoVec2(ed.base_point());
                entity->bClosed = ed.closed();
                entity->bCCW = ed.ccw();

                if (ed.layer_id() != 0)
                    doc.entityLayerMap[entity->id] = ed.layer_id();

                doc.entities.push_back(std::move(entity));
            }

            for (int i = 0; i < protoDoc.groups_size(); ++i)
            {
                const auto& gd = protoDoc.groups(i);
                GroupInfo info;
                info.id = gd.id();
                info.name = gd.name();
                info.parentGroupId = gd.parent_group_id();

                for (int j = 0; j < gd.entity_ids_size(); ++j)
                    info.entityIds.push_back(gd.entity_ids(j));

                for (int j = 0; j < gd.sub_group_ids_size(); ++j)
                    info.subGroupIds.push_back(gd.sub_group_ids(j));

                doc.groups.push_back(std::move(info));
            }

            if (protoDoc.has_hardware())
            {
                const auto& hw = protoDoc.hardware();
                doc.hardware.laserType = hw.laser_type();
                doc.hardware.controllerModel = hw.controller_model();
                doc.hardware.maxPower = hw.max_power();
                doc.hardware.workAreaWidth = hw.work_area_width();
                doc.hardware.workAreaHeight = hw.work_area_height();
                doc.hardware.customProperties = fromProtoProperties(hw.custom_properties());
            }

            return true;
        }
    }

    SySerializer::SySerializer() = default;
    SySerializer::~SySerializer() = default;

    void SySerializer::setCryptoProvider(CryptoProviderPtr provider)
    {
        m_cryptoProvider = std::move(provider);
    }

    bool SySerializer::hasCrypto() const
    {
        return m_cryptoProvider != nullptr;
    }

    bool SySerializer::writeFileHeader(std::vector<uint8_t>& buffer,
        uint32_t              dataLen,
        uint32_t              flags,
        const char            magic[4])
    {
        buffer.resize(SyFileConst::HEADER_SIZE + dataLen + SyFileConst::FOOTER_SIZE);

        std::memcpy(buffer.data(), magic, 4);

        buffer[4] = static_cast<uint8_t>(SyFileConst::FILE_VERSION & 0xFF);
        buffer[5] = static_cast<uint8_t>((SyFileConst::FILE_VERSION >> 8) & 0xFF);
        buffer[6] = static_cast<uint8_t>((SyFileConst::FILE_VERSION >> 16) & 0xFF);
        buffer[7] = static_cast<uint8_t>((SyFileConst::FILE_VERSION >> 24) & 0xFF);

        buffer[8] = static_cast<uint8_t>(flags & 0xFF);
        buffer[9] = static_cast<uint8_t>((flags >> 8) & 0xFF);
        buffer[10] = static_cast<uint8_t>((flags >> 16) & 0xFF);
        buffer[11] = static_cast<uint8_t>((flags >> 24) & 0xFF);

        buffer[12] = static_cast<uint8_t>(dataLen & 0xFF);
        buffer[13] = static_cast<uint8_t>((dataLen >> 8) & 0xFF);
        buffer[14] = static_cast<uint8_t>((dataLen >> 16) & 0xFF);
        buffer[15] = static_cast<uint8_t>((dataLen >> 24) & 0xFF);

        return true;
    }

    SySerializer::FileHeaderResult SySerializer::readFileHeader(
        const std::vector<uint8_t>& buffer)
    {
        FileHeaderResult result;

        if (buffer.size() < SyFileConst::HEADER_SIZE)
            return result;

        if (std::memcmp(buffer.data(), SyFileConst::MAGIC_SY, 4) != 0)
            return result;

        result.version = static_cast<uint32_t>(buffer[4])
            | (static_cast<uint32_t>(buffer[5]) << 8)
            | (static_cast<uint32_t>(buffer[6]) << 16)
            | (static_cast<uint32_t>(buffer[7]) << 24);

        result.flags = static_cast<uint32_t>(buffer[8])
            | (static_cast<uint32_t>(buffer[9]) << 8)
            | (static_cast<uint32_t>(buffer[10]) << 16)
            | (static_cast<uint32_t>(buffer[11]) << 24);

        result.dataLen = static_cast<uint32_t>(buffer[12])
            | (static_cast<uint32_t>(buffer[13]) << 8)
            | (static_cast<uint32_t>(buffer[14]) << 16)
            | (static_cast<uint32_t>(buffer[15]) << 24);

        result.valid = true;
        return result;
    }

    SerializeResult SySerializer::saveToFile(const std::string& filePath,
        const SyDocument& doc,
        bool               encrypt)
    {
        std::vector<uint8_t> protoData;
        if (!serializeToProto(doc, protoData))
        {
            return SerializeResult::fail("Failed to serialize document to protobuf");
        }

        std::vector<uint8_t> finalData = std::move(protoData);
        uint32_t             flags = 0;

        if (encrypt)
        {
            if (!m_cryptoProvider)
            {
                return SerializeResult::fail(
                    "Encryption requested but no crypto provider set");
            }

            auto encResult = m_cryptoProvider->encrypt(finalData);
            if (!encResult.success)
            {
                return SerializeResult::fail(
                    "Encryption failed: " + encResult.errorMessage);
            }
            finalData = std::move(encResult.data);
            flags |= SyFileConst::FLAG_ENCRYPTED;
        }

        const uint32_t crc = computeCrc32(finalData.data(), finalData.size());

        std::vector<uint8_t> fileBuffer;
        writeFileHeader(fileBuffer, static_cast<uint32_t>(finalData.size()), flags, SyFileConst::MAGIC_SY);
        std::memcpy(fileBuffer.data() + SyFileConst::HEADER_SIZE,
            finalData.data(), finalData.size());

        const size_t crcOffset = SyFileConst::HEADER_SIZE + finalData.size();
        fileBuffer[crcOffset + 0] = static_cast<uint8_t>(crc & 0xFF);
        fileBuffer[crcOffset + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);
        fileBuffer[crcOffset + 2] = static_cast<uint8_t>((crc >> 16) & 0xFF);
        fileBuffer[crcOffset + 3] = static_cast<uint8_t>((crc >> 24) & 0xFF);

        std::ofstream out(std::filesystem::u8path(filePath), std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return SerializeResult::fail("Cannot open file for writing: " + filePath);
        }

        out.write(reinterpret_cast<const char*>(fileBuffer.data()),
            static_cast<std::streamsize>(fileBuffer.size()));

        if (!out.good())
        {
            return SerializeResult::fail("Failed to write file: " + filePath);
        }

        return SerializeResult::ok();
    }

    SerializeResult SySerializer::loadFromFile(const std::string& filePath,
        SyDocument& doc)
    {
        doc.clear();

        std::ifstream in(std::filesystem::u8path(filePath), std::ios::binary | std::ios::ate);
        if (!in)
        {
            return SerializeResult::fail("Cannot open file: " + filePath);
        }

        const auto fileSize = static_cast<size_t>(in.tellg());
        if (fileSize < SyFileConst::HEADER_SIZE + SyFileConst::FOOTER_SIZE)
        {
            return SerializeResult::fail("File too small to be a valid .sy file");
        }

        in.seekg(0);
        std::vector<uint8_t> fileBuffer(fileSize);
        if (!in.read(reinterpret_cast<char*>(fileBuffer.data()),
            static_cast<std::streamsize>(fileSize)))
        {
            return SerializeResult::fail("Failed to read file: " + filePath);
        }

        auto header = readFileHeader(fileBuffer);
        if (!header.valid)
        {
            return SerializeResult::fail("Invalid .sy file header (wrong magic)");
        }

        if (header.version != SyFileConst::FILE_VERSION)
        {
            doc.warnings.push_back(
                "File version mismatch: expected " +
                std::to_string(SyFileConst::FILE_VERSION) + ", got " +
                std::to_string(header.version));
        }

        if (header.dataLen + SyFileConst::HEADER_SIZE + SyFileConst::FOOTER_SIZE > fileSize)
        {
            return SerializeResult::fail("Corrupted file: data size exceeds file size");
        }

        const uint8_t* dataPtr = fileBuffer.data() + SyFileConst::HEADER_SIZE;
        std::vector<uint8_t> data(dataPtr, dataPtr + header.dataLen);

        const uint32_t expectedCrc = computeCrc32(data.data(), data.size());
        const size_t   crcOffset = SyFileConst::HEADER_SIZE + header.dataLen;
        const uint32_t storedCrc = static_cast<uint32_t>(fileBuffer[crcOffset + 0])
            | (static_cast<uint32_t>(fileBuffer[crcOffset + 1]) << 8)
            | (static_cast<uint32_t>(fileBuffer[crcOffset + 2]) << 16)
            | (static_cast<uint32_t>(fileBuffer[crcOffset + 3]) << 24);

        if (expectedCrc != storedCrc)
        {
            return SerializeResult::fail(
                "CRC32 mismatch: file may be corrupted (expected " +
                std::to_string(expectedCrc) + ", got " + std::to_string(storedCrc) + ")");
        }

        if (header.flags & SyFileConst::FLAG_ENCRYPTED)
        {
            if (!m_cryptoProvider)
            {
                return SerializeResult::fail(
                    "File is encrypted but no crypto provider set");
            }

            auto decResult = m_cryptoProvider->decrypt(data);
            if (!decResult.success)
            {
                return SerializeResult::fail(
                    "Decryption failed: " + decResult.errorMessage);
            }
            data = std::move(decResult.data);
        }

        if (!deserializeFromProto(data, doc))
        {
            return SerializeResult::fail("Failed to parse protobuf data");
        }

        return SerializeResult::ok(doc.warnings);
    }

    SerializeResult SySerializer::serializeToMemory(const SyDocument& doc,
        std::vector<uint8_t>& data)
    {
        if (!serializeToProto(doc, data))
        {
            return SerializeResult::fail("Failed to serialize document to protobuf");
        }
        return SerializeResult::ok();
    }

    SerializeResult SySerializer::deserializeFromMemory(const std::vector<uint8_t>& data,
        SyDocument& doc)
    {
        doc.clear();
        if (!deserializeFromProto(data, doc))
        {
            return SerializeResult::fail("Failed to parse protobuf data from memory");
        }
        return SerializeResult::ok(doc.warnings);
    }

    uint32_t SySerializer::fileVersion()
    {
        return SyFileConst::FILE_VERSION;
    }

    bool SySerializer::isValidSyFile(const std::vector<uint8_t>& header)
    {
        if (header.size() < 4)
            return false;
        return std::memcmp(header.data(), SyFileConst::MAGIC_SY, 4) == 0;
    }
}
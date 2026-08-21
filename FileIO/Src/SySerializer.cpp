#include "FileIO/SySerializer.h"
#include "SyEntitySerializer.h"
#include "SyDocumentData.h"

#include "FileIO/SyCryptoProvider.h"
#include "SanYiDocument.pb.h"
#include "Engine/Layer/SyLayer.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace Fio
{
    namespace
    {
        uint32_t crc32Table[256];
        bool crc32TableInitialized = false;

        void initCrc32Table()
        {
            if (crc32TableInitialized)
            {
                return;
            }

            const uint32_t polynomial = 0xEDB88320;
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t crc = i;
                for (int j = 0; j < 8; ++j)
                {
                    if (crc & 1)
                    {
                        crc = (crc >> 1) ^ polynomial;
                    }
                    else
                    {
                        crc >>= 1;
                    }
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

        void toProtoProperties(const PropertyMap& props,
            sanyi::proto::SanYiDocument& /*doc*/,
            google::protobuf::RepeatedPtrField<sanyi::proto::PropertyEntry>* entries)
        {
            for (const auto& kv : props)
            {
                auto* entry = entries->Add();
                entry->set_key(kv.first);
                entry->set_value(kv.second);
            }
        }

        PropertyMap fromProtoProperties(const google::protobuf::RepeatedPtrField<sanyi::proto::PropertyEntry>& entries)
        {
            PropertyMap props;
            for (const auto& entry : entries)
            {
                props[entry.key()] = entry.value();
            }
            return props;
        }

        bool serializeToProto(const SyDocument& doc, std::vector<uint8_t>& out)
        {
            const auto& docData = syDocumentData(doc);
            sanyi::proto::SanYiDocument protoDoc;

            {
                auto* meta = protoDoc.mutable_metadata();
                meta->set_version(docData.metadata.version);
                meta->set_file_version(docData.metadata.fileVersion);
                meta->set_author(docData.metadata.author);
                meta->set_software_name(docData.metadata.softwareName);
                meta->set_software_version(docData.metadata.softwareVersion);
                meta->set_created_time(docData.metadata.createdTime);
                meta->set_modified_time(docData.metadata.modifiedTime);
                meta->set_operating_system(docData.metadata.operatingSystem);
                meta->set_description(docData.metadata.description);

                // === 环境溯源信息 ===
                meta->set_serial_number(docData.metadata.serialNumber);
                meta->set_computer_username(docData.metadata.computerUsername);
                meta->set_os_version(docData.metadata.osVersion);

                toProtoProperties(docData.metadata.customProperties, protoDoc, meta->mutable_custom_properties());
            }

            for (const auto& layer : docData.layers)
            {
                auto* l = protoDoc.add_layers();
                l->set_id(layer.id);
                l->set_name(layer.name);
                l->set_color(layer.color);
                l->set_visible(layer.visible);
                l->set_locked(layer.locked);
                toProtoProperties(layer.customProperties, protoDoc, l->mutable_custom_properties());
            }

            for (const auto& entity : docData.entities)
            {
                if (!entity)
                {
                    continue;
                }
                auto* e = protoDoc.add_entities();
                SyEntitySerializer::serializeEntity(*entity, e);
            }

            for (const auto& groupInfo : docData.groups)
            {
                if (groupInfo.isEmpty())
                {
                    continue;
                }

                auto* g = protoDoc.add_groups();
                g->set_id(groupInfo.id);
                if (!groupInfo.name.empty())
                {
                    g->set_name(groupInfo.name);
                }
                if (groupInfo.parentGroupId != 0)
                {
                    g->set_parent_group_id(groupInfo.parentGroupId);
                }

                for (uint64_t entityId : groupInfo.entityIds)
                {
                    g->add_entity_ids(entityId);
                }

                for (uint64_t subGroupId : groupInfo.subGroupIds)
                {
                    g->add_sub_group_ids(subGroupId);
                }
            }

            {
                auto* hw = protoDoc.mutable_hardware();
                hw->set_laser_type(docData.hardware.laserType);
                hw->set_controller_model(docData.hardware.controllerModel);
                hw->set_max_power(docData.hardware.maxPower);
                hw->set_work_area_width(docData.hardware.workAreaWidth);
                hw->set_work_area_height(docData.hardware.workAreaHeight);
                toProtoProperties(docData.hardware.customProperties, protoDoc, hw->mutable_custom_properties());
            }

            const size_t byteSize = protoDoc.ByteSizeLong();
            out.resize(byteSize);
            return protoDoc.SerializeToArray(out.data(), static_cast<int>(byteSize));
        }

        bool deserializeFromProto(const uint8_t* data, size_t size, SyDocument& doc)
        {
            auto& docData = syDocumentData(doc);
            sanyi::proto::SanYiDocument protoDoc;
            if (!protoDoc.ParseFromArray(data, static_cast<int>(size)))
            {
                return false;
            }

            if (protoDoc.has_metadata())
            {
                const auto& meta = protoDoc.metadata();
                docData.metadata.version = meta.version();
                docData.metadata.fileVersion = meta.file_version();
                docData.metadata.author = meta.author();
                docData.metadata.softwareName = meta.software_name();
                docData.metadata.softwareVersion = meta.software_version();
                docData.metadata.createdTime = meta.created_time();
                docData.metadata.modifiedTime = meta.modified_time();
                docData.metadata.operatingSystem = meta.operating_system();
                docData.metadata.description = meta.description();
                docData.metadata.serialNumber = meta.serial_number();
                docData.metadata.computerUsername = meta.computer_username();
                docData.metadata.osVersion = meta.os_version();
                docData.metadata.customProperties = fromProtoProperties(meta.custom_properties());
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
                docData.layers.push_back(std::move(layer));
            }

            for (int i = 0; i < protoDoc.entities_size(); ++i)
            {
                const auto& ed = protoDoc.entities(i);
                std::unique_ptr<Eg::SyEntity> entity = SyEntitySerializer::deserializeEntity(ed);

                if (!entity)
                {
                    docData.warnings.push_back("Unknown entity type: " + std::to_string(static_cast<int>(ed.type())));
                    continue;
                }

                // 通用属性已由 deserializeEntity 内部设置，此处仅处理图层关联
                if (ed.layer_id() != 0)
                {
                    docData.entityLayerMap[entity->id] = ed.layer_id();
                }

                docData.entities.push_back(std::move(entity));
            }

            for (int i = 0; i < protoDoc.groups_size(); ++i)
            {
                const auto& gd = protoDoc.groups(i);
                GroupInfo info;
                info.id = gd.id();
                info.name = gd.name();
                info.parentGroupId = gd.parent_group_id();

                for (int j = 0; j < gd.entity_ids_size(); ++j)
                {
                    info.entityIds.push_back(gd.entity_ids(j));
                }

                for (int j = 0; j < gd.sub_group_ids_size(); ++j)
                {
                    info.subGroupIds.push_back(gd.sub_group_ids(j));
                }

                docData.groups.push_back(std::move(info));
            }

            if (protoDoc.has_hardware())
            {
                const auto& hw = protoDoc.hardware();
                docData.hardware.laserType = hw.laser_type();
                docData.hardware.controllerModel = hw.controller_model();
                docData.hardware.maxPower = hw.max_power();
                docData.hardware.workAreaWidth = hw.work_area_width();
                docData.hardware.workAreaHeight = hw.work_area_height();
                docData.hardware.customProperties = fromProtoProperties(hw.custom_properties());
            }

            return true;
        }

        /// 将 docData.warnings 逐条通过回调输出（回调为空时忽略）
        void emitWarnings(const SyDocument& doc, SerializeWarningCallback warningCb, void* warningCtx)
        {
            const auto& docData = syDocumentData(doc);
            if (!warningCb)
            {
                return;
            }
            for (const auto& warn : docData.warnings)
            {
                warningCb(warn.c_str(), warningCtx);
            }
        }
    }  // namespace

    struct SySerializer::Impl
    {
    public:
        Impl() = default;

        ~Impl()
        {
            delete m_cryptoProvider;
        }

        Impl(const Impl&) = delete;
        Impl& operator=(const Impl&) = delete;

        ISyCryptoProvider* m_cryptoProvider = nullptr;
    };

    SySerializer::SySerializer()
        : m_impl(new Impl())
    {
    }

    SySerializer::~SySerializer()
    {
        delete m_impl;
    }

    SySerializer::SySerializer(SySerializer&& other) noexcept
        : m_impl(other.m_impl)
    {
        other.m_impl = nullptr;
    }

    SySerializer& SySerializer::operator=(SySerializer&& other) noexcept
    {
        if (this != &other)
        {
            delete m_impl;
            m_impl = other.m_impl;
            other.m_impl = nullptr;
        }
        return *this;
    }

    void SySerializer::setCryptoProvider(ISyCryptoProvider* provider)
    {
        delete m_impl->m_cryptoProvider;
        m_impl->m_cryptoProvider = provider;
    }

    bool SySerializer::hasCrypto() const
    {
        return m_impl && m_impl->m_cryptoProvider != nullptr;
    }

    SerializeResult SySerializer::saveToFile(
        const char* filePath, const SyDocument& doc, bool encrypt, SerializeWarningCallback warningCb, void* warningCtx)
    {
        // 原有接口默认使用 2D 格式（保持向后兼容）
        return saveToFile(filePath, doc, encrypt, FileFormat::Native, warningCb, warningCtx);
    }

    SerializeResult SySerializer::saveToFile(const char* filePath,
        const SyDocument& doc,
        bool encrypt,
        FileFormat fmt,
        SerializeWarningCallback /*warningCb*/,
        void* /*warningCtx*/)
    {
        if (!filePath || !*filePath)
        {
            return SerializeResult::fail("Empty file path");
        }

        // 根据目标格式选择魔数
        const char* magic = (fmt == FileFormat::Native3D) ? SyFileConst::MAGIC_SYX : SyFileConst::MAGIC_SY;

        std::vector<uint8_t> protoData;
        if (!serializeToProto(doc, protoData))
        {
            return SerializeResult::fail("Failed to serialize document to protobuf");
        }

        std::vector<uint8_t> finalData = std::move(protoData);
        uint32_t flags = 0;

        if (encrypt)
        {
            if (!m_impl->m_cryptoProvider)
            {
                return SerializeResult::fail("Encryption requested but no crypto provider set");
            }

            auto encResult = m_impl->m_cryptoProvider->encrypt(finalData.data(), finalData.size());
            if (!encResult.success)
            {
                return SerializeResult::fail(std::string("Encryption failed: ").append(encResult.errorMessage).c_str());
            }
            finalData.assign(encResult.data, encResult.data + encResult.dataSize);
            m_impl->m_cryptoProvider->freeCryptoData(encResult.data);
            flags |= SyFileConst::FLAG_ENCRYPTED;
        }

        const uint32_t crc = computeCrc32(finalData.data(), finalData.size());

        std::vector<uint8_t> fileBuffer;
        fileBuffer.resize(SyFileConst::HEADER_SIZE + finalData.size() + SyFileConst::FOOTER_SIZE);

        std::memcpy(fileBuffer.data(), magic, 4);

        fileBuffer[4] = static_cast<uint8_t>(SyFileConst::FILE_VERSION & 0xFF);
        fileBuffer[5] = static_cast<uint8_t>((SyFileConst::FILE_VERSION >> 8) & 0xFF);
        fileBuffer[6] = static_cast<uint8_t>((SyFileConst::FILE_VERSION >> 16) & 0xFF);
        fileBuffer[7] = static_cast<uint8_t>((SyFileConst::FILE_VERSION >> 24) & 0xFF);

        fileBuffer[8] = static_cast<uint8_t>(flags & 0xFF);
        fileBuffer[9] = static_cast<uint8_t>((flags >> 8) & 0xFF);
        fileBuffer[10] = static_cast<uint8_t>((flags >> 16) & 0xFF);
        fileBuffer[11] = static_cast<uint8_t>((flags >> 24) & 0xFF);

        fileBuffer[12] = static_cast<uint8_t>(finalData.size() & 0xFF);
        fileBuffer[13] = static_cast<uint8_t>((finalData.size() >> 8) & 0xFF);
        fileBuffer[14] = static_cast<uint8_t>((finalData.size() >> 16) & 0xFF);
        fileBuffer[15] = static_cast<uint8_t>((finalData.size() >> 24) & 0xFF);

        std::memcpy(fileBuffer.data() + SyFileConst::HEADER_SIZE, finalData.data(), finalData.size());

        const size_t crcOffset = SyFileConst::HEADER_SIZE + finalData.size();
        fileBuffer[crcOffset + 0] = static_cast<uint8_t>(crc & 0xFF);
        fileBuffer[crcOffset + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);
        fileBuffer[crcOffset + 2] = static_cast<uint8_t>((crc >> 16) & 0xFF);
        fileBuffer[crcOffset + 3] = static_cast<uint8_t>((crc >> 24) & 0xFF);

        std::ofstream out(std::filesystem::u8path(filePath), std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return SerializeResult::fail(std::string("Cannot open file for writing: ").append(filePath).c_str());
        }

        out.write(reinterpret_cast<const char*>(fileBuffer.data()), static_cast<std::streamsize>(fileBuffer.size()));

        if (!out.good())
        {
            return SerializeResult::fail(std::string("Failed to write file: ").append(filePath).c_str());
        }

        return SerializeResult::ok();
    }

    SerializeResult SySerializer::loadFromFile(
        const char* filePath, SyDocument& doc, SerializeWarningCallback warningCb, void* warningCtx)
    {
        doc.clear();

        if (!filePath || !*filePath)
        {
            return SerializeResult::fail("Empty file path");
        }

        std::ifstream in(std::filesystem::u8path(filePath), std::ios::binary | std::ios::ate);
        if (!in)
        {
            return SerializeResult::fail(std::string("Cannot open file: ").append(filePath).c_str());
        }

        const auto fileSize = static_cast<size_t>(in.tellg());
        if (fileSize < SyFileConst::HEADER_SIZE + SyFileConst::FOOTER_SIZE)
        {
            return SerializeResult::fail("File too small to be a valid .sy file");
        }

        in.seekg(0);
        std::vector<uint8_t> fileBuffer(fileSize);
        if (!in.read(reinterpret_cast<char*>(fileBuffer.data()), static_cast<std::streamsize>(fileSize)))
        {
            return SerializeResult::fail(std::string("Failed to read file: ").append(filePath).c_str());
        }

        bool headerValid = false;
        uint32_t version = 0;
        uint32_t flags = 0;
        uint32_t dataLen = 0;

        if (fileBuffer.size() >= SyFileConst::HEADER_SIZE &&
            std::memcmp(fileBuffer.data(), SyFileConst::MAGIC_SY, 4) == 0)
        {
            headerValid = true;
            version = static_cast<uint32_t>(fileBuffer[4]) | (static_cast<uint32_t>(fileBuffer[5]) << 8) |
                (static_cast<uint32_t>(fileBuffer[6]) << 16) | (static_cast<uint32_t>(fileBuffer[7]) << 24);
            flags = static_cast<uint32_t>(fileBuffer[8]) | (static_cast<uint32_t>(fileBuffer[9]) << 8) |
                (static_cast<uint32_t>(fileBuffer[10]) << 16) | (static_cast<uint32_t>(fileBuffer[11]) << 24);
            dataLen = static_cast<uint32_t>(fileBuffer[12]) | (static_cast<uint32_t>(fileBuffer[13]) << 8) |
                (static_cast<uint32_t>(fileBuffer[14]) << 16) | (static_cast<uint32_t>(fileBuffer[15]) << 24);
        }

        if (!headerValid)
        {
            return SerializeResult::fail("Invalid .sy file header (wrong magic)");
        }

        if (version != SyFileConst::FILE_VERSION)
        {
            syDocumentData(doc).warnings.push_back("File version mismatch: expected " +
                std::to_string(SyFileConst::FILE_VERSION) + ", got " + std::to_string(version));
        }

        if (dataLen + SyFileConst::HEADER_SIZE + SyFileConst::FOOTER_SIZE > fileSize)
        {
            return SerializeResult::fail("Corrupted file: data size exceeds file size");
        }

        const uint8_t* dataPtr = fileBuffer.data() + SyFileConst::HEADER_SIZE;

        const uint32_t expectedCrc = computeCrc32(dataPtr, dataLen);
        const size_t crcOffset = SyFileConst::HEADER_SIZE + dataLen;
        const uint32_t storedCrc = static_cast<uint32_t>(fileBuffer[crcOffset + 0]) |
            (static_cast<uint32_t>(fileBuffer[crcOffset + 1]) << 8) |
            (static_cast<uint32_t>(fileBuffer[crcOffset + 2]) << 16) |
            (static_cast<uint32_t>(fileBuffer[crcOffset + 3]) << 24);

        if (expectedCrc != storedCrc)
        {
            return SerializeResult::fail(std::string("CRC32 mismatch: file may be corrupted (expected ")
                    .append(std::to_string(expectedCrc))
                    .append(", got ")
                    .append(std::to_string(storedCrc))
                    .append(")")
                    .c_str());
        }

        std::vector<uint8_t> data(dataPtr, dataPtr + dataLen);

        if (flags & SyFileConst::FLAG_ENCRYPTED)
        {
            if (!m_impl->m_cryptoProvider)
            {
                return SerializeResult::fail("File is encrypted but no crypto provider set");
            }

            auto decResult = m_impl->m_cryptoProvider->decrypt(data.data(), data.size());
            if (!decResult.success)
            {
                return SerializeResult::fail(std::string("Decryption failed: ").append(decResult.errorMessage).c_str());
            }
            data.assign(decResult.data, decResult.data + decResult.dataSize);
            m_impl->m_cryptoProvider->freeCryptoData(decResult.data);
        }

        if (!deserializeFromProto(data.data(), data.size(), doc))
        {
            return SerializeResult::fail("Failed to parse protobuf data");
        }

        emitWarnings(doc, warningCb, warningCtx);
        return SerializeResult::ok();
    }

    SerializeResult SySerializer::serializeToMemory(
        const SyDocument& doc, BinaryBlobOut* out, SerializeWarningCallback /*warningCb*/, void* /*warningCtx*/)
    {
        std::vector<uint8_t> data;
        if (!serializeToProto(doc, data))
        {
            return SerializeResult::fail("Failed to serialize document to protobuf");
        }

        if (out)
        {
            out->written = data.size();
            if (out->data && out->capacity > 0)
            {
                const size_t toCopy = (data.size() < out->capacity) ? data.size() : out->capacity;
                std::memcpy(out->data, data.data(), toCopy);
            }
        }
        return SerializeResult::ok();
    }

    SerializeResult SySerializer::deserializeFromMemory(
        BinaryBlob in, SyDocument& doc, SerializeWarningCallback warningCb, void* warningCtx)
    {
        doc.clear();
        if (!deserializeFromProto(in.data, in.size, doc))
        {
            return SerializeResult::fail("Failed to parse protobuf data from memory");
        }
        emitWarnings(doc, warningCb, warningCtx);
        return SerializeResult::ok();
    }

    uint32_t SySerializer::fileVersion()
    {
        return SyFileConst::FILE_VERSION;
    }

    bool SySerializer::isValidSyFile(const uint8_t* header, size_t headerSize)
    {
        if (!header || headerSize < 4)
        {
            return false;
        }
        return std::memcmp(header, SyFileConst::MAGIC_SY, 4) == 0;
    }

    bool SySerializer::isValidSyxFile(const uint8_t* header, size_t headerSize)
    {
        if (!header || headerSize < 4)
        {
            return false;
        }
        return std::memcmp(header, SyFileConst::MAGIC_SYX, 4) == 0;
    }
}  // namespace Fio
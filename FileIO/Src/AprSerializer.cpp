#include "FileIO/AprSerializer.h"
#include "AprEntitySerializer.h"
#include "AprDocumentData.h"
#include "ColorArgbUtils.h"
#include "DurableIO.h"

#include "FileIO/AprCryptoProvider.h"
#include "AprDocument.pb.h"
#include "Engine/Layer/AprLayer.h"
#include "Log/AprLogger.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <future>
#include <sstream>
#include <filesystem>
#include <mutex>
#include <system_error>

namespace Fio
{
    // 显式前向声明，确保 IAprCryptoProvider 在此作用域内可见
    class IAprCryptoProvider;

    namespace
    {
        // std::call_once 保证线程安全的一次性初始化（C++11 起）
        const uint32_t* getCrc32Table()
        {
            static uint32_t crc32Table[256];
            static std::once_flag flag;
            std::call_once(flag, [] {
                const uint32_t polynomial = 0xEDB88320;
                for (uint32_t i = 0; i < 256; ++i)
                {
                    uint32_t crc = i;
                    for (int j = 0; j < 8; ++j)
                    {
                        crc = (crc & 1) ? ((crc >> 1) ^ polynomial) : (crc >> 1);
                    }
                    crc32Table[i] = crc;
                }
            });
            return crc32Table;
        }

        uint32_t computeCrc32(const uint8_t* data, size_t len)
        {
            const uint32_t* table = getCrc32Table();
            uint32_t crc = 0xFFFFFFFF;
            for (size_t i = 0; i < len; ++i)
            {
                crc = (crc >> 8) ^ table[(crc ^ data[i]) & 0xFF];
            }
            return crc ^ 0xFFFFFFFF;
        }

        // 颜色 ARGB 编解码由 Internal/ColorArgbUtils.h 统一提供：
        // unity build 会把多个 cpp 合并进同一翻译单元，各写一份会重复定义。

        void toProtoProperties(const PropertyMap& props,
            apr::proto::AprDocument& /*doc*/,
            google::protobuf::RepeatedPtrField<apr::proto::PropertyEntry>* entries)
        {
            for (const auto& kv : props)
            {
                auto* entry = entries->Add();
                entry->set_key(kv.first);
                entry->set_value(kv.second);
            }
        }

        PropertyMap fromProtoProperties(const google::protobuf::RepeatedPtrField<apr::proto::PropertyEntry>& entries)
        {
            PropertyMap props;
            for (const auto& entry : entries)
            {
                props[entry.key()] = entry.value();
            }
            return props;
        }

        bool serializeToProto(const AprDocument& doc, std::vector<uint8_t>& out, std::string* errorMessage = nullptr)
        {
            const auto& docData = syDocumentData(doc);
            apr::proto::AprDocument protoDoc;

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

                // === 长度单位（v2）===
                meta->set_units(docData.metadata.units);

                toProtoProperties(docData.metadata.customProperties, protoDoc, meta->mutable_custom_properties());
            }

            if (!docData.layers.empty())
            {
                protoDoc.mutable_layers()->Reserve(static_cast<int>(docData.layers.size()));
            }
            for (const auto& layer : docData.layers)
            {
                auto* l = protoDoc.add_layers();
                l->set_id(layer.id);
                l->set_name(layer.name);
                l->set_color(layer.color);
                l->set_visible(layer.visible);
                l->set_locked(layer.locked);
                l->set_layer_type(layer.layerType);
                l->set_fill(layer.fill);
                l->set_fill_color(layer.fillColor);
                // v2：加工语义。output 用 optional —— 字段缺失（v1 文件）时读端
                // 必须回退为 true，否则打开旧文件后「开始加工」什么都不发生。
                l->set_output(layer.output);
                l->set_default_process_id(layer.defaultProcessId);
                // proto 里早就声明了这两个字段，旧写路径却从未赋过值：
                // 保存后图层绑定的硬件 Profile 全部退回默认，用户设置的功率/速度丢了。
                l->set_hardware_profile_id(layer.hardwareProfileId);
                // optional：-1 表示「未设置」，不写该字段，读端按 has_* 回退到 -1，
                // 与 v1 文件（无此字段）的行为一致。
                if (layer.fillHardwareProfileId >= 0)
                {
                    l->set_fill_hardware_profile_id(layer.fillHardwareProfileId);
                }
                toProtoProperties(layer.customProperties, protoDoc, l->mutable_custom_properties());
            }

            // === v2：工艺（加工参数的一等载体）===
            if (!docData.processes.empty())
            {
                protoDoc.mutable_processes()->Reserve(static_cast<int>(docData.processes.size()));
            }
            for (const auto& p : docData.processes)
            {
                auto* pp = protoDoc.add_processes();
                pp->set_id(p.id);
                pp->set_name(p.name);
                pp->set_order(p.order);
                pp->set_output(p.output);

                auto* params = pp->mutable_params();
                params->set_name(p.name);
                params->set_enabled(p.paramsEnabled);
                params->set_power_percent(p.powerPercent);
                params->set_speed_mm_per_sec(p.speedMmPerSec);
                params->set_frequency_hz(p.frequencyHz);
                params->set_pulse_width_us(p.pulseWidthUs);
                params->set_waveform_index(p.waveformIndex);
                params->set_passes(p.passes);
                params->set_fill_enabled(p.fillEnabled);
                params->set_fill_interval_mm(p.fillIntervalMm);
                params->set_fill_angle_deg(p.fillAngleDeg);
                params->set_fill_mode(p.fillMode);
                params->set_kerf_mm(p.kerfMm);
                params->set_rapid_speed_mm_per_sec(p.rapidSpeedMmPerSec);
                params->set_lead_in_enabled(p.leadInEnabled);
                params->set_lead_in_length_mm(p.leadInLengthMm);
                params->set_lead_in_mode(p.leadInMode);
                params->set_lead_in_angle_deg(p.leadInAngleDeg);
                params->set_focus_offset_mm(p.focusOffsetMm);

                for (uint32_t layerId : p.targetLayerIds)
                {
                    pp->add_target_layer_ids(layerId);
                }
                for (uint64_t entityId : p.targetEntityIds)
                {
                    pp->add_target_entity_ids(entityId);
                }
                toProtoProperties(p.customProperties, protoDoc, pp->mutable_custom_properties());
            }

            // === v2：工程级设置 ===
            {
                auto* ps = protoDoc.mutable_project_settings();
                ps->set_machine_profile_ref(docData.machineProfileRef);
                ps->set_default_process_id(docData.defaultProcessId);
            }

            // === v2 补齐：硬件 Profile 值集 ===
            // LayerData.hardware_profile_id / fill_hardware_profile_id 只是引用，
            // 没有这组值就等于没存功率/速度/频率。旧读端忽略未知字段，安全退化。
            if (!docData.hardwareProfiles.empty())
            {
                protoDoc.mutable_hardware_profiles()->Reserve(static_cast<int>(docData.hardwareProfiles.size()));
            }
            for (const auto& hp : docData.hardwareProfiles)
            {
                auto* p = protoDoc.add_hardware_profiles();
                p->set_id(hp.id);
                p->set_name(hp.name);
                p->set_power(hp.power);
                p->set_speed(hp.speed);
                p->set_frequency(hp.frequency);
                p->set_focus_offset(hp.focusOffset);
                p->set_passes(hp.passes);
                p->set_kerf_mm(hp.kerfMm);
                p->set_lead_in_length_mm(hp.leadInLengthMm);
                p->set_lead_in_mode(hp.leadInMode);
                p->set_lead_in_angle_deg(hp.leadInAngleDeg);
            }

            // 预先 Reserve，避免 add_entities() 反复扩容搬移整个 repeated message 数组。
            const size_t totalEntities = docData.entities.size() + docData.borrowedEntities.size();
            if (totalEntities > 0)
            {
                protoDoc.mutable_entities()->Reserve(static_cast<int>(totalEntities));
            }

            for (const auto& entity : docData.entities)
            {
                if (!entity)
                {
                    continue;
                }
                auto* e = protoDoc.add_entities();
                AprEntitySerializer::serializeEntity(*entity, e);
            }

            // 借出图元（不拥有）：直接序列化，不深拷贝
            for (const Eg::AprEntity* entity : docData.borrowedEntities)
            {
                if (!entity)
                {
                    continue;
                }
                auto* e = protoDoc.add_entities();
                AprEntitySerializer::serializeEntity(*entity, e);
            }

            if (!docData.groups.empty())
            {
                protoDoc.mutable_groups()->Reserve(static_cast<int>(docData.groups.size()));
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
            // protobuf 的 SerializeToArray 只接受 int 长度，文件头的 payload 长度也是 uint32。
            // 超过 2 GiB 时序列化结果会溢出甚至被截断，必须在分配/写入之前拒绝。
            if (byteSize > static_cast<size_t>(INT32_MAX))
            {
                if (errorMessage)
                {
                    *errorMessage = "Document is too large to serialize (payload exceeds the 2 GiB limit)";
                }
                return false;
            }
            out.resize(byteSize);
            return protoDoc.SerializeToArray(out.data(), static_cast<int>(byteSize));
        }

        bool deserializeFromProto(const uint8_t* data, size_t size, AprDocument& doc, std::string* errorMessage = nullptr)
        {
            auto& docData = syDocumentData(doc);
            apr::proto::AprDocument protoDoc;
            if (size > static_cast<size_t>(INT32_MAX) || !protoDoc.ParseFromArray(data, static_cast<int>(size)))
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to parse protobuf data";
                }
                return false;
            }

            // 英制文档（units=1）尚未实现：几何恒为 mm，若按 mm 静默读入整图会小 25.4 倍。
            // 协议注释要求读端显式报错，而不是降级读取。
            if (protoDoc.has_metadata() && protoDoc.metadata().units() != 0)
            {
                if (errorMessage)
                {
                    *errorMessage = "Unsupported document units: imperial (inch) documents are not supported";
                }
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
                docData.metadata.units = meta.units();
                docData.metadata.customProperties = fromProtoProperties(meta.custom_properties());
            }

            for (int i = 0; i < protoDoc.layers_size(); ++i)
            {
                const auto& l = protoDoc.layers(i);
                LayerInfo layer;
                layer.id = l.id();
                layer.name = l.name();
                // 颜色高 8 位为 alpha；历史缺省值（0）按不透明兜底
                layer.color = detail::normalizeLegacyArgb(l.color());
                layer.visible = l.visible();
                layer.locked = l.locked();
                layer.layerType = l.layer_type();
                layer.fill = l.fill();
                layer.fillColor = detail::normalizeLegacyArgb(l.fill_color());
                layer.customProperties = fromProtoProperties(l.custom_properties());
                // v2：加工语义。output 是 optional —— v1 文件里没有这个字段，
                // proto3 的 has_output() 为 false。**必须回退为 true**：
                // 若按 proto3 默认 false 处理，打开旧文件后所有图层都不参与加工，
                // 表现是「点开始加工什么都没发生」，且没有任何报错。
                layer.output = l.has_output() ? l.output() : true;
                layer.defaultProcessId = l.default_process_id();
                layer.hardwareProfileId = l.hardware_profile_id();
                // optional 字段：旧文件没有时回退 -1（= 用 hardwareProfileId），
                // 不能按 proto3 默认 0 处理，否则会误指到 id=0 的默认 Profile。
                layer.fillHardwareProfileId = l.has_fill_hardware_profile_id()
                    ? l.fill_hardware_profile_id() : -1;
                docData.layers.push_back(std::move(layer));
            }

            // === v2：工艺 ===
            for (int i = 0; i < protoDoc.processes_size(); ++i)
            {
                const auto& pd = protoDoc.processes(i);
                ProcessInfo p;
                p.id = pd.id();
                p.name = pd.name();
                p.order = pd.order();
                p.output = pd.output();

                if (pd.has_params())
                {
                    const auto& pp = pd.params();
                    p.paramsEnabled = pp.enabled();
                    p.powerPercent = pp.power_percent();
                    p.speedMmPerSec = pp.speed_mm_per_sec();
                    p.frequencyHz = pp.frequency_hz();
                    p.pulseWidthUs = pp.pulse_width_us();
                    p.waveformIndex = pp.waveform_index();
                    p.passes = pp.passes();
                    p.fillEnabled = pp.fill_enabled();
                    p.fillIntervalMm = pp.fill_interval_mm();
                    p.fillAngleDeg = pp.fill_angle_deg();
                    p.fillMode = pp.fill_mode();
                    p.kerfMm = pp.kerf_mm();
                    p.rapidSpeedMmPerSec = pp.rapid_speed_mm_per_sec();
                    // 旧文件没有这 4 个字段 → proto3 默认值即「关闭/0」，
                    // 与 ProcessParams 的默认值一致，不会改变既有档案的刀路
                    p.leadInEnabled = pp.lead_in_enabled();
                    p.leadInLengthMm = pp.lead_in_length_mm();
                    p.leadInMode = pp.lead_in_mode();
                    p.leadInAngleDeg = pp.lead_in_angle_deg();
                    // 旧文件没有该字段 → proto3 默认 0.0，即"不补偿"
                    p.focusOffsetMm = pp.focus_offset_mm();
                }

                for (int j = 0; j < pd.target_layer_ids_size(); ++j)
                {
                    p.targetLayerIds.push_back(pd.target_layer_ids(j));
                }
                for (int j = 0; j < pd.target_entity_ids_size(); ++j)
                {
                    p.targetEntityIds.push_back(pd.target_entity_ids(j));
                }
                p.customProperties = fromProtoProperties(pd.custom_properties());

                docData.processes.push_back(std::move(p));
            }

            // === v2：工程级设置 ===
            if (protoDoc.has_project_settings())
            {
                const auto& ps = protoDoc.project_settings();
                docData.machineProfileRef = ps.machine_profile_ref();
                docData.defaultProcessId = ps.default_process_id();
            }

            // === v2 补齐：硬件 Profile 值集 ===
            // 旧文件没有本字段：hardware_profiles_size()==0，读端只返回一个空值集，
            // 由调用方据此决定「保持运行时现有 Profile」，绝不在这里清空用户的 Profile。
            for (int i = 0; i < protoDoc.hardware_profiles_size(); ++i)
            {
                const auto& hp = protoDoc.hardware_profiles(i);
                HardwareProfileInfo info;
                info.id = hp.id();
                info.name = hp.name();
                info.power = hp.power();
                info.speed = hp.speed();
                info.frequency = hp.frequency();
                info.focusOffset = hp.focus_offset();
                info.passes = hp.passes();
                info.kerfMm = hp.kerf_mm();
                info.leadInLengthMm = hp.lead_in_length_mm();
                info.leadInMode = hp.lead_in_mode();
                info.leadInAngleDeg = hp.lead_in_angle_deg();
                docData.hardwareProfiles.push_back(std::move(info));
            }

            for (int i = 0; i < protoDoc.entities_size(); ++i)
            {
                const auto& ed = protoDoc.entities(i);
                std::unique_ptr<Eg::AprEntity> entity = AprEntitySerializer::deserializeEntity(ed);

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
        void emitWarnings(const AprDocument& doc, SerializeWarningCallback warningCb, void* warningCtx)
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

    // AprSerializer 的 PIMPL 实现
    struct AprSerializer::AprSerializerImpl
    {
    public:
        AprSerializerImpl() = default;

        ~AprSerializerImpl()
        {
            delete m_cryptoProvider;
        }

        AprSerializerImpl(const AprSerializerImpl&) = delete;
        AprSerializerImpl& operator=(const AprSerializerImpl&) = delete;

        IAprCryptoProvider* m_cryptoProvider = nullptr;
    };

    AprSerializer::AprSerializer()
        : m_impl(new AprSerializerImpl())
    {
    }

    AprSerializer::~AprSerializer()
    {
        delete m_impl;
    }

    AprSerializer::AprSerializer(AprSerializer&& other) noexcept
        : m_impl(other.m_impl)
    {
        other.m_impl = nullptr;
    }

    AprSerializer& AprSerializer::operator=(AprSerializer&& other) noexcept
    {
        if (this != &other)
        {
            delete m_impl;
            m_impl = other.m_impl;
            other.m_impl = nullptr;
        }
        return *this;
    }

    void AprSerializer::setCryptoProvider(IAprCryptoProvider* provider)
    {
        delete m_impl->m_cryptoProvider;
        m_impl->m_cryptoProvider = provider;
    }

    bool AprSerializer::hasCrypto() const
    {
        return m_impl && m_impl->m_cryptoProvider != nullptr;
    }

    SerializeResult AprSerializer::saveToFile(const char* filePath,
        const AprDocument& doc,
        bool encrypt,
        SerializeWarningCallback warningCb,
        void* warningCtx,
        SerializeProgressCallback progressCb,
        void* progressCtx)
    {
        // 原有接口默认使用 2D 格式（保持向后兼容）
        return saveToFile(filePath, doc, encrypt, FileFormat::Native, warningCb, warningCtx, progressCb, progressCtx);
    }

    SerializeResult AprSerializer::saveToFile(const char* filePath,
        const AprDocument& doc,
        bool encrypt,
        FileFormat fmt,
        SerializeWarningCallback /*warningCb*/,
        void* /*warningCtx*/,
        SerializeProgressCallback progressCb,
        void* progressCtx)
    {
        if (!filePath || !*filePath)
        {
            return SerializeResult::fail("Empty file path");
        }

        // 辅助 lambda：发射进度
        auto emitProgress = [progressCb, progressCtx](float p, const char* stage) {
            if (progressCb)
            {
                progressCb(p, stage, progressCtx);
            }
        };

        emitProgress(0.0f, "serializing");

        // 根据目标格式选择魔数
        const char* magic = (fmt == FileFormat::Native3D) ? AprFileConst::MAGIC_APR3D : AprFileConst::MAGIC_APR;

        std::vector<uint8_t> protoData;
        std::string serializeError;
        if (!serializeToProto(doc, protoData, &serializeError))
        {
            return SerializeResult::fail(
                serializeError.empty() ? "Failed to serialize document to protobuf" : serializeError.c_str());
        }

        emitProgress(0.5f, "encrypting");

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
            flags |= AprFileConst::FLAG_ENCRYPTED;
        }

        emitProgress(0.7f, "checksum");

        const uint32_t crc = computeCrc32(finalData.data(), finalData.size());

        // 直接分三段写出：header(16) + payload + crc(4)。
        // 不再额外分配 HEADER+payload+FOOTER 的整块缓冲并把 payload 再 memcpy 一遍，
        // 大文件下省掉一次与 payload 等大的内存分配和拷贝。
        uint8_t header[AprFileConst::HEADER_SIZE] = { 0 };
        std::memcpy(header, magic, 4);

        header[4] = static_cast<uint8_t>(AprFileConst::FILE_VERSION & 0xFF);
        header[5] = static_cast<uint8_t>((AprFileConst::FILE_VERSION >> 8) & 0xFF);
        header[6] = static_cast<uint8_t>((AprFileConst::FILE_VERSION >> 16) & 0xFF);
        header[7] = static_cast<uint8_t>((AprFileConst::FILE_VERSION >> 24) & 0xFF);

        header[8] = static_cast<uint8_t>(flags & 0xFF);
        header[9] = static_cast<uint8_t>((flags >> 8) & 0xFF);
        header[10] = static_cast<uint8_t>((flags >> 16) & 0xFF);
        header[11] = static_cast<uint8_t>((flags >> 24) & 0xFF);

        header[12] = static_cast<uint8_t>(finalData.size() & 0xFF);
        header[13] = static_cast<uint8_t>((finalData.size() >> 8) & 0xFF);
        header[14] = static_cast<uint8_t>((finalData.size() >> 16) & 0xFF);
        header[15] = static_cast<uint8_t>((finalData.size() >> 24) & 0xFF);

        uint8_t footer[AprFileConst::FOOTER_SIZE];
        footer[0] = static_cast<uint8_t>(crc & 0xFF);
        footer[1] = static_cast<uint8_t>((crc >> 8) & 0xFF);
        footer[2] = static_cast<uint8_t>((crc >> 16) & 0xFF);
        footer[3] = static_cast<uint8_t>((crc >> 24) & 0xFF);

        emitProgress(0.8f, "writing");

        // 原子写：先写同目录临时文件，刷盘成功后再 rename 覆盖目标。
        //
        // 旧实现直接用 trunc 打开目标文件，写到一半崩溃 / 断电 / 磁盘满就会把用户
        // 原有文档清空且没有备份 —— 这种损坏不可恢复，代价远高于多一次 rename。
        const std::filesystem::path targetPath = std::filesystem::u8path(filePath);
        std::filesystem::path tempPath = targetPath;
        {
            // 同目录：跨卷 rename 会退化成「拷贝 + 删除」，失去原子性
            static std::atomic<unsigned long long> s_tempSeq{ 0 };
            tempPath += ".tmp" + std::to_string(s_tempSeq.fetch_add(1, std::memory_order_relaxed));
        }

        auto discardTemp = [&tempPath]() {
            std::error_code ignored;
            std::filesystem::remove(tempPath, ignored);
        };

        {
            std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                return SerializeResult::fail(std::string("Cannot open file for writing: ").append(filePath).c_str());
            }

            out.write(reinterpret_cast<const char*>(header), sizeof(header));
            if (!finalData.empty())
            {
                out.write(
                    reinterpret_cast<const char*>(finalData.data()), static_cast<std::streamsize>(finalData.size()));
            }
            out.write(reinterpret_cast<const char*>(footer), sizeof(footer));

            if (!out.good())
            {
                out.close();
                discardTemp();
                return SerializeResult::fail(std::string("Failed to write file: ").append(filePath).c_str());
            }

            // flush / close 必须显式检查：它们失败时目标文件还不能被替换，
            // 否则rename 会把一份截断的临时文件当成成功结果。
            out.flush();
            if (!out.good())
            {
                out.close();
                discardTemp();
                return SerializeResult::fail(std::string("Failed to flush file: ").append(filePath).c_str());
            }
            out.close();
            if (out.fail())
            {
                discardTemp();
                return SerializeResult::fail(std::string("Failed to close file: ").append(filePath).c_str());
            }
        }

        // flush/close 只把数据交给内核页缓存，断电后 rename 仍可能得到 0 字节文件。
        // 必须先对临时文件内容做持久化同步，失败时目标文件保持原样。
        const Internal::DurableResult tempSync = Internal::syncFile(tempPath);
        if (!tempSync.success)
        {
            discardTemp();
            return SerializeResult::fail(std::string("Failed to flush file to disk: ")
                    .append(filePath)
                    .append(" (")
                    .append(tempSync.error)
                    .append(")")
                    .c_str());
        }

        // 保存前把上一版轮转为 .bak：误操作与坏盘都还有退路。
        // 先删旧备份再 rename，避免 rename 到已存在文件在不同平台上的差异。
        const std::filesystem::path backupPath = std::filesystem::path(targetPath).concat(".bak");
        std::error_code existsError;
        const bool targetExisted = std::filesystem::exists(targetPath, existsError);
        if (existsError)
        {
            discardTemp();
            return SerializeResult::fail(std::string("Failed to inspect target file: ")
                    .append(filePath)
                    .append(" (")
                    .append(existsError.message())
                    .append(")")
                    .c_str());
        }

        if (targetExisted)
        {
            std::error_code removeBackupError;
            std::filesystem::remove(backupPath, removeBackupError);
            if (removeBackupError)
            {
                discardTemp();
                return SerializeResult::fail(std::string("Failed to remove previous backup: ")
                        .append(backupPath.u8string())
                        .append(" (")
                        .append(removeBackupError.message())
                        .append(")")
                        .c_str());
            }

            std::error_code rotateError;
            std::filesystem::rename(targetPath, backupPath, rotateError);
            if (rotateError)
            {
                discardTemp();
                return SerializeResult::fail(std::string("Failed to rotate previous version to backup: ")
                        .append(backupPath.u8string())
                        .append(" (")
                        .append(rotateError.message())
                        .append(")")
                        .c_str());
            }
        }

        std::error_code renameError;
        std::filesystem::rename(tempPath, targetPath, renameError);
        if (renameError)
        {
            discardTemp();
            // 目标文件已被轮转为 .bak：必须还原，否则用户文档在磁盘上「消失」。
            if (targetExisted)
            {
                std::error_code restoreError;
                std::filesystem::rename(backupPath, targetPath, restoreError);
                if (restoreError)
                {
                    APR_ERRORF("[AprSerializer] Failed to restore backup after rename failure: %s (%s)",
                        backupPath.u8string().c_str(),
                        restoreError.message().c_str());
                }
            }
            return SerializeResult::fail(std::string("Failed to replace target file: ")
                    .append(filePath)
                    .append(" (")
                    .append(renameError.message())
                    .append(")")
                    .c_str());
        }

        // rename 之后还必须同步父目录：否则目录项本身可能仍停留在缓存里，
        // 掉电后表现为「文件内容在、文件名不在」。
        const Internal::DurableResult dirSync = Internal::syncParentDirectory(targetPath);
        if (!dirSync.success)
        {
            return SerializeResult::fail(std::string("Failed to flush directory entry to disk: ")
                    .append(filePath)
                    .append(" (")
                    .append(dirSync.error)
                    .append(")")
                    .c_str());
        }

        emitProgress(1.0f, "done");
        return SerializeResult::ok();
    }

    SerializeResult AprSerializer::loadFromFile(
        const char* filePath, AprDocument& doc, SerializeWarningCallback warningCb, void* warningCtx)
    {
        if (!filePath || !*filePath)
        {
            return SerializeResult::fail("Empty file path");
        }

        // 主文件与 .bak 共用同一套读取/校验逻辑（魔数、长度、CRC 全走一遍），
        // doc 由 lambda 在每次尝试前清空，避免失败残留污染回退结果。
        auto loadOnce = [this, &doc, warningCb, warningCtx](const char* filePath) -> SerializeResult {
        doc.clear();

        std::ifstream in(std::filesystem::u8path(filePath), std::ios::binary | std::ios::ate);
        if (!in)
        {
            return SerializeResult::fail(std::string("Cannot open file: ").append(filePath).c_str());
        }

        const auto fileSize = static_cast<size_t>(in.tellg());
        if (fileSize < AprFileConst::HEADER_SIZE + AprFileConst::FOOTER_SIZE)
        {
            return SerializeResult::fail("File too small to be a valid .apr file");
        }

        in.seekg(0);
        std::vector<uint8_t> fileBuffer(fileSize);
        if (!in.read(reinterpret_cast<char*>(fileBuffer.data()), static_cast<std::streamsize>(fileSize)))
        {
            return SerializeResult::fail(std::string("Failed to read file: ").append(filePath).c_str());
        }

        bool headerValid = false;
        bool is3DContainer = false;
        uint32_t version = 0;
        uint32_t flags = 0;
        uint32_t dataLen = 0;

        // 两种魔数都要认。保存侧按目标格式写 APRB（2D）/ AP3D（3D，含 2D+3D 混合文档），
        // 而旧实现只接受 APRB —— 结果是所有 AP3D 文件保存后再也打不开。
        if (fileBuffer.size() >= AprFileConst::HEADER_SIZE)
        {
            if (std::memcmp(fileBuffer.data(), AprFileConst::MAGIC_APR, 4) == 0)
            {
                headerValid = true;
            }
            else if (std::memcmp(fileBuffer.data(), AprFileConst::MAGIC_APR3D, 4) == 0)
            {
                headerValid = true;
                is3DContainer = true;
            }

            if (headerValid)
            {
                version = static_cast<uint32_t>(fileBuffer[4]) | (static_cast<uint32_t>(fileBuffer[5]) << 8) |
                    (static_cast<uint32_t>(fileBuffer[6]) << 16) | (static_cast<uint32_t>(fileBuffer[7]) << 24);
                flags = static_cast<uint32_t>(fileBuffer[8]) | (static_cast<uint32_t>(fileBuffer[9]) << 8) |
                    (static_cast<uint32_t>(fileBuffer[10]) << 16) | (static_cast<uint32_t>(fileBuffer[11]) << 24);
                dataLen = static_cast<uint32_t>(fileBuffer[12]) | (static_cast<uint32_t>(fileBuffer[13]) << 8) |
                    (static_cast<uint32_t>(fileBuffer[14]) << 16) | (static_cast<uint32_t>(fileBuffer[15]) << 24);
            }
        }

        if (!headerValid)
        {
            return SerializeResult::fail("Invalid .apr file header (wrong magic)");
        }

        if (version != AprFileConst::FILE_VERSION)
        {
            syDocumentData(doc).warnings.push_back("File version mismatch: expected " +
                std::to_string(AprFileConst::FILE_VERSION) + ", got " + std::to_string(version));
        }

        if (is3DContainer)
        {
            APR_DEBUGF("[AprSerializer] loadFromFile: 3D container (AP3D), path=%s", filePath);
        }

        // 必须用 64 位相加：dataLen 是 uint32_t，`dataLen + HEADER + FOOTER` 在 32 位域内
        // 会回绕 —— 构造 dataLen=0xFFFFFFFF 的文件头即可通过此检查，随后 CRC 计算
        // 按 4GB 长度越界读取缓冲区。
        const uint64_t totalNeeded =
            static_cast<uint64_t>(dataLen) + AprFileConst::HEADER_SIZE + AprFileConst::FOOTER_SIZE;
        if (totalNeeded > static_cast<uint64_t>(fileSize))
        {
            return SerializeResult::fail("Corrupted file: data size exceeds file size");
        }

        const uint8_t* dataPtr = fileBuffer.data() + AprFileConst::HEADER_SIZE;

        const uint32_t expectedCrc = computeCrc32(dataPtr, dataLen);
        const size_t crcOffset = AprFileConst::HEADER_SIZE + dataLen;
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

        // 优化: 非加密文件直接使用 fileBuffer 中的 payload，避免冗余拷贝
        // 加密文件需要解密后的独立缓冲区
        if (flags & AprFileConst::FLAG_ENCRYPTED)
        {
            if (!m_impl->m_cryptoProvider)
            {
                return SerializeResult::fail("File is encrypted but no crypto provider set");
            }

            // 加密文件: 复制 payload 后解密
            std::vector<uint8_t> data(dataPtr, dataPtr + dataLen);
            auto decResult = m_impl->m_cryptoProvider->decrypt(data.data(), data.size());
            if (!decResult.success)
            {
                return SerializeResult::fail(std::string("Decryption failed: ").append(decResult.errorMessage).c_str());
            }
            data.assign(decResult.data, decResult.data + decResult.dataSize);
            m_impl->m_cryptoProvider->freeCryptoData(decResult.data);

            std::string parseError;
            if (!deserializeFromProto(data.data(), data.size(), doc, &parseError))
            {
                return SerializeResult::fail(
                    parseError.empty() ? "Failed to parse protobuf data" : parseError.c_str());
            }
        }
        else
        {
            // 非加密文件: 直接解析 fileBuffer 中的 payload，零拷贝
            std::string parseError;
            if (!deserializeFromProto(dataPtr, dataLen, doc, &parseError))
            {
                return SerializeResult::fail(
                    parseError.empty() ? "Failed to parse protobuf data" : parseError.c_str());
            }
        }

        emitWarnings(doc, warningCb, warningCtx);
        return SerializeResult::ok();
        };

        const SerializeResult primaryResult = loadOnce(filePath);
        if (primaryResult.success)
        {
            return primaryResult;
        }

        // 主文件损坏时给出可用的回退路径：保存侧每次覆盖前都会把上一版轮转为 .bak。
        // 回退必须显式告知（经警告回调），否则用户会在不知情的情况下拿到旧版本。
        const std::string backupPath = std::string(filePath).append(".bak");
        std::error_code backupExistsError;
        const bool backupExists = std::filesystem::exists(std::filesystem::u8path(backupPath), backupExistsError);
        if (backupExistsError || !backupExists)
        {
            return primaryResult;
        }

        const SerializeResult backupResult = loadOnce(backupPath.c_str());
        if (!backupResult.success)
        {
            // 两者都读不出来：返回主文件的错误（更贴近用户实际打开的目标）。
            return primaryResult;
        }

        char notice[768] = { 0 };
        std::snprintf(notice,
            sizeof(notice),
            "Primary file is damaged (%s); recovered from backup file: %s",
            primaryResult.errorMessage,
            backupPath.c_str());
        APR_WARNF("[AprSerializer] %s", notice);
        if (warningCb)
        {
            warningCb(notice, warningCtx);
        }
        return SerializeResult::ok();
    }

    SerializeResult AprSerializer::serializeToMemory(
        const AprDocument& doc, BinaryBlobOut* out, SerializeWarningCallback /*warningCb*/, void* /*warningCtx*/)
    {
        std::vector<uint8_t> data;
        std::string serializeError;
        if (!serializeToProto(doc, data, &serializeError))
        {
            return SerializeResult::fail(
                serializeError.empty() ? "Failed to serialize document to protobuf" : serializeError.c_str());
        }

        if (out)
        {
            out->written = data.size();
            if (out->data && out->capacity > 0)
            {
                if (data.size() > out->capacity)
                {
                    // 缓冲区不足：明确失败并报告所需字节数，避免调用方把截断数据当完整数据用。
                    // out->written 已置为完整大小，「先传 null 查大小」的调用模式不受影响。
                    char msg[512];
                    std::snprintf(msg, sizeof(msg), "Output buffer too small: need %zu bytes, capacity %zu bytes",
                        data.size(), out->capacity);
                    return SerializeResult::fail(msg);
                }
                std::memcpy(out->data, data.data(), data.size());
            }
        }
        return SerializeResult::ok();
    }

    SerializeResult AprSerializer::deserializeFromMemory(
        BinaryBlob in, AprDocument& doc, SerializeWarningCallback warningCb, void* warningCtx)
    {
        doc.clear();
        std::string parseError;
        if (!deserializeFromProto(in.data, in.size, doc, &parseError))
        {
            return SerializeResult::fail(
                parseError.empty() ? "Failed to parse protobuf data from memory" : parseError.c_str());
        }
        emitWarnings(doc, warningCb, warningCtx);
        return SerializeResult::ok();
    }

    uint32_t AprSerializer::fileVersion()
    {
        return AprFileConst::FILE_VERSION;
    }

    bool AprSerializer::isValidAprFile(const uint8_t* header, size_t headerSize)
    {
        if (!header || headerSize < 4)
        {
            return false;
        }
        return std::memcmp(header, AprFileConst::MAGIC_APR, 4) == 0;
    }

    bool AprSerializer::isValidApr3DFile(const uint8_t* header, size_t headerSize)
    {
        if (!header || headerSize < 4)
        {
            return false;
        }
        return std::memcmp(header, AprFileConst::MAGIC_APR3D, 4) == 0;
    }
}  // namespace Fio
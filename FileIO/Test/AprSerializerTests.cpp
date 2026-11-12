/**
 * @file AprSerializerTests.cpp
 * @brief AprSerializer 回归测试 — 文件级序列化/反序列化
 *
 * 覆盖：
 *   - 内存级序列化/反序列化闭环（BinaryBlob/BinaryBlobOut）
 *   - 文件魔数校验
 *   - 文件版本号
 *   - 空文档序列化
 *   - 元数据序列化
 *   - 加密配置
 *
 * P2 测试覆盖扩展 (2026-07-30)
 * C3 收口 (2026-07-31)：序列化接口已 blob 化，测试改用查询大小→分配→写入 两段式
 */

#include <gtest/gtest.h>

#include "FileIO/AprSerializer.h"
#include "FileIO/AprDocument.h"

// 图元级读写兼容：直接构造 proto 消息验证 mesh_data.name 的读取优先级
#include "AprEntitySerializer.h"
#include "AprDocument.pb.h"

// AprDocument 使用 std::unique_ptr<Eg::AprEntity>，需要完整类型定义
#include "Engine/AprEntity/AprEntity.h"
#include "Engine2D/AprEntity/AprPolygon.h"
#include "Engine2D/AprEntity/AprFillRegion.h"
#include "Engine2D/AprEntity/AprLine.h"
#include "Engine2D/AprEntity/AprSmartLine.h"
#include "Engine3D/AprEntity/AprMeshEntity.h"

// 内部文档数据：测试需要构造带填充色的图层（公开 AprLayerInfo 无 fillColor 字段）
#include "AprDocumentData.h"
#include "Ut/Color.h"

#include <cstring>
#include <memory>

using namespace Fio;

namespace
{
    // 序列化辅助：查询大小 → 分配 → 写入，返回完整数据
    SerializeResult serializeDoc(AprSerializer& s, const AprDocument& doc, std::vector<uint8_t>& out)
    {
        BinaryBlobOut query;
        auto r = s.serializeToMemory(doc, &query);
        if (!r.success)
        {
            return r;
        }
        out.resize(query.written);
        BinaryBlobOut blobOut{ out.data(), out.size(), 0 };
        return s.serializeToMemory(doc, &blobOut);
    }

    SerializeResult deserializeDoc(AprSerializer& s, const std::vector<uint8_t>& data, AprDocument& doc)
    {
        BinaryBlob in{ const_cast<uint8_t*>(data.data()), data.size() };
        return s.deserializeFromMemory(in, doc);
    }
}  // namespace

// ==================== 文件魔数校验 ====================

TEST(AprSerializerTest, MagicHeaderConstants)
{
    // 验证魔数常量正确
    EXPECT_EQ(AprFileConst::MAGIC_APR[0], 'A');
    EXPECT_EQ(AprFileConst::MAGIC_APR[1], 'P');
    EXPECT_EQ(AprFileConst::MAGIC_APR[2], 'R');
    EXPECT_EQ(AprFileConst::MAGIC_APR[3], 'B');

    EXPECT_EQ(AprFileConst::MAGIC_APR3D[0], 'A');
    EXPECT_EQ(AprFileConst::MAGIC_APR3D[1], 'P');
    EXPECT_EQ(AprFileConst::MAGIC_APR3D[2], '3');
    EXPECT_EQ(AprFileConst::MAGIC_APR3D[3], 'D');
}

TEST(AprSerializerTest, FileVersion)
{
    // 文件版本号必须稳定，跨版本兼容
    EXPECT_EQ(AprSerializer::fileVersion(), AprFileConst::FILE_VERSION);
}

TEST(AprSerializerTest, HeaderSize)
{
    // 头部大小 = magic(4) + version(4) + flags(4) + data_len(4)
    EXPECT_EQ(AprFileConst::HEADER_SIZE, 16u);
}

TEST(AprSerializerTest, FooterSize)
{
    // 尾部 CRC32 大小
    EXPECT_EQ(AprFileConst::FOOTER_SIZE, 4u);
}

// ==================== 魔数识别 ====================

TEST(AprSerializerTest, IsValidAprFile_Valid)
{
    const uint8_t header[] = { 'A', 'P', 'R', 'B' };
    EXPECT_TRUE(AprSerializer::isValidAprFile(header, sizeof(header)));
}

TEST(AprSerializerTest, IsValidAprFile_Invalid)
{
    const uint8_t header[] = { 'X', 'X', 'X', 'X' };
    EXPECT_FALSE(AprSerializer::isValidAprFile(header, sizeof(header)));
}

TEST(AprSerializerTest, IsValidAprFile_TooShort)
{
    const uint8_t header[] = { 'A', 'P' };
    EXPECT_FALSE(AprSerializer::isValidAprFile(header, sizeof(header)));
}

TEST(AprSerializerTest, IsValidAprFile_Empty)
{
    EXPECT_FALSE(AprSerializer::isValidAprFile(nullptr, 0));
}

TEST(AprSerializerTest, IsValidApr3DFile_Valid)
{
    const uint8_t header[] = { 'A', 'P', '3', 'D' };
    EXPECT_TRUE(AprSerializer::isValidApr3DFile(header, sizeof(header)));
}

TEST(AprSerializerTest, IsValidApr3DFile_Invalid)
{
    const uint8_t header[] = { 'A', 'P', 'R', 'B' };  // 2D 魔数不是 3D 魔数
    EXPECT_FALSE(AprSerializer::isValidApr3DFile(header, sizeof(header)));
}

// ==================== 构造/析构 ====================

TEST(AprSerializerTest, DefaultConstruction)
{
    AprSerializer serializer;
    EXPECT_FALSE(serializer.hasCrypto());
}

TEST(AprSerializerTest, MoveConstruction)
{
    AprSerializer a;
    AprSerializer b = std::move(a);
    EXPECT_FALSE(b.hasCrypto());
}

// ==================== 加密配置 ====================

TEST(AprSerializerTest, SetCryptoProviderNull)
{
    AprSerializer serializer;
    serializer.setCryptoProvider(nullptr);
    EXPECT_FALSE(serializer.hasCrypto());
}

// ==================== 内存序列化/反序列化闭环 ====================

TEST(AprSerializerTest, SerializeDeserializeEmptyDocument)
{
    // 空文档的序列化/反序列化闭环
    AprDocument doc;
    doc.setAuthor("TestAuthor");
    doc.setSoftwareName("April");
    doc.setMetadataVersion(1);

    AprSerializer serializer;
    std::vector<uint8_t> data;

    auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;
    EXPECT_FALSE(data.empty());

    // 反序列化
    AprDocument loadedDoc;
    auto loadResult = deserializeDoc(serializer, data, loadedDoc);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;

    EXPECT_STREQ(loadedDoc.author(), "TestAuthor");
    EXPECT_STREQ(loadedDoc.softwareName(), "April");
    EXPECT_EQ(loadedDoc.metadataVersion(), 1);
}

TEST(AprSerializerTest, SerializeDeserializeWithLayers)
{
    // 含图层的文档序列化/反序列化闭环
    AprDocument doc;
    doc.setAuthor("Test");

    AprLayerInfo layer;
    layer.id = 1;
    std::strcpy(layer.name, "Default");
    // ARGB：高 8 位为 alpha，这里用不透明红验证颜色往返
    layer.color = 0xFFFF0000;
    layer.visible = true;
    layer.locked = false;
    doc.addLayer(layer);

    AprSerializer serializer;
    std::vector<uint8_t> data;

    auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    AprDocument loadedDoc;
    auto loadResult = deserializeDoc(serializer, data, loadedDoc);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;

    ASSERT_EQ(loadedDoc.layerCount(), 1u);
    AprLayerInfo loadedLayer;
    ASSERT_TRUE(loadedDoc.getLayerAt(0, loadedLayer));
    EXPECT_EQ(loadedLayer.id, 1u);
    EXPECT_STREQ(loadedLayer.name, "Default");
    EXPECT_EQ(loadedLayer.color, 0xFFFF0000u);
    EXPECT_TRUE(loadedLayer.visible);
    EXPECT_FALSE(loadedLayer.locked);
}

TEST(AprSerializerTest, SerializeDeserializeWithMetadata)
{
    // 完整元数据的序列化/反序列化闭环
    AprDocument doc;
    doc.setMetadataVersion(2);
    doc.setMetadataFileVersion(5);
    doc.setAuthor("Developer");
    doc.setSoftwareName("April");
    doc.setSoftwareVersion("1.0.0");
    doc.setCreatedTime("2026-07-30T10:00:00Z");
    doc.setModifiedTime("2026-07-30T12:00:00Z");
    doc.setOperatingSystem("Windows");
    doc.setDescription("Test document");

    AprSerializer serializer;
    std::vector<uint8_t> data;

    auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    AprDocument loadedDoc;
    auto loadResult = deserializeDoc(serializer, data, loadedDoc);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;

    EXPECT_EQ(loadedDoc.metadataVersion(), 2);
    EXPECT_EQ(loadedDoc.metadataFileVersion(), 5);
    EXPECT_STREQ(loadedDoc.author(), "Developer");
    EXPECT_STREQ(loadedDoc.softwareName(), "April");
    EXPECT_STREQ(loadedDoc.softwareVersion(), "1.0.0");
    EXPECT_STREQ(loadedDoc.createdTime(), "2026-07-30T10:00:00Z");
    EXPECT_STREQ(loadedDoc.modifiedTime(), "2026-07-30T12:00:00Z");
    EXPECT_STREQ(loadedDoc.operatingSystem(), "Windows");
    EXPECT_STREQ(loadedDoc.description(), "Test document");
}

TEST(AprSerializerTest, SerializeDeserializeWithHardwareInfo)
{
    // 含硬件信息的文档序列化/反序列化闭环
    AprDocument doc;
    AprHardwareInfo hw;
    std::strcpy(hw.laserType, "CO2");
    std::strcpy(hw.controllerModel, "RDC6445");
    hw.maxPower = 100.0;
    hw.workAreaWidth = 1200.0;
    hw.workAreaHeight = 800.0;
    doc.setHardware(hw);

    AprSerializer serializer;
    std::vector<uint8_t> data;

    auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    AprDocument loadedDoc;
    auto loadResult = deserializeDoc(serializer, data, loadedDoc);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;

    AprHardwareInfo loadedHw;
    loadedDoc.getHardware(loadedHw);
    EXPECT_STREQ(loadedHw.laserType, "CO2");
    EXPECT_STREQ(loadedHw.controllerModel, "RDC6445");
    EXPECT_DOUBLE_EQ(loadedHw.maxPower, 100.0);
    EXPECT_DOUBLE_EQ(loadedHw.workAreaWidth, 1200.0);
    EXPECT_DOUBLE_EQ(loadedHw.workAreaHeight, 800.0);
}

// ==================== SMARTLINE 读写往返 ====================

TEST(AprSerializerTest, SmartLineRoundTrip)
{
    // SMARTLINE 曾是唯一没有持久化分支的 2D 图元：保存后重新打开整条丢失。
    // 这里验证类型、段数、段内坐标与 ID 都能还原。
    AprDocument doc;
    auto smart = std::make_unique<Eg::AprSmartLine>();
    auto line1 = std::make_unique<Eg::AprLine>(std::vector<Ut::Vec2d>{ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 0.0) });
    auto line2 = std::make_unique<Eg::AprLine>(std::vector<Ut::Vec2d>{ Ut::Vec2d(20.0, 5.0), Ut::Vec2d(30.0, 5.0) });
    // connectToPrevious=false：两个段保持各自坐标，便于验证读取时没有被二次平移
    smart->addSegmentUnique(std::move(line1), false);
    smart->addSegmentUnique(std::move(line2), false);
    smart->id = 42;
    doc.addEntity(smart.release());

    AprSerializer serializer;
    std::vector<uint8_t> data;
    auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    AprDocument loadedDoc;
    auto loadResult = deserializeDoc(serializer, data, loadedDoc);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;

    ASSERT_EQ(loadedDoc.entityCount(), 1u);
    Eg::AprEntity* loadedEntity = loadedDoc.entityAt(0);
    ASSERT_NE(loadedEntity, nullptr);
    EXPECT_EQ(loadedEntity->eType, Eg::EType::SMARTLINE);
    EXPECT_EQ(loadedEntity->id, 42);

    auto* loadedSmart = static_cast<Eg::AprSmartLine*>(loadedEntity);
    ASSERT_EQ(loadedSmart->segmentCount(), 2u);

    auto* seg0 = loadedSmart->segment(0);
    auto* seg1 = loadedSmart->segment(1);
    ASSERT_NE(seg0, nullptr);
    ASSERT_NE(seg1, nullptr);
    ASSERT_EQ(seg0->eType, Eg::EType::LINE);
    ASSERT_EQ(seg1->eType, Eg::EType::LINE);

    const auto& pts0 = static_cast<Eg::AprLine*>(seg0)->pointRef();
    const auto& pts1 = static_cast<Eg::AprLine*>(seg1)->pointRef();
    ASSERT_EQ(pts0.size(), 2u);
    ASSERT_EQ(pts1.size(), 2u);
    EXPECT_NEAR(pts0[0].x(), 0.0, 1e-9);
    EXPECT_NEAR(pts0[1].x(), 10.0, 1e-9);
    EXPECT_NEAR(pts1[0].x(), 20.0, 1e-9);
    EXPECT_NEAR(pts1[0].y(), 5.0, 1e-9);
    EXPECT_NEAR(pts1[1].x(), 30.0, 1e-9);
}

TEST(AprSerializerTest, RejectsImperialUnits)
{
    // units=1 表示英制文档，当前几何恒按 mm 解释。读端必须显式失败，
    // 否则整份图会被当作毫米读入，尺寸小 25.4 倍。
    AprDocument doc;
    doc.addLayer(AprLayerInfo{});
    doc.setDocumentUnits(1);

    AprSerializer serializer;
    std::vector<uint8_t> data;
    auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    AprDocument loadedDoc;
    auto loadResult = deserializeDoc(serializer, data, loadedDoc);
    EXPECT_FALSE(loadResult.success);
    EXPECT_STRNE(loadResult.errorMessage, "");
    EXPECT_NE(std::string(loadResult.errorMessage).find("units"), std::string::npos);
}

// ==================== 反序列化错误处理 ====================

TEST(AprSerializerTest, DeserializeFromInvalidData)
{
    AprSerializer serializer;
    AprDocument doc;
    std::vector<uint8_t> invalidData = { 0xDE, 0xAD, 0xBE, 0xEF };

    auto result = deserializeDoc(serializer, invalidData, doc);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage[0] == '\0');
}

TEST(AprSerializerTest, DeserializeFromEmptyData)
{
    // 空数据对于 protobuf 是合法的（空消息），反序列化应成功产生空文档
    AprSerializer serializer;
    AprDocument doc;
    std::vector<uint8_t> emptyData;

    auto result = deserializeDoc(serializer, emptyData, doc);
    EXPECT_TRUE(result.success);  // 空数据 = 合法空 protobuf 消息
    EXPECT_FALSE(doc.isValid());  // 但不含任何有效内容
}

// ==================== 序列化结果工厂方法 ====================

TEST(AprSerializerTest, SerializeResultOk)
{
    auto result = SerializeResult::ok();
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.errorMessage[0] == '\0');
}

TEST(AprSerializerTest, SerializeResultFail)
{
    auto result = SerializeResult::fail("Something went wrong");
    EXPECT_FALSE(result.success);
    EXPECT_STREQ(result.errorMessage, "Something went wrong");
}

// ==================== AprDocument 便捷方法 ====================

TEST(AprSerializerTest, AprDocumentIsValid_Empty)
{
    AprDocument doc;
    EXPECT_FALSE(doc.isValid());
}

TEST(AprSerializerTest, AprDocumentIsValid_WithLayers)
{
    AprDocument doc;
    doc.addLayer(AprLayerInfo{});
    EXPECT_TRUE(doc.isValid());
}

TEST(AprSerializerTest, AprDocumentClear)
{
    AprDocument doc;
    doc.addLayer(AprLayerInfo{});
    doc.setAuthor("Test");
    AprHardwareInfo hw;
    std::strcpy(hw.laserType, "CO2");
    doc.setHardware(hw);

    doc.clear();

    EXPECT_EQ(doc.layerCount(), 0u);
    EXPECT_STREQ(doc.author(), "");
    AprHardwareInfo clearedHw;
    doc.getHardware(clearedHw);
    EXPECT_STREQ(clearedHw.laserType, "");
    EXPECT_FALSE(doc.isValid());
}

// ==================== 两次序列化应产生相同结果（确定性） ====================

TEST(AprSerializerTest, SerializationIsDeterministic)
{
    AprDocument doc;
    doc.setAuthor("Test");

    AprSerializer serializer;
    std::vector<uint8_t> data1, data2;

    auto r1 = serializeDoc(serializer, doc, data1);
    auto r2 = serializeDoc(serializer, doc, data2);

    ASSERT_TRUE(r1.success);
    ASSERT_TRUE(r2.success);
    EXPECT_EQ(data1, data2);
}

// ==================== 加密标志 ====================

TEST(AprSerializerTest, EncryptedFlagConstant)
{
    EXPECT_EQ(AprFileConst::FLAG_ENCRYPTED, 0x01u);
}

// ==================== 文档元数据序列化闭环 ====================

TEST(AprSerializerTest, DocumentWithMetadataRoundTrip)
{
    AprDocument original;
    original.setAuthor("TestAuthor");
    original.setMetadataVersion(2);
    original.setSoftwareName("April");
    original.setDescription("Test document");

    AprSerializer serializer;
    std::vector<uint8_t> data;
    auto result = serializeDoc(serializer, original, data);
    ASSERT_TRUE(result.success);
    EXPECT_FALSE(data.empty());

    AprDocument restored;
    result = deserializeDoc(serializer, data, restored);
    ASSERT_TRUE(result.success);
    EXPECT_STREQ(restored.author(), "TestAuthor");
    EXPECT_EQ(restored.metadataVersion(), 2);
    EXPECT_STREQ(restored.softwareName(), "April");
    EXPECT_STREQ(restored.description(), "Test document");
}

TEST(AprSerializerTest, DocumentWithMultipleLayersRoundTrip)
{
    AprDocument original;
    for (int i = 1; i <= 5; ++i)
    {
        AprLayerInfo layer;
        layer.id = i;
        std::strcpy(layer.name, ("Layer" + std::to_string(i)).c_str());
        layer.visible = (i % 2 == 0);  // 偶层可见
        original.addLayer(layer);
    }

    AprSerializer serializer;
    std::vector<uint8_t> data;
    auto result = serializeDoc(serializer, original, data);
    ASSERT_TRUE(result.success);

    AprDocument restored;
    result = deserializeDoc(serializer, data, restored);
    ASSERT_TRUE(result.success);
    ASSERT_EQ(restored.layerCount(), 5u);
    AprLayerInfo l0, l1, l4;
    ASSERT_TRUE(restored.getLayerAt(0, l0));
    ASSERT_TRUE(restored.getLayerAt(1, l1));
    ASSERT_TRUE(restored.getLayerAt(4, l4));
    EXPECT_STREQ(l0.name, "Layer1");
    EXPECT_STREQ(l4.name, "Layer5");
    EXPECT_TRUE(l1.visible);   // Layer2
    EXPECT_FALSE(l0.visible);  // Layer1
}

// ==================== 网格名称读写口径 ====================

TEST(AprEntitySerializerTest, MeshNamePrefersMeshDataName)
{
    // 历史存档同时写 mesh_data.name 与公共 name：读取以非空的 mesh_data.name 为准，
    // 这样只更新过 mesh_data.name 的旧档不会读回公共块里的旧名字。
    apr::proto::EntityData proto;
    proto.set_type(apr::proto::ENTITY_MESH);
    proto.set_id(7);
    proto.set_name("common_name");
    proto.mutable_mesh_data()->set_name("mesh_name");

    auto entity = Fio::AprEntitySerializer::deserializeEntity(proto);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::MESH);
    EXPECT_STREQ(entity->name(), "mesh_name");
}

TEST(AprEntitySerializerTest, MeshNameFallsBackToCommonName)
{
    // 旧写路径没有 mesh_data.name（proto3 默认空串）：必须回落到公共 name，
    // 否则旧档的网格名称会整体丢失。
    apr::proto::EntityData proto;
    proto.set_type(apr::proto::ENTITY_MESH);
    proto.set_id(8);
    proto.set_name("legacy_common_name");
    proto.mutable_mesh_data();  // 有 mesh_data 但不带 name

    auto entity = Fio::AprEntitySerializer::deserializeEntity(proto);
    ASSERT_NE(entity, nullptr);
    EXPECT_STREQ(entity->name(), "legacy_common_name");
}

TEST(AprSerializerTest, MeshNameRoundTrip)
{
    // 新存档端到端：写入两份名称，读回后名称保持一致
    AprDocument doc;
    auto mesh = std::make_unique<Eg::AprMeshEntity>("mesh_round_trip");
    mesh->id = 9;
    doc.addEntity(mesh.release());

    AprSerializer serializer;
    std::vector<uint8_t> data;
    auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    AprDocument restored;
    auto loadResult = deserializeDoc(serializer, data, restored);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;
    ASSERT_EQ(restored.entityCount(), 1u);
    Eg::AprEntity* loaded = restored.entityAt(0);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->eType, Eg::EType::MESH);
    EXPECT_STREQ(loaded->name(), "mesh_round_trip");
}

TEST(AprSerializerTest, DocumentWithHardwareInfoRoundTrip)
{
    AprDocument original;
    AprHardwareInfo hw;
    std::strcpy(hw.laserType, "CO2");
    std::strcpy(hw.controllerModel, "Model-X");
    hw.maxPower = 100.0;
    hw.workAreaWidth = 600.0;
    hw.workAreaHeight = 400.0;
    original.setHardware(hw);

    AprSerializer serializer;
    std::vector<uint8_t> data;
    auto result = serializeDoc(serializer, original, data);
    ASSERT_TRUE(result.success);

    AprDocument restored;
    result = deserializeDoc(serializer, data, restored);
    ASSERT_TRUE(result.success);
    AprHardwareInfo restoredHw;
    restored.getHardware(restoredHw);
    EXPECT_STREQ(restoredHw.laserType, "CO2");
    EXPECT_STREQ(restoredHw.controllerModel, "Model-X");
    EXPECT_DOUBLE_EQ(restoredHw.maxPower, 100.0);
    EXPECT_DOUBLE_EQ(restoredHw.workAreaWidth, 600.0);
    EXPECT_DOUBLE_EQ(restoredHw.workAreaHeight, 400.0);
}

// ==================== 加工语义序列化闭环（v2） ====================

/// 工艺（图层 → 工艺 → 参数）必须能完整往返。
///
/// 这一条是「工艺是一等文档对象」的落地证明：
/// 参数只存在内存里 = 换台电脑打开就丢，图纸不再可复现。
TEST(AprSerializerTest, ProcessAndLayerOutputRoundTrip)
{
    AprDocument original;

    // 工艺 1：切割
    AprProcessInfo cut;
    cut.id = 1;
    std::strcpy(cut.name, "Cut");
    cut.order = 0;
    cut.output = true;
    cut.params.enabled = true;
    cut.params.powerPercent = 90.0;
    cut.params.speedMmPerSec = 20.0;
    cut.params.frequencyHz = 5000.0;
    cut.params.pulseWidthUs = 3.0;
    cut.params.waveformIndex = 2;
    cut.params.passes = 3;
    cut.params.fillEnabled = true;
    cut.params.fillIntervalMm = 0.25;
    cut.params.fillAngleDeg = 45.0;
    cut.params.fillMode = 2;
    cut.params.kerfMm = 0.05;
    cut.params.rapidSpeedMmPerSec = 250.0;
    original.addProcess(cut);

    // 工艺 2：标刻
    AprProcessInfo engrave;
    engrave.id = 2;
    std::strcpy(engrave.name, "Engrave");
    engrave.order = 1;
    engrave.output = false;
    engrave.params.powerPercent = 15.0;
    engrave.params.speedMmPerSec = 200.0;
    original.addProcess(engrave);

    // 图层：默认工艺 + 输出开关
    AprLayerInfo layer;
    layer.id = 7;
    std::strcpy(layer.name, "CutLayer");
    layer.visible = true;
    layer.locked = false;
    layer.output = false;  // 显示但不加工
    layer.defaultProcessId = 1;
    original.addLayer(layer);

    original.setMachineProfileRef("leadshine_ltdmc");
    original.setDefaultProcessId(1);
    original.setDocumentUnits(0);

    AprSerializer serializer;
    std::vector<uint8_t> data;
    auto result = serializeDoc(serializer, original, data);
    ASSERT_TRUE(result.success);

    AprDocument restored;
    result = deserializeDoc(serializer, data, restored);
    ASSERT_TRUE(result.success);

    // ---- 工艺 ----
    ASSERT_EQ(restored.processCount(), 2u);

    AprProcessInfo rCut;
    ASSERT_TRUE(restored.getProcessAt(0, rCut));
    EXPECT_EQ(1u, rCut.id);
    EXPECT_STREQ(rCut.name, "Cut");
    EXPECT_EQ(0, rCut.order);
    EXPECT_TRUE(rCut.output);
    EXPECT_TRUE(rCut.params.enabled);
    EXPECT_DOUBLE_EQ(90.0, rCut.params.powerPercent);
    EXPECT_DOUBLE_EQ(20.0, rCut.params.speedMmPerSec);
    EXPECT_DOUBLE_EQ(5000.0, rCut.params.frequencyHz);
    EXPECT_DOUBLE_EQ(3.0, rCut.params.pulseWidthUs);
    EXPECT_EQ(2, rCut.params.waveformIndex);
    EXPECT_EQ(3, rCut.params.passes);
    EXPECT_TRUE(rCut.params.fillEnabled);
    EXPECT_DOUBLE_EQ(0.25, rCut.params.fillIntervalMm);
    EXPECT_DOUBLE_EQ(45.0, rCut.params.fillAngleDeg);
    EXPECT_EQ(2, rCut.params.fillMode);
    EXPECT_DOUBLE_EQ(0.05, rCut.params.kerfMm);
    EXPECT_DOUBLE_EQ(250.0, rCut.params.rapidSpeedMmPerSec);

    AprProcessInfo rEngrave;
    ASSERT_TRUE(restored.getProcessAt(1, rEngrave));
    EXPECT_EQ(2u, rEngrave.id);
    EXPECT_STREQ(rEngrave.name, "Engrave");
    EXPECT_EQ(1, rEngrave.order);
    EXPECT_FALSE(rEngrave.output);
    EXPECT_DOUBLE_EQ(15.0, rEngrave.params.powerPercent);

    // ---- 图层 ----
    ASSERT_EQ(restored.layerCount(), 1u);
    AprLayerInfo rLayer;
    ASSERT_TRUE(restored.getLayerAt(0, rLayer));
    EXPECT_EQ(7u, rLayer.id);
    EXPECT_STREQ(rLayer.name, "CutLayer");
    EXPECT_FALSE(rLayer.output);
    EXPECT_EQ(1u, rLayer.defaultProcessId);

    // ---- 工程级设置 ----
    EXPECT_STREQ(restored.machineProfileRef(), "leadshine_ltdmc");
    EXPECT_EQ(1u, restored.defaultProcessId());
    EXPECT_EQ(0, restored.documentUnits());
}

/// 图层的「输出开关」必须能独立于「可见性」往返。
///
/// 这两个是**不同**的概念：visible=false + output=true = 不显示但要加工；
/// visible=true + output=false = 显示但不加工。合成一个字段就再也分不开了。
TEST(AprSerializerTest, LayerOutputIsIndependentOfVisibility)
{
    AprDocument original;

    AprLayerInfo hiddenButMachined;
    hiddenButMachined.id = 1;
    std::strcpy(hiddenButMachined.name, "Hidden");
    hiddenButMachined.visible = false;
    hiddenButMachined.output = true;
    original.addLayer(hiddenButMachined);

    AprLayerInfo shownButNotMachined;
    shownButNotMachined.id = 2;
    std::strcpy(shownButNotMachined.name, "Annotation");
    shownButNotMachined.visible = true;
    shownButNotMachined.output = false;
    original.addLayer(shownButNotMachined);

    AprSerializer serializer;
    std::vector<uint8_t> data;
    auto result = serializeDoc(serializer, original, data);
    ASSERT_TRUE(result.success);

    AprDocument restored;
    result = deserializeDoc(serializer, data, restored);
    ASSERT_TRUE(result.success);

    ASSERT_EQ(restored.layerCount(), 2u);
    AprLayerInfo l0, l1;
    ASSERT_TRUE(restored.getLayerAt(0, l0));
    ASSERT_TRUE(restored.getLayerAt(1, l1));

    EXPECT_FALSE(l0.visible);
    EXPECT_TRUE(l0.output);

    EXPECT_TRUE(l1.visible);
    EXPECT_FALSE(l1.output);
}

/// 旧文件（v1，无 output 字段）读入时，图层必须**默认参与加工**。
///
/// 若按 proto3 默认 false 处理，打开旧文件后所有图层都不加工，
/// 表现是「点开始加工什么都没发生」且没有任何报错 —— 最难排查的一类现场问题。
TEST(AprSerializerTest, LegacyDocumentWithoutOutputFieldDefaultsToMachined)
{
    // 构造一份「只有 visible/locked」的图层数据（模拟 v1 文件的形状）
    AprDocument original;
    AprLayerInfo layer;
    layer.id = 1;
    std::strcpy(layer.name, "Legacy");
    layer.visible = true;
    layer.locked = false;
    original.addLayer(layer);

    AprSerializer serializer;
    std::vector<uint8_t> data;
    auto result = serializeDoc(serializer, original, data);
    ASSERT_TRUE(result.success);

    // 写出的文件里 output 字段存在（proto3 optional 有值即写入）。
    // 读回时走 has_output() 分支，值应为 true。
    AprDocument restored;
    result = deserializeDoc(serializer, data, restored);
    ASSERT_TRUE(result.success);

    AprLayerInfo l0;
    ASSERT_TRUE(restored.getLayerAt(0, l0));
    EXPECT_TRUE(l0.output) << "output must default to true (preserving the behavior of older files)";
}

// ==================== 移动语义测试 ====================

TEST(AprSerializerTest, MoveAssignment)
{
    AprSerializer a;
    AprSerializer b;
    a.setCryptoProvider(nullptr);

    b = std::move(a);  // 移动赋值

    // 移动后 b 应可用，a 应处于有效但未指定状态
    AprDocument doc;
    std::vector<uint8_t> data;
    auto result = serializeDoc(b, doc, data);
    EXPECT_TRUE(result.success);
}

// ==================== 颜色 alpha 持久化 ====================

TEST(AprSerializerTest, LayerColorAlphaRoundTrip)
{
    AprDocument doc;
    auto& src = syDocumentData(doc);
    // 图层色 alpha=128，填充色 alpha=64：非不透明值必须原样往返
    LayerInfo layer;
    layer.id = 3;
    layer.name = "AlphaLayer";
    layer.color = 0x800A141E;   // ARGB(128,10,20,30)
    layer.fill = true;
    layer.fillColor = 0x40C8280A; // ARGB(64,200,40,10)
    src.layers.push_back(layer);

    AprSerializer serializer;
    std::vector<uint8_t> data;
    auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    AprDocument loadedDoc;
    auto loadResult = deserializeDoc(serializer, data, loadedDoc);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;

    const auto& loaded = syDocumentData(loadedDoc).layers;
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].color, 0x800A141Eu);
    EXPECT_EQ(loaded[0].fillColor, 0x40C8280Au);
}

TEST(AprSerializerTest, LayerColorOpaqueRoundTrip)
{
    AprDocument doc;
    auto& src = syDocumentData(doc);
    LayerInfo layer;
    layer.id = 5;
    layer.name = "OpaqueLayer";
    layer.color = 0xFF0A141E;
    layer.fillColor = 0xFFC8280A;
    src.layers.push_back(layer);

    AprSerializer serializer;
    std::vector<uint8_t> data;
    auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    AprDocument loadedDoc;
    auto loadResult = deserializeDoc(serializer, data, loadedDoc);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;

    const auto& loaded = syDocumentData(loadedDoc).layers;
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].color, 0xFF0A141Eu);
    EXPECT_EQ(loaded[0].fillColor, 0xFFC8280Au);
}

TEST(AprSerializerTest, LegacyLayerColorZeroAlphaFallsBackOpaque)
{
    AprDocument doc;
    auto& src = syDocumentData(doc);
    // 模拟历史数据：颜色高 8 位为 0（旧写路径未设置 alpha）
    LayerInfo layer;
    layer.id = 7;
    layer.name = "LegacyLayer";
    layer.color = 0x000A141E;
    layer.fillColor = 0x00C8280A;
    src.layers.push_back(layer);

    AprSerializer serializer;
    std::vector<uint8_t> data;
    auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    AprDocument loadedDoc;
    auto loadResult = deserializeDoc(serializer, data, loadedDoc);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;

    const auto& loaded = syDocumentData(loadedDoc).layers;
    ASSERT_EQ(loaded.size(), 1u);
    // alpha 位 0 必须回落到不透明，RGB 保持不变
    EXPECT_EQ(loaded[0].color, 0xFF0A141Eu);
    EXPECT_EQ(loaded[0].fillColor, 0xFFC8280Au);
}

TEST(AprSerializerTest, EntityOverrideColorAlphaRoundTrip)
{
    AprDocument doc;
    auto& src = syDocumentData(doc);
    // 色块实体覆盖色 alpha=96：持久化后必须保留，几何（三角形流）也要保留
    auto fill = std::make_unique<Eg::AprFillRegion>();
    fill->id = 1001;
    const Ut::Vec2d tri[] = { Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 0.0), Ut::Vec2d(10.0, 10.0),
        Ut::Vec2d(10.0, 10.0), Ut::Vec2d(0.0, 10.0), Ut::Vec2d(0.0, 0.0) };
    fill->setVertices(tri, 6);
    const Eg::EntityId sources[] = { 7, 8 };
    fill->setSources(sources, 2);
    fill->setOverrideColor(Ut::Color::fromRGB255(90, 160, 220, 96));
    src.entities.push_back(std::move(fill));

    AprSerializer serializer;
    std::vector<uint8_t> data;
    auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    AprDocument loadedDoc;
    auto loadResult = deserializeDoc(serializer, data, loadedDoc);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;

    const auto& loaded = syDocumentData(loadedDoc).entities;
    ASSERT_EQ(loaded.size(), 1u);
    ASSERT_EQ(loaded[0]->eType, Eg::EType::FILL_REGION);
    const auto* loadedFill = static_cast<const Eg::AprFillRegion*>(loaded[0].get());
    EXPECT_EQ(loadedFill->vertexCount(), 6u);
    EXPECT_EQ(loadedFill->sourceCount(), 2u);
    const auto overrideColor = loaded[0]->overrideColor();
    ASSERT_TRUE(overrideColor.has_value());
    EXPECT_EQ(overrideColor->red255(), 90);
    EXPECT_EQ(overrideColor->green255(), 160);
    EXPECT_EQ(overrideColor->blue255(), 220);
    EXPECT_EQ(overrideColor->alpha255(), 96);
    EXPECT_EQ(loaded[0]->getColor().alpha255(), 96);
}

TEST(AprSerializerTest, EntityWithoutOverrideColorStaysNullopt)
{
    AprDocument doc;
    auto& src = syDocumentData(doc);
    auto poly = std::make_unique<Eg::AprPolygon>();
    poly->id = 1002;
    poly->setVertices({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 0.0), Ut::Vec2d(10.0, 10.0) });
    src.entities.push_back(std::move(poly));

    AprSerializer serializer;
    std::vector<uint8_t> data;
    auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    AprDocument loadedDoc;
    auto loadResult = deserializeDoc(serializer, data, loadedDoc);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;

    const auto& loaded = syDocumentData(loadedDoc).entities;
    ASSERT_EQ(loaded.size(), 1u);
    // 旧文件/无覆盖色：保持跟随图层色，不能被钉成独立颜色
    EXPECT_FALSE(loaded[0]->overrideColor().has_value());
}

// ==================== 图层 → 硬件 Profile 绑定持久化 ====================
// AprDocument.proto 里 hardware_profile_id=12 / fill_hardware_profile_id=13
// 早就声明了，但 AprSerializer 的写/读路径一直没赋过值：保存后图层绑定的
// 工艺参数全部退回默认 Profile，用户设置的功率/速度在重开文档后丢失。

TEST(AprSerializerTest, LayerHardwareProfileIdsRoundTrip)
{
    AprDocument doc;
    auto& src = syDocumentData(doc);
    LayerInfo layer;
    layer.id = 7;
    layer.name = "CutLayer";
    layer.hardwareProfileId = 3;
    layer.fillHardwareProfileId = 4;
    src.layers.push_back(layer);

    AprSerializer serializer;
    std::vector<uint8_t> data;
    const auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    AprDocument loadedDoc;
    const auto loadResult = deserializeDoc(serializer, data, loadedDoc);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;

    const auto& loaded = syDocumentData(loadedDoc).layers;
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].hardwareProfileId, 3);
    EXPECT_EQ(loaded[0].fillHardwareProfileId, 4);
}

TEST(AprSerializerTest, LayerWithoutFillProfileFallsBackToMinusOne)
{
    AprDocument doc;
    auto& src = syDocumentData(doc);
    LayerInfo layer;
    layer.id = 9;
    layer.name = "LegacyLayer";
    layer.hardwareProfileId = 2;
    // fill 未设置（-1）：不应被写成 proto3 默认 0 而误指到默认 Profile
    src.layers.push_back(layer);

    AprSerializer serializer;
    std::vector<uint8_t> data;
    const auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    AprDocument loadedDoc;
    const auto loadResult = deserializeDoc(serializer, data, loadedDoc);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;

    const auto& loaded = syDocumentData(loadedDoc).layers;
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].hardwareProfileId, 2);
    EXPECT_EQ(loaded[0].fillHardwareProfileId, -1);
}

// ==================== 完整加工语义往返（公开 API 通道） ====================
// 生产导出路径不能碰 AprDocumentData.h（那是 FileIO.dll 内部头）；它只能走
// AprLayerDetailInfo / AprHardwareProfileInfo 这两个跨 DLL POD。下面用公开 API
// 验证这条通道确实把图层完整字段 + Profile 值集写进 .apr 并读回来。

TEST(AprSerializerTest, LayerDetailAndProfileValueSetRoundTripViaPublicApi)
{
    AprDocument doc;

    AprLayerDetailInfo layer;
    layer.id = 42;
    std::strncpy(layer.name, "Cut-42", sizeof(layer.name) - 1);
    layer.color = 0xFF123456;
    layer.visible = false;
    layer.locked = true;
    layer.output = false;
    layer.defaultProcessId = 7;
    layer.layerType = 1;
    layer.fill = true;
    layer.fillColor = 0xFFAABBCC;
    layer.hardwareProfileId = 3;
    layer.fillHardwareProfileId = 4;
    doc.addLayerDetail(layer);

    AprHardwareProfileInfo profile;
    profile.id = 3;
    std::strncpy(profile.name, "Steel-3mm", sizeof(profile.name) - 1);
    profile.power = 78.5;
    profile.speed = 245.25;
    profile.frequency = 32.0;  // kHz
    profile.focusOffset = -1.25;
    profile.passes = 3;
    doc.addHardwareProfile(profile);

    AprSerializer serializer;
    std::vector<uint8_t> data;
    const auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    AprDocument loadedDoc;
    const auto loadResult = deserializeDoc(serializer, data, loadedDoc);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;

    ASSERT_EQ(loadedDoc.layerCount(), 1u);
    AprLayerDetailInfo loadedLayer;
    ASSERT_TRUE(loadedDoc.getLayerDetailAt(0, loadedLayer));
    EXPECT_EQ(loadedLayer.id, 42u);
    EXPECT_STREQ(loadedLayer.name, "Cut-42");
    EXPECT_EQ(loadedLayer.color, 0xFF123456u);
    EXPECT_FALSE(loadedLayer.visible);
    EXPECT_TRUE(loadedLayer.locked);
    EXPECT_FALSE(loadedLayer.output);
    EXPECT_EQ(loadedLayer.defaultProcessId, 7u);
    EXPECT_EQ(loadedLayer.layerType, 1u);
    EXPECT_TRUE(loadedLayer.fill);
    EXPECT_EQ(loadedLayer.fillColor, 0xFFAABBCCu);
    EXPECT_EQ(loadedLayer.hardwareProfileId, 3);
    EXPECT_EQ(loadedLayer.fillHardwareProfileId, 4);

    ASSERT_EQ(loadedDoc.hardwareProfileCount(), 1u);
    AprHardwareProfileInfo loadedProfile;
    ASSERT_TRUE(loadedDoc.getHardwareProfileAt(0, loadedProfile));
    EXPECT_EQ(loadedProfile.id, 3);
    EXPECT_STREQ(loadedProfile.name, "Steel-3mm");
    EXPECT_DOUBLE_EQ(loadedProfile.power, 78.5);
    EXPECT_DOUBLE_EQ(loadedProfile.speed, 245.25);
    EXPECT_DOUBLE_EQ(loadedProfile.frequency, 32.0);
    EXPECT_DOUBLE_EQ(loadedProfile.focusOffset, -1.25);
    EXPECT_EQ(loadedProfile.passes, 3);
}

TEST(AprSerializerTest, LegacyDocumentWithoutProfileValueSetStaysEmpty)
{
    // v1/v2 旧文件没有 hardware_profiles 字段。读端必须返回空值集，
    // 由上层据此决定「保持运行时现有 Profile」——静默清空用户参数是数据丢失。
    AprDocument doc;
    AprLayerDetailInfo layer;
    layer.id = 1;
    std::strncpy(layer.name, "Legacy", sizeof(layer.name) - 1);
    doc.addLayerDetail(layer);

    AprSerializer serializer;
    std::vector<uint8_t> data;
    ASSERT_TRUE(serializeDoc(serializer, doc, data).success);

    AprDocument loadedDoc;
    ASSERT_TRUE(deserializeDoc(serializer, data, loadedDoc).success);
    EXPECT_EQ(loadedDoc.hardwareProfileCount(), 0u);

    AprLayerDetailInfo loadedLayer;
    ASSERT_TRUE(loadedDoc.getLayerDetailAt(0, loadedLayer));
    EXPECT_EQ(loadedLayer.hardwareProfileId, 0);
    EXPECT_EQ(loadedLayer.fillHardwareProfileId, -1);
}

// ==================== 轮廓加工顺序（contourOrdering）序列化 ====================

TEST(AprSerializerTest, ProcessContourOrderingRoundTrip)
{
    // 工艺参数的 contourOrdering 以前没有 proto 字段，保存后即丢；
    // 这里锁定「写进去 → 读回来」并且旧档案（0）保持文档顺序。
    AprDocument doc;
    doc.setDefaultProcessId(1);

    AprProcessInfo ordered;
    ordered.id = 1;
    std::strncpy(ordered.name, "InnerFirst", sizeof(ordered.name) - 1);
    ordered.params.contourOrdering = 1;
    doc.addProcess(ordered);

    AprProcessInfo legacy;
    legacy.id = 2;
    std::strncpy(legacy.name, "DocumentOrder", sizeof(legacy.name) - 1);
    legacy.params.contourOrdering = 0;
    doc.addProcess(legacy);

    AprSerializer serializer;
    std::vector<uint8_t> data;
    ASSERT_TRUE(serializeDoc(serializer, doc, data).success);

    AprDocument loadedDoc;
    ASSERT_TRUE(deserializeDoc(serializer, data, loadedDoc).success);
    ASSERT_EQ(loadedDoc.processCount(), 2u);

    AprProcessInfo loaded;
    ASSERT_TRUE(loadedDoc.getProcessAt(0, loaded));
    EXPECT_EQ(loaded.params.contourOrdering, 1);

    ASSERT_TRUE(loadedDoc.getProcessAt(1, loaded));
    EXPECT_EQ(loaded.params.contourOrdering, 0);
}

TEST(AprSerializerTest, HardwareProfileContourOrderingRoundTrip)
{
    // HardwareProfile 的 proto 原来没有 contour_ordering，导出后重开就退回文档顺序。
    AprDocument doc;

    AprHardwareProfileInfo ordered;
    ordered.id = 5;
    std::strncpy(ordered.name, "InnerFirst", sizeof(ordered.name) - 1);
    ordered.contourOrdering = 1;
    doc.addHardwareProfile(ordered);

    AprHardwareProfileInfo sourceDefault;
    sourceDefault.id = 6;
    std::strncpy(sourceDefault.name, "DocumentOrder", sizeof(sourceDefault.name) - 1);
    doc.addHardwareProfile(sourceDefault);

    AprSerializer serializer;
    std::vector<uint8_t> data;
    ASSERT_TRUE(serializeDoc(serializer, doc, data).success);

    AprDocument loadedDoc;
    ASSERT_TRUE(deserializeDoc(serializer, data, loadedDoc).success);
    ASSERT_EQ(loadedDoc.hardwareProfileCount(), 2u);

    AprHardwareProfileInfo loadedProfile;
    ASSERT_TRUE(loadedDoc.getHardwareProfileAt(0, loadedProfile));
    EXPECT_EQ(loadedProfile.contourOrdering, 1);

    ASSERT_TRUE(loadedDoc.getHardwareProfileAt(1, loadedProfile));
    EXPECT_EQ(loadedProfile.contourOrdering, 0);
}

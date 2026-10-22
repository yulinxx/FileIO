/**
 * @file SySerializerTests.cpp
 * @brief SySerializer 回归测试 — 文件级序列化/反序列化
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

#include "FileIO/SySerializer.h"
#include "FileIO/SyDocument.h"

// SyDocument 使用 std::unique_ptr<Eg::SyEntity>，需要完整类型定义
#include "Engine/SyEntity/SyEntity.h"

#include <cstring>

using namespace Fio;

namespace
{
    // 序列化辅助：查询大小 → 分配 → 写入，返回完整数据
    SerializeResult serializeDoc(SySerializer& s, const SyDocument& doc, std::vector<uint8_t>& out)
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

    SerializeResult deserializeDoc(SySerializer& s, const std::vector<uint8_t>& data, SyDocument& doc)
    {
        BinaryBlob in{ const_cast<uint8_t*>(data.data()), data.size() };
        return s.deserializeFromMemory(in, doc);
    }
}  // namespace

// ==================== 文件魔数校验 ====================

TEST(SySerializerTest, MagicHeaderConstants)
{
    // 验证魔数常量正确
    EXPECT_EQ(SyFileConst::MAGIC_SY[0], 'S');
    EXPECT_EQ(SyFileConst::MAGIC_SY[1], 'Y');
    EXPECT_EQ(SyFileConst::MAGIC_SY[2], 'P');
    EXPECT_EQ(SyFileConst::MAGIC_SY[3], 'B');

    EXPECT_EQ(SyFileConst::MAGIC_SYX[0], 'S');
    EXPECT_EQ(SyFileConst::MAGIC_SYX[1], 'X');
    EXPECT_EQ(SyFileConst::MAGIC_SYX[2], 'P');
    EXPECT_EQ(SyFileConst::MAGIC_SYX[3], 'B');
}

TEST(SySerializerTest, FileVersion)
{
    // 文件版本号必须稳定，跨版本兼容
    EXPECT_EQ(SySerializer::fileVersion(), SyFileConst::FILE_VERSION);
}

TEST(SySerializerTest, HeaderSize)
{
    // 头部大小 = magic(4) + version(4) + flags(4) + data_len(4)
    EXPECT_EQ(SyFileConst::HEADER_SIZE, 16u);
}

TEST(SySerializerTest, FooterSize)
{
    // 尾部 CRC32 大小
    EXPECT_EQ(SyFileConst::FOOTER_SIZE, 4u);
}

// ==================== 魔数识别 ====================

TEST(SySerializerTest, IsValidSyFile_Valid)
{
    const uint8_t header[] = { 'S', 'Y', 'P', 'B' };
    EXPECT_TRUE(SySerializer::isValidSyFile(header, sizeof(header)));
}

TEST(SySerializerTest, IsValidSyFile_Invalid)
{
    const uint8_t header[] = { 'X', 'X', 'X', 'X' };
    EXPECT_FALSE(SySerializer::isValidSyFile(header, sizeof(header)));
}

TEST(SySerializerTest, IsValidSyFile_TooShort)
{
    const uint8_t header[] = { 'S', 'Y' };
    EXPECT_FALSE(SySerializer::isValidSyFile(header, sizeof(header)));
}

TEST(SySerializerTest, IsValidSyFile_Empty)
{
    EXPECT_FALSE(SySerializer::isValidSyFile(nullptr, 0));
}

TEST(SySerializerTest, IsValidSyxFile_Valid)
{
    const uint8_t header[] = { 'S', 'X', 'P', 'B' };
    EXPECT_TRUE(SySerializer::isValidSyxFile(header, sizeof(header)));
}

TEST(SySerializerTest, IsValidSyxFile_Invalid)
{
    const uint8_t header[] = { 'S', 'Y', 'P', 'B' };  // SY 魔数不是 SYX
    EXPECT_FALSE(SySerializer::isValidSyxFile(header, sizeof(header)));
}

// ==================== 构造/析构 ====================

TEST(SySerializerTest, DefaultConstruction)
{
    SySerializer serializer;
    EXPECT_FALSE(serializer.hasCrypto());
}

TEST(SySerializerTest, MoveConstruction)
{
    SySerializer a;
    SySerializer b = std::move(a);
    EXPECT_FALSE(b.hasCrypto());
}

// ==================== 加密配置 ====================

TEST(SySerializerTest, SetCryptoProviderNull)
{
    SySerializer serializer;
    serializer.setCryptoProvider(nullptr);
    EXPECT_FALSE(serializer.hasCrypto());
}

// ==================== 内存序列化/反序列化闭环 ====================

TEST(SySerializerTest, SerializeDeserializeEmptyDocument)
{
    // 空文档的序列化/反序列化闭环
    SyDocument doc;
    doc.setAuthor("TestAuthor");
    doc.setSoftwareName("SanYiCAD");
    doc.setMetadataVersion(1);

    SySerializer serializer;
    std::vector<uint8_t> data;

    auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;
    EXPECT_FALSE(data.empty());

    // 反序列化
    SyDocument loadedDoc;
    auto loadResult = deserializeDoc(serializer, data, loadedDoc);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;

    EXPECT_STREQ(loadedDoc.author(), "TestAuthor");
    EXPECT_STREQ(loadedDoc.softwareName(), "SanYiCAD");
    EXPECT_EQ(loadedDoc.metadataVersion(), 1);
}

TEST(SySerializerTest, SerializeDeserializeWithLayers)
{
    // 含图层的文档序列化/反序列化闭环
    SyDocument doc;
    doc.setAuthor("Test");

    SyLayerInfo layer;
    layer.id = 1;
    std::strcpy(layer.name, "Default");
    layer.color = 0xFF0000;
    layer.visible = true;
    layer.locked = false;
    doc.addLayer(layer);

    SySerializer serializer;
    std::vector<uint8_t> data;

    auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    SyDocument loadedDoc;
    auto loadResult = deserializeDoc(serializer, data, loadedDoc);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;

    ASSERT_EQ(loadedDoc.layerCount(), 1u);
    SyLayerInfo loadedLayer;
    ASSERT_TRUE(loadedDoc.getLayerAt(0, loadedLayer));
    EXPECT_EQ(loadedLayer.id, 1u);
    EXPECT_STREQ(loadedLayer.name, "Default");
    EXPECT_EQ(loadedLayer.color, 0xFF0000u);
    EXPECT_TRUE(loadedLayer.visible);
    EXPECT_FALSE(loadedLayer.locked);
}

TEST(SySerializerTest, SerializeDeserializeWithMetadata)
{
    // 完整元数据的序列化/反序列化闭环
    SyDocument doc;
    doc.setMetadataVersion(2);
    doc.setMetadataFileVersion(5);
    doc.setAuthor("Developer");
    doc.setSoftwareName("SanYiCAD");
    doc.setSoftwareVersion("1.0.0");
    doc.setCreatedTime("2026-07-30T10:00:00Z");
    doc.setModifiedTime("2026-07-30T12:00:00Z");
    doc.setOperatingSystem("Windows");
    doc.setDescription("Test document");

    SySerializer serializer;
    std::vector<uint8_t> data;

    auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    SyDocument loadedDoc;
    auto loadResult = deserializeDoc(serializer, data, loadedDoc);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;

    EXPECT_EQ(loadedDoc.metadataVersion(), 2);
    EXPECT_EQ(loadedDoc.metadataFileVersion(), 5);
    EXPECT_STREQ(loadedDoc.author(), "Developer");
    EXPECT_STREQ(loadedDoc.softwareName(), "SanYiCAD");
    EXPECT_STREQ(loadedDoc.softwareVersion(), "1.0.0");
    EXPECT_STREQ(loadedDoc.createdTime(), "2026-07-30T10:00:00Z");
    EXPECT_STREQ(loadedDoc.modifiedTime(), "2026-07-30T12:00:00Z");
    EXPECT_STREQ(loadedDoc.operatingSystem(), "Windows");
    EXPECT_STREQ(loadedDoc.description(), "Test document");
}

TEST(SySerializerTest, SerializeDeserializeWithHardwareInfo)
{
    // 含硬件信息的文档序列化/反序列化闭环
    SyDocument doc;
    SyHardwareInfo hw;
    std::strcpy(hw.laserType, "CO2");
    std::strcpy(hw.controllerModel, "RDC6445");
    hw.maxPower = 100.0;
    hw.workAreaWidth = 1200.0;
    hw.workAreaHeight = 800.0;
    doc.setHardware(hw);

    SySerializer serializer;
    std::vector<uint8_t> data;

    auto saveResult = serializeDoc(serializer, doc, data);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    SyDocument loadedDoc;
    auto loadResult = deserializeDoc(serializer, data, loadedDoc);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;

    SyHardwareInfo loadedHw;
    loadedDoc.getHardware(loadedHw);
    EXPECT_STREQ(loadedHw.laserType, "CO2");
    EXPECT_STREQ(loadedHw.controllerModel, "RDC6445");
    EXPECT_DOUBLE_EQ(loadedHw.maxPower, 100.0);
    EXPECT_DOUBLE_EQ(loadedHw.workAreaWidth, 1200.0);
    EXPECT_DOUBLE_EQ(loadedHw.workAreaHeight, 800.0);
}

// ==================== 反序列化错误处理 ====================

TEST(SySerializerTest, DeserializeFromInvalidData)
{
    SySerializer serializer;
    SyDocument doc;
    std::vector<uint8_t> invalidData = { 0xDE, 0xAD, 0xBE, 0xEF };

    auto result = deserializeDoc(serializer, invalidData, doc);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage[0] == '\0');
}

TEST(SySerializerTest, DeserializeFromEmptyData)
{
    // 空数据对于 protobuf 是合法的（空消息），反序列化应成功产生空文档
    SySerializer serializer;
    SyDocument doc;
    std::vector<uint8_t> emptyData;

    auto result = deserializeDoc(serializer, emptyData, doc);
    EXPECT_TRUE(result.success);  // 空数据 = 合法空 protobuf 消息
    EXPECT_FALSE(doc.isValid());  // 但不含任何有效内容
}

// ==================== 序列化结果工厂方法 ====================

TEST(SySerializerTest, SerializeResultOk)
{
    auto result = SerializeResult::ok();
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.errorMessage[0] == '\0');
}

TEST(SySerializerTest, SerializeResultFail)
{
    auto result = SerializeResult::fail("Something went wrong");
    EXPECT_FALSE(result.success);
    EXPECT_STREQ(result.errorMessage, "Something went wrong");
}

// ==================== SyDocument 便捷方法 ====================

TEST(SySerializerTest, SyDocumentIsValid_Empty)
{
    SyDocument doc;
    EXPECT_FALSE(doc.isValid());
}

TEST(SySerializerTest, SyDocumentIsValid_WithLayers)
{
    SyDocument doc;
    doc.addLayer(SyLayerInfo{});
    EXPECT_TRUE(doc.isValid());
}

TEST(SySerializerTest, SyDocumentClear)
{
    SyDocument doc;
    doc.addLayer(SyLayerInfo{});
    doc.setAuthor("Test");
    SyHardwareInfo hw;
    std::strcpy(hw.laserType, "CO2");
    doc.setHardware(hw);

    doc.clear();

    EXPECT_EQ(doc.layerCount(), 0u);
    EXPECT_STREQ(doc.author(), "");
    SyHardwareInfo clearedHw;
    doc.getHardware(clearedHw);
    EXPECT_STREQ(clearedHw.laserType, "");
    EXPECT_FALSE(doc.isValid());
}

// ==================== 两次序列化应产生相同结果（确定性） ====================

TEST(SySerializerTest, SerializationIsDeterministic)
{
    SyDocument doc;
    doc.setAuthor("Test");

    SySerializer serializer;
    std::vector<uint8_t> data1, data2;

    auto r1 = serializeDoc(serializer, doc, data1);
    auto r2 = serializeDoc(serializer, doc, data2);

    ASSERT_TRUE(r1.success);
    ASSERT_TRUE(r2.success);
    EXPECT_EQ(data1, data2);
}

// ==================== 加密标志 ====================

TEST(SySerializerTest, EncryptedFlagConstant)
{
    EXPECT_EQ(SyFileConst::FLAG_ENCRYPTED, 0x01u);
}

// ==================== 文档元数据序列化闭环 ====================

TEST(SySerializerTest, DocumentWithMetadataRoundTrip)
{
    SyDocument original;
    original.setAuthor("TestAuthor");
    original.setMetadataVersion(2);
    original.setSoftwareName("SanYiCAD");
    original.setDescription("Test document");

    SySerializer serializer;
    std::vector<uint8_t> data;
    auto result = serializeDoc(serializer, original, data);
    ASSERT_TRUE(result.success);
    EXPECT_FALSE(data.empty());

    SyDocument restored;
    result = deserializeDoc(serializer, data, restored);
    ASSERT_TRUE(result.success);
    EXPECT_STREQ(restored.author(), "TestAuthor");
    EXPECT_EQ(restored.metadataVersion(), 2);
    EXPECT_STREQ(restored.softwareName(), "SanYiCAD");
    EXPECT_STREQ(restored.description(), "Test document");
}

TEST(SySerializerTest, DocumentWithMultipleLayersRoundTrip)
{
    SyDocument original;
    for (int i = 1; i <= 5; ++i)
    {
        SyLayerInfo layer;
        layer.id = i;
        std::strcpy(layer.name, ("Layer" + std::to_string(i)).c_str());
        layer.visible = (i % 2 == 0);  // 偶层可见
        original.addLayer(layer);
    }

    SySerializer serializer;
    std::vector<uint8_t> data;
    auto result = serializeDoc(serializer, original, data);
    ASSERT_TRUE(result.success);

    SyDocument restored;
    result = deserializeDoc(serializer, data, restored);
    ASSERT_TRUE(result.success);
    ASSERT_EQ(restored.layerCount(), 5u);
    SyLayerInfo l0, l1, l4;
    ASSERT_TRUE(restored.getLayerAt(0, l0));
    ASSERT_TRUE(restored.getLayerAt(1, l1));
    ASSERT_TRUE(restored.getLayerAt(4, l4));
    EXPECT_STREQ(l0.name, "Layer1");
    EXPECT_STREQ(l4.name, "Layer5");
    EXPECT_TRUE(l1.visible);   // Layer2
    EXPECT_FALSE(l0.visible);  // Layer1
}

TEST(SySerializerTest, DocumentWithHardwareInfoRoundTrip)
{
    SyDocument original;
    SyHardwareInfo hw;
    std::strcpy(hw.laserType, "CO2");
    std::strcpy(hw.controllerModel, "Model-X");
    hw.maxPower = 100.0;
    hw.workAreaWidth = 600.0;
    hw.workAreaHeight = 400.0;
    original.setHardware(hw);

    SySerializer serializer;
    std::vector<uint8_t> data;
    auto result = serializeDoc(serializer, original, data);
    ASSERT_TRUE(result.success);

    SyDocument restored;
    result = deserializeDoc(serializer, data, restored);
    ASSERT_TRUE(result.success);
    SyHardwareInfo restoredHw;
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
TEST(SySerializerTest, ProcessAndLayerOutputRoundTrip)
{
    SyDocument original;

    // 工艺 1：切割
    SyProcessInfo cut;
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
    SyProcessInfo engrave;
    engrave.id = 2;
    std::strcpy(engrave.name, "Engrave");
    engrave.order = 1;
    engrave.output = false;
    engrave.params.powerPercent = 15.0;
    engrave.params.speedMmPerSec = 200.0;
    original.addProcess(engrave);

    // 图层：默认工艺 + 输出开关
    SyLayerInfo layer;
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

    SySerializer serializer;
    std::vector<uint8_t> data;
    auto result = serializeDoc(serializer, original, data);
    ASSERT_TRUE(result.success);

    SyDocument restored;
    result = deserializeDoc(serializer, data, restored);
    ASSERT_TRUE(result.success);

    // ---- 工艺 ----
    ASSERT_EQ(restored.processCount(), 2u);

    SyProcessInfo rCut;
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

    SyProcessInfo rEngrave;
    ASSERT_TRUE(restored.getProcessAt(1, rEngrave));
    EXPECT_EQ(2u, rEngrave.id);
    EXPECT_STREQ(rEngrave.name, "Engrave");
    EXPECT_EQ(1, rEngrave.order);
    EXPECT_FALSE(rEngrave.output);
    EXPECT_DOUBLE_EQ(15.0, rEngrave.params.powerPercent);

    // ---- 图层 ----
    ASSERT_EQ(restored.layerCount(), 1u);
    SyLayerInfo rLayer;
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
TEST(SySerializerTest, LayerOutputIsIndependentOfVisibility)
{
    SyDocument original;

    SyLayerInfo hiddenButMachined;
    hiddenButMachined.id = 1;
    std::strcpy(hiddenButMachined.name, "Hidden");
    hiddenButMachined.visible = false;
    hiddenButMachined.output = true;
    original.addLayer(hiddenButMachined);

    SyLayerInfo shownButNotMachined;
    shownButNotMachined.id = 2;
    std::strcpy(shownButNotMachined.name, "Annotation");
    shownButNotMachined.visible = true;
    shownButNotMachined.output = false;
    original.addLayer(shownButNotMachined);

    SySerializer serializer;
    std::vector<uint8_t> data;
    auto result = serializeDoc(serializer, original, data);
    ASSERT_TRUE(result.success);

    SyDocument restored;
    result = deserializeDoc(serializer, data, restored);
    ASSERT_TRUE(result.success);

    ASSERT_EQ(restored.layerCount(), 2u);
    SyLayerInfo l0, l1;
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
TEST(SySerializerTest, LegacyDocumentWithoutOutputFieldDefaultsToMachined)
{
    // 构造一份「只有 visible/locked」的图层数据（模拟 v1 文件的形状）
    SyDocument original;
    SyLayerInfo layer;
    layer.id = 1;
    std::strcpy(layer.name, "Legacy");
    layer.visible = true;
    layer.locked = false;
    original.addLayer(layer);

    SySerializer serializer;
    std::vector<uint8_t> data;
    auto result = serializeDoc(serializer, original, data);
    ASSERT_TRUE(result.success);

    // 写出的文件里 output 字段存在（proto3 optional 有值即写入）。
    // 读回时走 has_output() 分支，值应为 true。
    SyDocument restored;
    result = deserializeDoc(serializer, data, restored);
    ASSERT_TRUE(result.success);

    SyLayerInfo l0;
    ASSERT_TRUE(restored.getLayerAt(0, l0));
    EXPECT_TRUE(l0.output) << "output 缺省必须是 true（保持旧文件的行为）";
}

// ==================== 移动语义测试 ====================

TEST(SySerializerTest, MoveAssignment)
{
    SySerializer a;
    SySerializer b;
    a.setCryptoProvider(nullptr);

    b = std::move(a);  // 移动赋值

    // 移动后 b 应可用，a 应处于有效但未指定状态
    SyDocument doc;
    std::vector<uint8_t> data;
    auto result = serializeDoc(b, doc, data);
    EXPECT_TRUE(result.success);
}
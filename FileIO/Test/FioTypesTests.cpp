#include <gtest/gtest.h>
#include "FileIO/FioTypes.h"
#include <type_traits>

// ==================== POD 合规性验证 ====================

TEST(FioTypesTest, IrLayerInfoIsPOD)
{
    EXPECT_TRUE(std::is_trivially_copyable_v<Fio::IrLayerInfo>);
    EXPECT_TRUE(std::is_standard_layout_v<Fio::IrLayerInfo>);
}

TEST(FioTypesTest, EntityInfoIsPOD)
{
    EXPECT_TRUE(std::is_trivially_copyable_v<Fio::EntityInfo>);
    EXPECT_TRUE(std::is_standard_layout_v<Fio::EntityInfo>);
}

TEST(FioTypesTest, BinaryBlobIsPOD)
{
    EXPECT_TRUE(std::is_trivially_copyable_v<Fio::BinaryBlob>);
    EXPECT_TRUE(std::is_standard_layout_v<Fio::BinaryBlob>);
}

// ==================== IrLayerInfo 默认构造 ====================

TEST(IrLayerInfoTest, DefaultValues)
{
    Fio::IrLayerInfo layer;
    EXPECT_EQ(layer.sourceId, 0u);
    EXPECT_EQ(layer.name[0], '\0');
    EXPECT_EQ(layer.color, 0xFF000000u);
    EXPECT_TRUE(layer.visible);
    EXPECT_FALSE(layer.locked);
}

TEST(IrLayerInfoTest, NameBufferSize)
{
    Fio::IrLayerInfo layer;
    // 确保缓冲区足够容纳合理长度的图层名
    constexpr size_t expectedSize = 256;
    EXPECT_EQ(sizeof(layer.name), expectedSize);
}

TEST(IrLayerInfoTest, CopyAssignment)
{
    Fio::IrLayerInfo a;
    a.sourceId = 42;
    a.color = 0x00FF0000;
    a.visible = false;
    a.locked = true;
    std::strncpy(a.name, "TestLayer", sizeof(a.name));

    Fio::IrLayerInfo b = a;
    EXPECT_EQ(b.sourceId, 42u);
    EXPECT_EQ(b.color, 0x00FF0000u);
    EXPECT_FALSE(b.visible);
    EXPECT_TRUE(b.locked);
    EXPECT_EQ(std::string(b.name), "TestLayer");
}

// ==================== EntityInfo 默认构造 ====================

TEST(EntityInfoTest, DefaultValues)
{
    Fio::EntityInfo entity;
    EXPECT_EQ(entity.sourceId, 0u);
    EXPECT_EQ(entity.type, Fio::EntityType::Unknown);
    EXPECT_EQ(entity.name[0], '\0');
    EXPECT_EQ(entity.layerSourceId, 0u);
    EXPECT_DOUBLE_EQ(entity.lineWidth, 1.0);
    EXPECT_TRUE(entity.visible);
    EXPECT_FALSE(entity.locked);
}

TEST(EntityInfoTest, LineGeometry)
{
    Fio::EntityInfo entity;
    entity.type = Fio::EntityType::Line;
    entity.line.x1 = 0.0; entity.line.y1 = 0.0;
    entity.line.x2 = 100.0; entity.line.y2 = 200.0;

    EXPECT_DOUBLE_EQ(entity.line.x1, 0.0);
    EXPECT_DOUBLE_EQ(entity.line.y1, 0.0);
    EXPECT_DOUBLE_EQ(entity.line.x2, 100.0);
    EXPECT_DOUBLE_EQ(entity.line.y2, 200.0);
}

TEST(EntityInfoTest, CircleGeometry)
{
    Fio::EntityInfo entity;
    entity.type = Fio::EntityType::Circle;
    entity.circle.cx = 50.0;
    entity.circle.cy = 60.0;
    entity.circle.r = 25.0;

    EXPECT_DOUBLE_EQ(entity.circle.cx, 50.0);
    EXPECT_DOUBLE_EQ(entity.circle.cy, 60.0);
    EXPECT_DOUBLE_EQ(entity.circle.r, 25.0);
}

TEST(EntityInfoTest, ArcGeometry)
{
    Fio::EntityInfo entity;
    entity.type = Fio::EntityType::Arc;
    entity.arc.cx = 10.0;
    entity.arc.cy = 20.0;
    entity.arc.r = 30.0;
    entity.arc.sa = 0.0;
    entity.arc.ea = 1.5708;

    EXPECT_DOUBLE_EQ(entity.arc.cx, 10.0);
    EXPECT_DOUBLE_EQ(entity.arc.cy, 20.0);
    EXPECT_DOUBLE_EQ(entity.arc.r, 30.0);
    EXPECT_DOUBLE_EQ(entity.arc.sa, 0.0);
    EXPECT_DOUBLE_EQ(entity.arc.ea, 1.5708);
}

TEST(EntityInfoTest, EllipseGeometry)
{
    Fio::EntityInfo entity;
    entity.type = Fio::EntityType::Ellipse;
    entity.ellipse.cx = 0.0;
    entity.ellipse.cy = 0.0;
    entity.ellipse.rx = 50.0;
    entity.ellipse.ry = 30.0;
    entity.ellipse.rot = 0.0;
    entity.ellipse.sa = 0.0;
    entity.ellipse.ea = 6.2832;

    EXPECT_DOUBLE_EQ(entity.ellipse.cx, 0.0);
    EXPECT_DOUBLE_EQ(entity.ellipse.cy, 0.0);
    EXPECT_DOUBLE_EQ(entity.ellipse.rx, 50.0);
    EXPECT_DOUBLE_EQ(entity.ellipse.ry, 30.0);
    EXPECT_DOUBLE_EQ(entity.ellipse.rot, 0.0);
}

TEST(EntityInfoTest, TextGeometry)
{
    Fio::EntityInfo entity;
    entity.type = Fio::EntityType::Text;
    entity.text.x = 100.0;
    entity.text.y = 200.0;
    std::strncpy(entity.text.text, "Hello World", sizeof(entity.text.text));
    entity.text.h = 12.0;
    entity.text.a = 0.0;

    EXPECT_DOUBLE_EQ(entity.text.x, 100.0);
    EXPECT_DOUBLE_EQ(entity.text.y, 200.0);
    EXPECT_EQ(std::string(entity.text.text), "Hello World");
    EXPECT_DOUBLE_EQ(entity.text.h, 12.0);
    EXPECT_DOUBLE_EQ(entity.text.a, 0.0);
}

TEST(EntityInfoTest, BezierGeometry)
{
    Fio::EntityInfo entity;
    entity.type = Fio::EntityType::Bezier;
    entity.bezier.c0x = 0.0; entity.bezier.c0y = 0.0;
    entity.bezier.c1x = 50.0; entity.bezier.c1y = 100.0;
    entity.bezier.ex = 100.0; entity.bezier.ey = 0.0;

    EXPECT_DOUBLE_EQ(entity.bezier.c0x, 0.0);
    EXPECT_DOUBLE_EQ(entity.bezier.c1x, 50.0);
    EXPECT_DOUBLE_EQ(entity.bezier.ex, 100.0);
    EXPECT_DOUBLE_EQ(entity.bezier.ey, 0.0);
}

// ==================== EntityType 枚举 ====================

TEST(EntityTypeTest, EnumValues)
{
    EXPECT_EQ(static_cast<int>(Fio::EntityType::Line), 0);
    EXPECT_EQ(static_cast<int>(Fio::EntityType::Arc), 1);
    EXPECT_EQ(static_cast<int>(Fio::EntityType::Circle), 2);
    EXPECT_EQ(static_cast<int>(Fio::EntityType::Ellipse), 3);
    EXPECT_NE(static_cast<int>(Fio::EntityType::Unknown),
              static_cast<int>(Fio::EntityType::Mesh3D));
}

// ==================== BinaryBlob ====================

TEST(BinaryBlobTest, DefaultValues)
{
    Fio::BinaryBlob blob;
    EXPECT_EQ(blob.data, nullptr);
    EXPECT_EQ(blob.size, 0u);
}

TEST(BinaryBlobTest, CopyAssignment)
{
    uint8_t dummyData[] = { 0x01, 0x02, 0x03 };
    Fio::BinaryBlob a{ dummyData, 3 };
    Fio::BinaryBlob b = a;  // shallow POD copy — intentional
    EXPECT_EQ(b.data, dummyData);
    EXPECT_EQ(b.size, 3u);
}

// ==================== 跨 DLL 安全验证 ====================

TEST(FioTypesTest, IrLayerInfoSize)
{
    // IrLayerInfo 必须是固定大小，以确保跨 DLL 边界时的布局一致
    // 结构体: uint32_t(4) + char[256](256) + uint32_t(4) + bool(1) + bool(1) = 266 + padding
    constexpr size_t expectedMinSize = 256u + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(bool) + sizeof(bool);
    EXPECT_GE(sizeof(Fio::IrLayerInfo), expectedMinSize);
    // 对齐填充后不应超过 272 字节
    EXPECT_LE(sizeof(Fio::IrLayerInfo), 272u);
}

TEST(FioTypesTest, BinaryBlobSize)
{
    EXPECT_EQ(sizeof(Fio::BinaryBlob), sizeof(uint8_t*) + sizeof(size_t));
}

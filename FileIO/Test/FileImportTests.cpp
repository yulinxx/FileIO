#include <gtest/gtest.h>
#include "FileIO/FileImporter.h"
#include "FileIO/FileFormat.h"

#include <string>
#include <filesystem>

// ==================== FileImporter 构造与析构 ====================

TEST(FileImporterTest, DefaultConstruction)
{
    Fio::FileImporter importer;
    EXPECT_EQ(importer.LayerCount(), 0);
    EXPECT_EQ(importer.EntityCount(), 0);
    EXPECT_NE(importer.LastError(), nullptr);
}

TEST(FileImporterTest, DestructorDoesNotCrash)
{
    auto* importer = new Fio::FileImporter();
    delete importer;
}

// ==================== 文件打开与错误处理 ====================

TEST(FileImporterTest, OpenNonExistentFile)
{
    Fio::FileImporter importer;
    bool ok = importer.Open("/nonexistent/path/file.dxf");
    EXPECT_FALSE(ok);
    std::string err = importer.LastError();
    EXPECT_FALSE(err.empty());
}

TEST(FileImporterTest, OpenEmptyPath)
{
    Fio::FileImporter importer;
    bool ok = importer.Open("");
    EXPECT_FALSE(ok);
}

TEST(FileImporterTest, OpenNullPath)
{
    Fio::FileImporter importer;
    bool ok = importer.Open(nullptr);
    EXPECT_FALSE(ok);
}

TEST(FileImporterTest, OpenDirectoryPath)
{
    Fio::FileImporter importer;
    std::string dirPath = std::filesystem::temp_directory_path().string();
    bool ok = importer.Open(dirPath.c_str());
    EXPECT_FALSE(ok);
}

// ==================== 逐条查询接口（空状态） ====================

TEST(FileImporterTest, GetLayerOutOfRange)
{
    Fio::FileImporter importer;
    Fio::IrLayerInfo layer;
    bool ok = importer.GetLayer(0, &layer);
    EXPECT_FALSE(ok);
}

TEST(FileImporterTest, GetLayerNullOutput)
{
    Fio::FileImporter importer;
    bool ok = importer.GetLayer(0, nullptr);
    EXPECT_FALSE(ok);
}

TEST(FileImporterTest, GetEntityOutOfRange)
{
    Fio::FileImporter importer;
    Fio::EntityInfo entity;
    bool ok = importer.GetEntity(0, &entity);
    EXPECT_FALSE(ok);
}

TEST(FileImporterTest, GetEntityNullOutput)
{
    Fio::FileImporter importer;
    bool ok = importer.GetEntity(0, nullptr);
    EXPECT_FALSE(ok);
}

// ==================== Blob 接口 ====================

TEST(FileImporterTest, ExportBlobOnEmptyImporter)
{
    Fio::FileImporter importer;
    Fio::BinaryBlob blob = importer.ExportBlob();
    EXPECT_EQ(blob.data, nullptr);
    EXPECT_EQ(blob.size, 0u);
}

TEST(FileImporterTest, FreeNullBlob)
{
    // FreeBlob(nullptr) 必须不崩溃
    Fio::FileImporter::FreeBlob(nullptr);
}

TEST(FileImporterTest, FreeEmptyBlob)
{
    Fio::BinaryBlob blob{ nullptr, 0 };
    Fio::FileImporter::FreeBlob(&blob);
    EXPECT_EQ(blob.data, nullptr);
    EXPECT_EQ(blob.size, 0u);
}

// ==================== Open + 重新 Open 的幂等性 ====================

TEST(FileImporterTest, OpenAfterFailedOpenResetsState)
{
    Fio::FileImporter importer;
    importer.Open("/nonexistent/file.dxf");
    int layerCount = importer.LayerCount();
    int entityCount = importer.EntityCount();
    EXPECT_EQ(layerCount, 0);
    EXPECT_EQ(entityCount, 0);
}

// ==================== FileFormat 枚举 ====================

TEST(FileFormatTest, KnownFormatsAreUnique)
{
    EXPECT_NE(static_cast<int>(Fio::FileFormat::Unknown),
              static_cast<int>(Fio::FileFormat::DXF));
    EXPECT_NE(static_cast<int>(Fio::FileFormat::DXF),
              static_cast<int>(Fio::FileFormat::SVG));
    EXPECT_NE(static_cast<int>(Fio::FileFormat::SVG),
              static_cast<int>(Fio::FileFormat::Native));
}

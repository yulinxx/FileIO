#include <gtest/gtest.h>
#include "FileIO/ImageUtils.h"
#include "FileIOUtils.h"

#include <string>
#include <fstream>
#include <filesystem>
#include <regex>
#include <vector>

// ==================== generateHash 哈希生成测试 ====================

TEST(GenerateHashTest, EmptyString)
{
    std::string hash = Fio::generateHash("");
    EXPECT_FALSE(hash.empty());
    EXPECT_EQ(hash.length(), 8u);
}

TEST(GenerateHashTest, SimpleString)
{
    std::string hash = Fio::generateHash("Hello, World!");
    EXPECT_EQ(hash.length(), 8u);
}

TEST(GenerateHashTest, ChineseString)
{
    std::string hash = Fio::generateHash(u8"你好，世界！");
    EXPECT_EQ(hash.length(), 8u);
}

TEST(GenerateHashTest, LongString)
{
    std::string longStr(10000, 'A');
    std::string hash = Fio::generateHash(longStr);
    EXPECT_EQ(hash.length(), 8u);
}

TEST(GenerateHashTest, SameStringSameHash)
{
    std::string str = "Test String 12345";
    std::string hash1 = Fio::generateHash(str);
    std::string hash2 = Fio::generateHash(str);
    EXPECT_EQ(hash1, hash2);
}

TEST(GenerateHashTest, DifferentStringDifferentHash)
{
    std::string str1 = "String A";
    std::string str2 = "String B";
    std::string hash1 = Fio::generateHash(str1);
    std::string hash2 = Fio::generateHash(str2);
    EXPECT_NE(hash1, hash2);
}

TEST(GenerateHashTest, HashFormatHexadecimal)
{
    std::string hash = Fio::generateHash("Test for format");
    EXPECT_EQ(hash.length(), 8u);
    std::regex hexPattern("^[0-9a-f]{8}$");
    EXPECT_TRUE(std::regex_match(hash, hexPattern));
}

// ==================== pixelsToUnit 像素转单位测试 ====================

TEST(PixelsToUnitTest, MillimeterConversion)
{
    float result = Fio::pixelsToUnit(96, 96.0f, Fio::UnitType::Millimeter);
    EXPECT_FLOAT_EQ(result, 25.4f);
}

TEST(PixelsToUnitTest, InchConversion)
{
    float result = Fio::pixelsToUnit(96, 96.0f, Fio::UnitType::Inch);
    EXPECT_FLOAT_EQ(result, 1.0f);
}

TEST(PixelsToUnitTest, PixelConversion)
{
    float result = Fio::pixelsToUnit(100, 96.0f, Fio::UnitType::Pixel);
    EXPECT_FLOAT_EQ(result, 100.0f);
}

TEST(PixelsToUnitTest, ZeroPixels)
{
    float mmResult = Fio::pixelsToUnit(0, 96.0f, Fio::UnitType::Millimeter);
    float inchResult = Fio::pixelsToUnit(0, 96.0f, Fio::UnitType::Inch);
    float pixelResult = Fio::pixelsToUnit(0, 96.0f, Fio::UnitType::Pixel);
    EXPECT_FLOAT_EQ(mmResult, 0.0f);
    EXPECT_FLOAT_EQ(inchResult, 0.0f);
    EXPECT_FLOAT_EQ(pixelResult, 0.0f);
}

TEST(PixelsToUnitTest, NegativePixels)
{
    float mmResult = Fio::pixelsToUnit(-96, 96.0f, Fio::UnitType::Millimeter);
    float inchResult = Fio::pixelsToUnit(-96, 96.0f, Fio::UnitType::Inch);
    EXPECT_FLOAT_EQ(mmResult, -25.4f);
    EXPECT_FLOAT_EQ(inchResult, -1.0f);
}

TEST(PixelsToUnitTest, Standard96DPI)
{
    float result = Fio::pixelsToUnit(96, 96.0f, Fio::UnitType::Millimeter);
    EXPECT_FLOAT_EQ(result, 25.4f);
}

TEST(PixelsToUnitTest, CustomDPI)
{
    float result72 = Fio::pixelsToUnit(72, 72.0f, Fio::UnitType::Millimeter);
    float result300 = Fio::pixelsToUnit(300, 300.0f, Fio::UnitType::Millimeter);
    EXPECT_FLOAT_EQ(result72, 25.4f);
    EXPECT_FLOAT_EQ(result300, 25.4f);
}

TEST(PixelsToUnitTest, InvalidDPI)
{
    float resultZero = Fio::pixelsToUnit(96, 0.0f, Fio::UnitType::Millimeter);
    float resultNegative = Fio::pixelsToUnit(96, -10.0f, Fio::UnitType::Millimeter);
    EXPECT_FLOAT_EQ(resultZero, 25.4f);
    EXPECT_FLOAT_EQ(resultNegative, 25.4f);
}

TEST(PixelsToUnitTest, DefaultUnitType)
{
    float result = Fio::pixelsToUnit(96, 96.0f);
    EXPECT_FLOAT_EQ(result, 25.4f);
}

// ==================== inchToMm 英寸转毫米测试 ====================

TEST(InchToMmTest, StandardValue)
{
    EXPECT_FLOAT_EQ(Fio::inchToMm(1.0f), 25.4f);
}

TEST(InchToMmTest, ZeroValue)
{
    EXPECT_FLOAT_EQ(Fio::inchToMm(0.0f), 0.0f);
}

TEST(InchToMmTest, NegativeValue)
{
    EXPECT_FLOAT_EQ(Fio::inchToMm(-1.0f), -25.4f);
}

TEST(InchToMmTest, LargeValue)
{
    EXPECT_FLOAT_EQ(Fio::inchToMm(1000.0f), 25400.0f);
}

TEST(InchToMmTest, SmallValue)
{
    EXPECT_FLOAT_EQ(Fio::inchToMm(0.001f), 0.0254f);
}

// ==================== mmToInch 毫米转英寸测试 ====================

TEST(MmToInchTest, StandardValue)
{
    EXPECT_FLOAT_EQ(Fio::mmToInch(25.4f), 1.0f);
}

TEST(MmToInchTest, ZeroValue)
{
    EXPECT_FLOAT_EQ(Fio::mmToInch(0.0f), 0.0f);
}

TEST(MmToInchTest, NegativeValue)
{
    EXPECT_FLOAT_EQ(Fio::mmToInch(-25.4f), -1.0f);
}

TEST(MmToInchTest, InverseOfInchToMm)
{
    float testValue = 123.456f;
    float mm = Fio::inchToMm(testValue);
    float inch = Fio::mmToInch(mm);
    EXPECT_NEAR(inch, testValue, 0.0001f);
}

TEST(MmToInchTest, LargeValue)
{
    EXPECT_FLOAT_EQ(Fio::mmToInch(25400.0f), 1000.0f);
}

// ==================== TempFileCopy 临时文件复制测试 ====================

TEST(TempFileCopyTest, ValidFileCopy)
{
    std::string tempDir = std::filesystem::temp_directory_path().string();
    std::string testFile = tempDir + "/fio_test_original.txt";

    {
        std::ofstream outFile(testFile, std::ios::binary);
        ASSERT_TRUE(outFile.is_open());
        outFile << "Test content for TempFileCopy";
    }

    {
        Fio::TempFileCopy tempCopy(testFile, "unittest");
        EXPECT_TRUE(tempCopy.isValid());
        EXPECT_TRUE(tempCopy.error().empty());
        EXPECT_NE(tempCopy.path(), testFile);

        std::ifstream inFile(tempCopy.path(), std::ios::binary);
        ASSERT_TRUE(inFile.is_open());
        std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
        EXPECT_EQ(content, "Test content for TempFileCopy");
    }

    std::filesystem::remove(testFile);
}

TEST(TempFileCopyTest, InvalidFilePath)
{
    Fio::TempFileCopy tempCopy("/nonexistent/path/file.txt", "unittest");
    EXPECT_FALSE(tempCopy.isValid());
    EXPECT_FALSE(tempCopy.error().empty());
}

TEST(TempFileCopyTest, TempFileDeletedOnDestruction)
{
    std::string tempDir = std::filesystem::temp_directory_path().string();
    std::string testFile = tempDir + "/fio_test_delete.txt";
    std::string tempFilePath;

    {
        std::ofstream outFile(testFile, std::ios::binary);
        ASSERT_TRUE(outFile.is_open());
        outFile << "Delete test";
    }

    {
        Fio::TempFileCopy tempCopy(testFile, "deletetest");
        ASSERT_TRUE(tempCopy.isValid());
        tempFilePath = tempCopy.path();
        EXPECT_TRUE(std::filesystem::exists(tempFilePath));
    }

    EXPECT_FALSE(std::filesystem::exists(tempFilePath));

    std::filesystem::remove(testFile);
}

TEST(TempFileCopyTest, CopyPreservesBinaryContent)
{
    std::string tempDir = std::filesystem::temp_directory_path().string();
    std::string testFile = tempDir + "/fio_test_binary.bin";

    std::vector<uint8_t> binaryData = { 0x00, 0x01, 0xFF, 0xFE, 0x80, 0x7F, 0x00, 0x00 };

    {
        std::ofstream outFile(testFile, std::ios::binary);
        ASSERT_TRUE(outFile.is_open());
        outFile.write(reinterpret_cast<const char*>(binaryData.data()), binaryData.size());
    }

    {
        Fio::TempFileCopy tempCopy(testFile, "binarytest");
        ASSERT_TRUE(tempCopy.isValid());

        std::ifstream inFile(tempCopy.path(), std::ios::binary);
        ASSERT_TRUE(inFile.is_open());
        std::vector<uint8_t> copiedData((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
        EXPECT_EQ(copiedData.size(), binaryData.size());
        for (size_t i = 0; i < binaryData.size(); ++i)
        {
            EXPECT_EQ(copiedData[i], binaryData[i]);
        }
    }

    std::filesystem::remove(testFile);
}

TEST(TempFileCopyTest, ContentConstructorWritesProvidedContent)
{
    std::string originalPath = std::filesystem::temp_directory_path().string() + "/fio_test_orig.txt";
    const std::string provided = "preprocessed\x0Acontent\x80\x0A";

    {
        Fio::TempFileCopy tempCopy(provided, originalPath, "contenttest");
        ASSERT_TRUE(tempCopy.isValid());
        EXPECT_TRUE(tempCopy.error().empty());

        std::ifstream inFile(tempCopy.path(), std::ios::binary);
        ASSERT_TRUE(inFile.is_open());
        std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
        EXPECT_EQ(content, provided);
    }
}

// ==================== 图片 DPI 元数据解析测试 ====================

namespace
{
    std::string writeTempImageFile(const std::string& fileName, const std::vector<unsigned char>& bytes)
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / fileName;
        std::ofstream out(path, std::ios::binary);
        if (!out.is_open())
        {
            return {};
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();
        return path.string();
    }

    // 2x2 灰度 PNG，pHYs = 11811 px/m（300 DPI）
    const std::vector<unsigned char> kPngWith300Dpi = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
        0x08, 0x00, 0x00, 0x00, 0x00, 0x57, 0xDD, 0x52, 0xF8, 0x00, 0x00, 0x00,
        0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00, 0x2E, 0x23, 0x00, 0x00, 0x2E,
        0x23, 0x01, 0x78, 0xA5, 0x3F, 0x76, 0x00, 0x00, 0x00, 0x0E, 0x49, 0x44,
        0x41, 0x54, 0x78, 0x9C, 0x63, 0x6C, 0x60, 0x60, 0x62, 0x60, 0x00, 0x00,
        0x02, 0x92, 0x00, 0x84, 0xF7, 0x7C, 0xA3, 0x58, 0x00, 0x00, 0x00, 0x00,
        0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
    };

    // 同一张 2x2 PNG，但没有 pHYs 块
    const std::vector<unsigned char> kPngWithoutDpi = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
        0x08, 0x00, 0x00, 0x00, 0x00, 0x57, 0xDD, 0x52, 0xF8, 0x00, 0x00, 0x00,
        0x0E, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x6C, 0x60, 0x60, 0x62,
        0x60, 0x00, 0x00, 0x02, 0x92, 0x00, 0x84, 0xF7, 0x7C, 0xA3, 0x58, 0x00,
        0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
    };

    // 2x2 JPEG，JFIF APP0 units=1，X/Y density=300 DPI
    const std::vector<unsigned char> kJpegWith300Dpi = {
        0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01,
        0x01, 0x01, 0x01, 0x2C, 0x01, 0x2C, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43,
        0x00, 0x03, 0x02, 0x02, 0x03, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x04,
        0x03, 0x03, 0x04, 0x05, 0x08, 0x05, 0x05, 0x04, 0x04, 0x05, 0x0A, 0x07,
        0x07, 0x06, 0x08, 0x0C, 0x0A, 0x0C, 0x0C, 0x0B, 0x0A, 0x0B, 0x0B, 0x0D,
        0x0E, 0x12, 0x10, 0x0D, 0x0E, 0x11, 0x0E, 0x0B, 0x0B, 0x10, 0x16, 0x10,
        0x11, 0x13, 0x14, 0x15, 0x15, 0x15, 0x0C, 0x0F, 0x17, 0x18, 0x16, 0x14,
        0x18, 0x12, 0x14, 0x15, 0x14, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x02,
        0x00, 0x02, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4, 0x00, 0x1F, 0x00, 0x00,
        0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0xFF, 0xC4, 0x00, 0xB5, 0x10, 0x00, 0x02, 0x01, 0x03,
        0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D,
        0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06,
        0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08,
        0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72,
        0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
        0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45,
        0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
        0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75,
        0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
        0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3,
        0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
        0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9,
        0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
        0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4,
        0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01,
        0x00, 0x00, 0x3F, 0x00, 0x2B, 0xFF, 0xD9
    };
}  // namespace

TEST(ImageInfoDpiTest, PngPhysChunkProvidesRealDpi)
{
    const std::string path = writeTempImageFile("sanyi_dpi_300.png", kPngWith300Dpi);
    ASSERT_FALSE(path.empty());

    const Fio::ImageInfo info = Fio::readImageInfo(path.c_str());
    EXPECT_TRUE(info.isValid);
    EXPECT_TRUE(info.dpiValid);
    EXPECT_NEAR(info.dpiX, 300.0f, 0.01f);
    EXPECT_NEAR(info.dpiY, 300.0f, 0.01f);

    // 1000 px @ 300 DPI = 84.67 mm，而不是 96 DPI 兜底的 264.58 mm
    EXPECT_NEAR(Fio::pixelsToUnit(1000, info.dpiX, Fio::UnitType::Millimeter), 84.6667f, 0.01f);

    std::filesystem::remove(path);
}

TEST(ImageInfoDpiTest, PngWithoutPhysIsMarkedAsNoRealDpi)
{
    const std::string path = writeTempImageFile("sanyi_dpi_none.png", kPngWithoutDpi);
    ASSERT_FALSE(path.empty());

    const Fio::ImageInfo info = Fio::readImageInfo(path.c_str());
    EXPECT_TRUE(info.isValid);
    EXPECT_FALSE(info.dpiValid);
    EXPECT_FLOAT_EQ(info.dpiX, 0.0f);
    EXPECT_FLOAT_EQ(info.dpiY, 0.0f);

    // 调用方仍可显式使用 96 DPI 兜底，但必须能区分这不是文件里的真实值
    EXPECT_NEAR(Fio::pixelsToUnit(1000, info.dpiX, Fio::UnitType::Millimeter), 264.5833f, 0.01f);

    std::filesystem::remove(path);
}

TEST(ImageInfoDpiTest, JpegJfifDensityProvidesRealDpi)
{
    const std::string path = writeTempImageFile("sanyi_dpi_300.jpg", kJpegWith300Dpi);
    ASSERT_FALSE(path.empty());

    const Fio::ImageInfo info = Fio::readImageInfo(path.c_str());
    EXPECT_TRUE(info.isValid);
    EXPECT_TRUE(info.dpiValid);
    EXPECT_NEAR(info.dpiX, 300.0f, 0.01f);
    EXPECT_NEAR(info.dpiY, 300.0f, 0.01f);

    std::filesystem::remove(path);
}

TEST(ImageInfoDpiTest, JpegAspectRatioOnlyIsNotTreatedAsDpi)
{
    // JFIF units=0 只描述像素宽高比，即使 density 字段有值也不是物理 DPI
    std::vector<unsigned char> jpeg = kJpegWith300Dpi;
    ASSERT_GT(jpeg.size(), 13u);
    jpeg[13] = 0x00;   // APP0 payload: units

    const std::string path = writeTempImageFile("sanyi_dpi_aspect.jpg", jpeg);
    ASSERT_FALSE(path.empty());

    const Fio::ImageInfo info = Fio::readImageInfo(path.c_str());
    EXPECT_TRUE(info.isValid);
    EXPECT_FALSE(info.dpiValid);
    EXPECT_FLOAT_EQ(info.dpiX, 0.0f);
    EXPECT_FLOAT_EQ(info.dpiY, 0.0f);

    std::filesystem::remove(path);
}
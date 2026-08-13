#include <gtest/gtest.h>
#include "FileIO/FileIOUtils.h"
#include "FileIO/ImageUtils.h"

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

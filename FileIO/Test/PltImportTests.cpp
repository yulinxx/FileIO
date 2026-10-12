// PLT(HPGL) 导入测试：按笔号（SP）建图层
#include "FileIO/FileIOManager.h"
#include "FileIO/FioTypes.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace
{
    void writeTempFile(const char* path, const std::string& content)
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.is_open()) << path;
        out << content;
    }

    // 找出名为 name 的图层，返回 sourceId；找不到返回 0
    uint32_t findLayer(const Fio::FioParseResult& r, const char* name)
    {
        for (uint32_t i = 0; i < r.layerCount; ++i)
        {
            if (std::string(r.layers[i].name) == name)
            {
                return r.layers[i].sourceId;
            }
        }
        return 0;
    }
}  // namespace

TEST(PltImportTest, PensCreateLayers)
{
    const auto path = std::filesystem::temp_directory_path() / "sanyi_plt_pens.plt";

    // 两笔两段折线：Pen1 一段，Pen2 一段
    const char* kHpgl =
        "IN;SP1;\n"
        "PU;PA100,100;PD;PA400,100;PA400,400;\n"
        "SP2;\n"
        "PU;PA600,100;PD;PA900,100;PA900,400;\n";

    writeTempFile(path.string().c_str(), kHpgl);

    Fio::FileIOManager mgr;
    char err[512] = {};
    Fio::FioParseResult r{};
    ASSERT_TRUE(mgr.importToIR(path.string().c_str(), Fio::FileFormat::PLT, &r, err, sizeof(err))) << err;

    // 两个笔图层
    ASSERT_EQ(r.layerCount, 2u);
    const uint32_t pen1 = findLayer(r, "Pen 1");
    const uint32_t pen2 = findLayer(r, "Pen 2");
    ASSERT_NE(pen1, 0u);
    ASSERT_NE(pen2, 0u);
    ASSERT_NE(pen1, pen2);
    EXPECT_NE(r.layers[pen1 - 1].color, 0u);  // 每笔一个确定颜色
    EXPECT_TRUE(r.layers[pen1 - 1].visible);

    // 两条折线各归其笔
    ASSERT_EQ(r.entityCount, 2u);
    EXPECT_EQ(r.entities[0].type, Fio::EntityType::Polyline);
    EXPECT_EQ(r.entities[0].layerSourceId, pen1);
    EXPECT_EQ(r.entities[1].type, Fio::EntityType::Polyline);
    EXPECT_EQ(r.entities[1].layerSourceId, pen2);

    EXPECT_EQ(std::string(r.sourceFormat), "PLT");

    std::filesystem::remove(path);
}

TEST(PltImportTest, SinglePenAllOneLayer)
{
    const auto path = std::filesystem::temp_directory_path() / "sanyi_plt_single.plt";

    const char* kHpgl =
        "IN;SP3;\n"
        "PU;PA0,0;PD;PA100,0;PA100,100;\n";

    writeTempFile(path.string().c_str(), kHpgl);

    Fio::FileIOManager mgr;
    char err[512] = {};
    Fio::FioParseResult r{};
    ASSERT_TRUE(mgr.importToIR(path.string().c_str(), Fio::FileFormat::PLT, &r, err, sizeof(err))) << err;

    ASSERT_EQ(r.layerCount, 1u);
    const uint32_t pen3 = findLayer(r, "Pen 3");
    ASSERT_NE(pen3, 0u);
    ASSERT_EQ(r.entityCount, 1u);
    EXPECT_EQ(r.entities[0].layerSourceId, pen3);

    std::filesystem::remove(path);
}
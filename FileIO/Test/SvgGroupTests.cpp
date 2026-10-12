// SVG <g> 群组导入测试：nanosvg 会压平群组层级，SvgParser 预扫描重建 groupSourceId
#include "FileIO/FileIOManager.h"
#include "FileIO/FioTypes.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace
{
    void writeSvg(const std::filesystem::path& path, const std::string& content)
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.is_open()) << path;
        out << content;
    }

    uint64_t findGroup(const Fio::FioParseResult& r, const char* name)
    {
        for (uint32_t i = 0; i < r.groupCount; ++i)
        {
            if (std::string(r.groups[i].name) == name)
            {
                return r.groups[i].sourceId;
            }
        }
        return 0;
    }
}  // namespace

TEST(SvgGroupTest, GroupWithIdAssignsChildren)
{
    const auto path = std::filesystem::temp_directory_path() / "sanyi_svg_group.svg";
    const char* kSvg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">\n"
        "<g id=\"layerA\" fill=\"none\" stroke=\"#000000\">\n"
        "  <path d=\"M10,10 L90,10 L90,90\"/>\n"
        "  <path d=\"M20,20 L80,20 L80,80\"/>\n"
        "</g>\n"
        "</svg>\n";
    writeSvg(path, kSvg);

    Fio::FileIOManager mgr;
    char err[512] = {};
    Fio::FioParseResult r{};
    ASSERT_TRUE(mgr.importToIR(path.string().c_str(), Fio::FileFormat::SVG, &r, err, sizeof(err))) << err;
    std::filesystem::remove(path);

    ASSERT_EQ(r.entityCount, 2u);
    ASSERT_EQ(r.groupCount, 1u);
    EXPECT_STREQ(r.groups[0].name, "layerA");
    EXPECT_EQ(r.groups[0].parentSourceId, 0u);

    const uint64_t g = findGroup(r, "layerA");
    ASSERT_NE(g, 0u);
    EXPECT_EQ(r.entities[0].groupSourceId, g);
    EXPECT_EQ(r.entities[1].groupSourceId, g);
}

TEST(SvgGroupTest, NestedGroupsBuildHierarchy)
{
    const auto path = std::filesystem::temp_directory_path() / "sanyi_svg_nested.svg";
    const char* kSvg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">\n"
        "<g id=\"outer\" fill=\"none\" stroke=\"#000000\">\n"
        "  <g id=\"inner\">\n"
        "    <path d=\"M10,10 L90,10 L90,90\"/>\n"
        "  </g>\n"
        "  <path d=\"M20,20 L80,20 L80,80\"/>\n"
        "</g>\n"
        "</svg>\n";
    writeSvg(path, kSvg);

    Fio::FileIOManager mgr;
    char err[512] = {};
    Fio::FioParseResult r{};
    ASSERT_TRUE(mgr.importToIR(path.string().c_str(), Fio::FileFormat::SVG, &r, err, sizeof(err))) << err;
    std::filesystem::remove(path);

    ASSERT_EQ(r.entityCount, 2u);
    ASSERT_EQ(r.groupCount, 2u);

    const uint64_t outer = findGroup(r, "outer");
    const uint64_t inner = findGroup(r, "inner");
    ASSERT_NE(outer, 0u);
    ASSERT_NE(inner, 0u);

    // inner 的父群组是 outer
    for (uint32_t i = 0; i < r.groupCount; ++i)
    {
        if (r.groups[i].sourceId == inner)
        {
            EXPECT_EQ(r.groups[i].parentSourceId, outer);
        }
    }

    // 顺序：inner 里的路径在前（shape->id 继承自 inner），outer 直属路径在后
    EXPECT_EQ(r.entities[0].groupSourceId, inner);
    EXPECT_EQ(r.entities[1].groupSourceId, outer);
}

TEST(SvgGroupTest, InkscapeLabelUsedAsGroupName)
{
    const auto path = std::filesystem::temp_directory_path() / "sanyi_svg_label.svg";
    const char* kSvg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        "xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\" viewBox=\"0 0 100 100\">\n"
        "<g id=\"g123\" inkscape:label=\"Cut Layer\" fill=\"none\" stroke=\"#000000\">\n"
        "  <path d=\"M10,10 L90,10 L90,90\"/>\n"
        "</g>\n"
        "</svg>\n";
    writeSvg(path, kSvg);

    Fio::FileIOManager mgr;
    char err[512] = {};
    Fio::FioParseResult r{};
    ASSERT_TRUE(mgr.importToIR(path.string().c_str(), Fio::FileFormat::SVG, &r, err, sizeof(err))) << err;
    std::filesystem::remove(path);

    ASSERT_EQ(r.groupCount, 1u);
    EXPECT_STREQ(r.groups[0].name, "Cut Layer");
    EXPECT_EQ(r.entities[0].groupSourceId, r.groups[0].sourceId);
}

TEST(SvgGroupTest, DefsGroupsAreIgnored)
{
    const auto path = std::filesystem::temp_directory_path() / "sanyi_svg_defs.svg";
    const char* kSvg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">\n"
        "<defs>\n"
        "  <g id=\"defLayer\">\n"
        "    <path d=\"M1,1 L2,1 L2,2\"/>\n"
        "  </g>\n"
        "</defs>\n"
        "<path fill=\"none\" stroke=\"#000000\" d=\"M10,10 L90,10 L90,90\"/>\n"
        "</svg>\n";
    writeSvg(path, kSvg);

    Fio::FileIOManager mgr;
    char err[512] = {};
    Fio::FioParseResult r{};
    ASSERT_TRUE(mgr.importToIR(path.string().c_str(), Fio::FileFormat::SVG, &r, err, sizeof(err))) << err;
    std::filesystem::remove(path);

    // 顶层无群组；<defs> 内的 <g> 不建群组
    EXPECT_EQ(r.groupCount, 0u);
    ASSERT_GE(r.entityCount, 1u);
    EXPECT_EQ(r.entities[0].groupSourceId, 0u);
}

TEST(SvgGroupTest, ElementOwnIdInsideGroupMapsToGroup)
{
    const auto path = std::filesystem::temp_directory_path() / "sanyi_svg_ownid.svg";
    const char* kSvg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">\n"
        "<g id=\"layerA\" fill=\"none\" stroke=\"#000000\">\n"
        "  <path id=\"p1\" d=\"M10,10 L90,10 L90,90\"/>\n"
        "</g>\n"
        "</svg>\n";
    writeSvg(path, kSvg);

    Fio::FileIOManager mgr;
    char err[512] = {};
    Fio::FioParseResult r{};
    ASSERT_TRUE(mgr.importToIR(path.string().c_str(), Fio::FileFormat::SVG, &r, err, sizeof(err))) << err;
    std::filesystem::remove(path);

    ASSERT_EQ(r.groupCount, 1u);
    ASSERT_EQ(r.entityCount, 1u);
    // 元素自身 id 覆盖继承，但仍归到最近的 <g> 群组
    EXPECT_EQ(r.entities[0].groupSourceId, r.groups[0].sourceId);
}
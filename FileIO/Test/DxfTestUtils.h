#pragma once

// DXF 测试公用工具：手写最小可解析的 DXF 文件。
//
// 为什么手写而不是准备样例文件：DXF 是纯文本分组码格式，手写能把每条测试关心的
// 那一个组码（bulge=42、闭合标志=70、挤出方向=210/220/230、$INSUNITS）单独隔离出来，
// 出问题时一眼看出是哪个字段没解析对；样例文件则会把十几个特性混在一起。

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace DxfTest
{
    /// 写一个最小可解析的 DXF：HEADER + TABLES(LAYER "0") + BLOCKS + ENTITIES
    /// @return 文件绝对路径；写入失败返回空串
    inline std::string writeDxf(const std::string& fileName,
        const std::string& headerBody,
        const std::string& blocksBody,
        const std::string& entitiesBody)
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / fileName;
        std::ofstream out(path, std::ios::binary);
        if (!out)
        {
            return {};
        }

        out << "0\nSECTION\n2\nHEADER\n" << headerBody << "0\nENDSEC\n";
        out << "0\nSECTION\n2\nTABLES\n";
        out << "0\nTABLE\n2\nLAYER\n70\n1\n";
        out << "0\nLAYER\n2\n0\n70\n0\n62\n7\n6\nCONTINUOUS\n";
        out << "0\nENDTAB\n0\nENDSEC\n";
        out << "0\nSECTION\n2\nBLOCKS\n" << blocksBody << "0\nENDSEC\n";
        out << "0\nSECTION\n2\nENTITIES\n" << entitiesBody << "0\nENDSEC\n";
        out << "0\nEOF\n";
        out.close();
        return path.string();
    }

    /// 无 HEADER 变量的简写
    inline std::string writeDxf(
        const std::string& fileName, const std::string& blocksBody, const std::string& entitiesBody)
    {
        return writeDxf(fileName, std::string(), blocksBody, entitiesBody);
    }

    /// 块定义：名为 name、基点 (bx,by)，内含一条 (0,0)→(10,0) 的直线
    inline std::string blockWithLine(const std::string& name, double bx = 0.0, double by = 0.0)
    {
        std::string s;
        s += "0\nBLOCK\n2\n" + name + "\n70\n0\n";
        s += "10\n" + std::to_string(bx) + "\n20\n" + std::to_string(by) + "\n30\n0.0\n";
        s += "3\n" + name + "\n1\n\n";
        s += "0\nLINE\n8\n0\n10\n0.0\n20\n0.0\n30\n0.0\n11\n10.0\n21\n0.0\n31\n0.0\n";
        s += "0\nENDBLK\n";
        return s;
    }

    /// 一条 INSERT 记录（角度用度，与 DXF 组码 50 一致）
    inline std::string insertOf(const std::string& name,
        double x,
        double y,
        double sx = 1.0,
        double sy = 1.0,
        double angleDeg = 0.0,
        int cols = 1,
        int rows = 1,
        double colspace = 0.0,
        double rowspace = 0.0)
    {
        std::string s;
        s += "0\nINSERT\n8\n0\n2\n" + name + "\n";
        s += "10\n" + std::to_string(x) + "\n20\n" + std::to_string(y) + "\n30\n0.0\n";
        s += "41\n" + std::to_string(sx) + "\n42\n" + std::to_string(sy) + "\n43\n1.0\n";
        s += "50\n" + std::to_string(angleDeg) + "\n";
        s += "70\n" + std::to_string(cols) + "\n71\n" + std::to_string(rows) + "\n";
        s += "44\n" + std::to_string(colspace) + "\n45\n" + std::to_string(rowspace) + "\n";
        return s;
    }

    inline void removeFile(const std::string& path)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}  // namespace DxfTest

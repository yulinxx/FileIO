#include "FileIO/Writers/UgWriter.h"

#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyEllipse.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine2D/SyEntity/SyBezier.h"
#include "Engine2D/SyEntity/SyBezier2.h"
#include "Engine2D/SyEntity/SyNurbs.h"
#include "Engine2D/SyEntity/SySmartLine.h"

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>

namespace Fio
{
    namespace
    {
        constexpr double kRadToDeg = 180.0 / M_PI;

        // IGES 固定长度段字段宽度
        constexpr int kFieldWidth = 8;

        // 格式化 IGES 参数字段（右填充空格到指定宽度）
        std::string padField(const std::string& val, int width = kFieldWidth)
        {
            if (static_cast<int>(val.size()) >= width)
                return val.substr(0, width);
            return val + std::string(width - val.size(), ' ');
        }

        // 格式化双精度为 IGES 参数字符串
        std::string igesDouble(double v)
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(6) << v;
            std::string s = oss.str();
            // 去除尾部多余的零
            auto dot = s.find('.');
            if (dot != std::string::npos)
            {
                auto last = s.find_last_not_of('0');
                if (last == dot)
                    s.erase(dot);
                else
                    s.erase(last + 1);
            }
            if (s.empty())
                s = "0";
            return s;
        }

        // 写 Global 段 (S=1, G=1)
        void writeGlobalSection(std::ostream& out)
        {
            // Global section 参数（逗号分隔，以分号结尾）
            std::ostringstream params;
            params << "1H,,"
                << "1HSanYi CAD,1HExport,17,7,2,,1.0,2,0.00001,200.0,"
                << "100000,0.00001,0.0,1.0,0.01,1H,0.0,0.0;";

            std::string pStr = params.str();

            // Global 段目录行 (G)
            // 格式: G{参数指针}{参数长度}{文件描述符}{编码}{最高实体ID}{参数段编号}{精度}{最大坐标宽}{最大坐标数}{线宽因子}{文件版本号}{审阅结束标记}
            std::ostringstream gLine;
            gLine << "1H,"                        // 参数定界符
                << padField("1H,")                // 记录定界符
                << padField("")                   // 发送方系统标识
                << padField("")                   // 文件名
                << padField("")                   // 前置处理器标识
                << "17" << ","                    // 系统标识码
                << "7" << ","                     // 辅助系统标识码
                << "2" << ","                     // 产品版本
                << padField("")                   // 处理码
                << "1.0" << ","                   // 矩阵单位
                << "2" << ","                     // 最小比例尺
                << "0.00001" << ","               // 最大坐标值
                << "200.0" << ","                 // 本地坐标精度
                << "100000" << ","                // 最大长整数
                << "0.00001" << ","               // 最小精度
                << "0.0" << ","                   // 最大线宽
                << "1.0" << ","                   // 精度位数
                << "0.01" << ","                  // 最大长整数宽度
                << "1H" << ","                    // 单位标识
                << "0.0" << ","                   // 最大坐标
                << "0.0;"                         // 最小坐标
                ;

            // 写 Global 段（固定长度80字符）
            std::string gStr = gLine.str();
            while (static_cast<int>(gStr.size()) < 72)
                gStr += ' ';
            gStr += "G";
            while (static_cast<int>(gStr.size()) < 80)
                gStr += ' ';
            out << gStr << '\n';
        }

        // 写目录段条目 (D) - 每个实体占2行 (共160字符)
        // 第1行 (D): 实体类型, 参数数据指针, 结构, 线型, 层, 视图, 变换矩阵, 标签显示, 顺序号
        // 第2行 (D): 颜色, 参数行数, 实体类型(备用), 格式, 相关性, 重量, 根号, 标签, 顺序号
        void writeDirectoryEntry(std::ostream& out, int& dirSeq,
            int entityType, int paramPointer, int paramCount, int color = 256)
        {
            // 第1行 (DE1)
            std::ostringstream de1;
            de1 << std::setw(8) << entityType           // 1-8:   实体类型号
                << std::setw(8) << paramPointer          // 9-16:  参数数据指针
                << std::setw(8) << 0                     // 17-24: 结构
                << std::setw(8) << 1                     // 25-32: 线型 (0=ByLayer, 1=Solid)
                << std::setw(8) << 0                     // 33-40: 图层
                << std::setw(8) << 0                     // 41-48: 视图
                << std::setw(8) << 0                     // 49-56: 变换矩阵
                << std::setw(8) << 0                     // 57-64: 标签显示
                << std::setw(8) << ++dirSeq;             // 65-72: 序列号
            de1 << "D";

            std::string s1 = de1.str();
            while (static_cast<int>(s1.size()) < 80)
                s1 += ' ';
            out << s1 << '\n';

            // 第2行 (DE2)
            std::ostringstream de2;
            de2 << std::setw(8) << color                 // 1-8:   颜色号 (256=ByLayer)
                << std::setw(8) << paramCount            // 9-16:  参数行数
                << std::setw(8) << entityType            // 17-24: 实体类型号(备用)
                << std::setw(8) << 0                     // 25-32: 格式号
                << std::setw(8) << 0                     // 33-40: 相关性
                << std::setw(8) << 0                     // 41-48: 重量
                << std::setw(8) << 0                     // 49-56: 节数
                << std::setw(8) << 0                     // 57-64: 标签
                << std::setw(8) << ++dirSeq;             // 65-72: 序列号
            de2 << "D";

            std::string s2 = de2.str();
            while (static_cast<int>(s2.size()) < 80)
                s2 += ' ';
            out << s2 << '\n';
        }

        // 写参数段条目 (P) - 每行80字符
        // 每行最后1字符: 最后一行 'T', 其他 ';'
        // 每行开头: 实体类型号(1-8), 参数数据(9-72), 序列号(73-80)
        void writeParameterLine(std::ostream& out, int entityType,
            const std::string& params, int& paramSeq, bool isLast)
        {
            std::ostringstream line;
            line << std::setw(8) << entityType << params;
            std::string s = line.str();

            // 截断到72字符（参数区域），然后添加序列号
            if (static_cast<int>(s.size()) > 72)
                s = s.substr(0, 72);

            // 填充到72字符
            while (static_cast<int>(s.size()) < 72)
                s += ' ';

            // 添加序列号和终止符
            std::ostringstream tail;
            tail << std::setw(8) << ++paramSeq << (isLast ? 'T' : ' ');
            s += tail.str();

            out << s << '\n';
        }

        // 添加图元到 IGES 输出
        void addEntityToIges(std::ostream& out,
            const Eg::SyEntity* entity,
            int& dirSeq, int& paramSeq)
        {
            if (!entity)
                return;

            switch (entity->eType)
            {
                case Eg::EType::LINE:
                {
                    const auto* line = static_cast<const Eg::SyLine*>(entity);
                    if (line->vPoints.size() < 2)
                        break;
                    // 将折线拆分为多条 LINE
                    for (size_t i = 1; i < line->vPoints.size(); ++i)
                    {
                        std::ostringstream params;
                        params << ",1,1,"
                            << igesDouble(line->vPoints[i - 1].x()) << ","
                            << igesDouble(line->vPoints[i - 1].y()) << ","
                            << igesDouble(0.0) << ","
                            << igesDouble(line->vPoints[i].x()) << ","
                            << igesDouble(line->vPoints[i].y()) << ","
                            << igesDouble(0.0) << ";";

                        writeDirectoryEntry(out, dirSeq, 116, paramSeq + 1, 1);
                        writeParameterLine(out, 116, params.str(), paramSeq, true);
                    }
                    break;
                }
                case Eg::EType::CIRCLE:
                {
                    const auto* circle = static_cast<const Eg::SyCircle*>(entity);
                    // IGES 圆 = 圆弧(102) + 圆弧数据(100)
                    // 参数: DE序号, 圆弧数量=1, 圆弧类型
                    std::ostringstream params;
                    params << ",0,2,1,0,"  // DE pointer(占位), 2个子参数, 类型=圆, 关联=0
                        << "1,100,1,"   // 起始实体=100, 类型=1
                        << "0,"         // 平面标识
                        << igesDouble(circle->basePoint.x()) << ","
                        << igesDouble(circle->basePoint.y()) << ","
                        << igesDouble(0.0) << ","
                        << igesDouble(circle->dRadius) << ","
                        << igesDouble(0.0) << ","
                        << igesDouble(2.0 * M_PI) << ";";

                    writeDirectoryEntry(out, dirSeq, 102, paramSeq + 1, 1);
                    writeParameterLine(out, 102, params.str(), paramSeq, true);
                    break;
                }
                case Eg::EType::ARC:
                {
                    const auto* arc = static_cast<const Eg::SyArc*>(entity);
                    std::ostringstream params;
                    params << ",0,2,1,0,"
                        << "1,100,1,"
                        << "0,"
                        << igesDouble(arc->basePoint.x()) << ","
                        << igesDouble(arc->basePoint.y()) << ","
                        << igesDouble(0.0) << ","
                        << igesDouble(arc->dRadius) << ","
                        << igesDouble(arc->dStartAngle) << ","
                        << igesDouble(arc->dEndAngle) << ";";

                    writeDirectoryEntry(out, dirSeq, 102, paramSeq + 1, 1);
                    writeParameterLine(out, 102, params.str(), paramSeq, true);
                    break;
                }
                case Eg::EType::ELLIPSE:
                {
                    const auto* ellipse = static_cast<const Eg::SyEllipse*>(entity);
                    std::ostringstream params;
                    params << ",0,"
                        << igesDouble(ellipse->basePoint.x()) << ","
                        << igesDouble(ellipse->basePoint.y()) << ","
                        << igesDouble(0.0) << ","
                        << igesDouble(ellipse->dRadiusX * std::cos(ellipse->dRotation)) << ","
                        << igesDouble(ellipse->dRadiusX * std::sin(ellipse->dRotation)) << ","
                        << igesDouble(ellipse->dRadiusY / std::max(ellipse->dRadiusX, 1e-9)) << ","
                        << igesDouble(ellipse->dStartAngle) << ","
                        << igesDouble(ellipse->dEndAngle) << ";";

                    writeDirectoryEntry(out, dirSeq, 104, paramSeq + 1, 1);
                    writeParameterLine(out, 104, params.str(), paramSeq, true);
                    break;
                }
                case Eg::EType::POLYGON:
                {
                    const auto* polygon = static_cast<const Eg::SyPolygon*>(entity);
                    if (polygon->vVertices.size() < 2)
                        break;
                    // 折线 (106): 参数 = 连续性, 顶点数, x1,y1, x2,y2, ...
                    std::ostringstream params;
                    const bool closed = polygon->bClosed;
                    params << "," << (closed ? 1 : 0) << ","
                        << polygon->vVertices.size();
                    for (const auto& pt : polygon->vVertices)
                    {
                        params << "," << igesDouble(pt.x()) << ","
                            << igesDouble(pt.y());
                    }
                    params << ";";

                    writeDirectoryEntry(out, dirSeq, 106, paramSeq + 1, 1);
                    writeParameterLine(out, 106, params.str(), paramSeq, true);
                    break;
                }
                case Eg::EType::BEZIER:
                {
                    const auto* bezier = static_cast<const Eg::SyBezier*>(entity);
                    // 二阶贝塞尔(20) → IGES 样条(126) 通过离散化
                    constexpr int kSegCount = 20;
                    std::ostringstream params;
                    params << "," << kSegCount << ",0,0,1,0,0,3,"
                        << kSegCount + 1;
                    for (int i = 0; i <= kSegCount; ++i)
                    {
                        const double t = static_cast<double>(i) / kSegCount;
                        const double u = 1.0 - t;
                        const double x = u * u * u * bezier->basePoint.x()
                            + 3.0 * u * u * t * bezier->ptCtrl0.x()
                            + 3.0 * u * t * t * bezier->ptCtrl1.x()
                            + t * t * t * bezier->ptEnd.x();
                        const double y = u * u * u * bezier->basePoint.y()
                            + 3.0 * u * u * t * bezier->ptCtrl0.y()
                            + 3.0 * u * t * t * bezier->ptCtrl1.y()
                            + t * t * t * bezier->ptEnd.y();
                        params << "," << igesDouble(x) << "," << igesDouble(y);
                    }
                    params << ";";

                    writeDirectoryEntry(out, dirSeq, 126, paramSeq + 1, 1);
                    writeParameterLine(out, 126, params.str(), paramSeq, true);
                    break;
                }
                case Eg::EType::BEZIER2:
                {
                    const auto* bezier2 = static_cast<const Eg::SyBezier2*>(entity);
                    constexpr int kSegCount = 20;
                    std::ostringstream params;
                    params << "," << kSegCount << ",0,0,1,0,0,3,"
                        << kSegCount + 1;
                    for (int i = 0; i <= kSegCount; ++i)
                    {
                        const double t = static_cast<double>(i) / kSegCount;
                        const double u = 1.0 - t;
                        const double x = u * u * bezier2->basePoint.x()
                            + 2.0 * u * t * bezier2->ptCtrl.x()
                            + t * t * bezier2->ptEnd.x();
                        const double y = u * u * bezier2->basePoint.y()
                            + 2.0 * u * t * bezier2->ptCtrl.y()
                            + t * t * bezier2->ptEnd.y();
                        params << "," << igesDouble(x) << "," << igesDouble(y);
                    }
                    params << ";";

                    writeDirectoryEntry(out, dirSeq, 126, paramSeq + 1, 1);
                    writeParameterLine(out, 126, params.str(), paramSeq, true);
                    break;
                }
                case Eg::EType::SPLINE:
                {
                    const auto* spline = static_cast<const Eg::SyNurbs*>(entity);
                    if (spline->vControlPoints.size() < 2)
                        break;
                    constexpr int kSegCount = 40;
                    std::ostringstream params;
                    params << "," << kSegCount << ",0,0,1,0,0,"
                        << std::max(1, spline->nDegree) << ","
                        << kSegCount + 1;
                    for (int i = 0; i <= kSegCount; ++i)
                    {
                        const double t = static_cast<double>(i) / kSegCount;
                        const auto pt = spline->value(t);
                        params << "," << igesDouble(pt.x()) << "," << igesDouble(pt.y());
                    }
                    params << ";";

                    writeDirectoryEntry(out, dirSeq, 126, paramSeq + 1, 1);
                    writeParameterLine(out, 126, params.str(), paramSeq, true);
                    break;
                }
                case Eg::EType::SMARTLINE:
                {
                    const auto* smartLine = static_cast<const Eg::SySmartLine*>(entity);
                    for (size_t si = 0; si < smartLine->segmentCount(); ++si)
                    {
                        addEntityToIges(out, smartLine->segment(si), dirSeq, paramSeq);
                    }
                    break;
                }
                default:
                    break;
            }
        }

        // 写终止段 (T)
        void writeTerminateSection(std::ostream& out, int dirLineCount, int paramLineCount)
        {
            // T 段格式: S数 G数 D数 P数
            std::ostringstream tLine;
            tLine << std::setw(8) << 1           // S 段行数
                << std::setw(8) << 1           // G 段行数
                << std::setw(8) << dirLineCount   // D 段行数
                << std::setw(8) << paramLineCount; // P 段行数

            std::string s = tLine.str();
            while (static_cast<int>(s.size()) < 72)
                s += ' ';
            s += 'T';
            while (static_cast<int>(s.size()) < 80)
                s += ' ';
            out << s << '\n';
        }
    } // anonymous namespace

    FileFormat UgWriter::format() const
    {
        return FileFormat::UG;
    }

    std::string UgWriter::formatName() const
    {
        return "IGES 5.3 (UG/NX)";
    }

    std::string UgWriter::defaultExtension() const
    {
        return "igs";
    }

    WriteResult UgWriter::write(const std::string& filePath, const VecSyEntityPtr& entities)
    {
        if (entities.empty())
        {
            return WriteResult::fail("No entities to export");
        }

        std::filesystem::path fsPath = std::filesystem::u8path(filePath);
        std::ofstream out(fsPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return WriteResult::fail("Cannot open file for writing: " + filePath);
        }

        // 使用内存流收集参数段内容，以便最后计算偏移量
        std::ostringstream paramStream;
        int dirSeq = 0;
        int paramSeq = 0;
        int dirLineCount = 0;

        // 先写文件头占位符（80字符/行，后面会重写）
        out << std::string(80, ' ') << '\n'; // S 段 (1行)
        // G 段会在收集完参数后写入

        // 收集所有图元的参数段
        for (const auto& entity : entities)
        {
            if (!entity)
            {
                continue;
            }
            addEntityToIges(paramStream, entity.get(), dirSeq, paramSeq);
        }

        if (dirSeq == 0)
        {
            return WriteResult::fail("No supported entities to export as IGES");
        }

        // dirLineCount = dirSeq (每个实体2行，dirSeq每次+2)
        // 但 dirSeq 是递增的序列号，实际行数 = dirSeq
        dirLineCount = dirSeq;
        int paramLineCount = paramSeq;

        // 重写文件：S段
        out.seekp(0);
        {
            std::ostringstream sLine;
            sLine << "1H," << std::setw(73) << 'S';
            std::string s = sLine.str();
            while (static_cast<int>(s.size()) < 80)
                s += ' ';
            out << s << '\n';
        }

        // G 段
        {
            std::ostringstream gLine;
            gLine << "1H,1HSanYi CAD,1HIGES,17,7,2,,1.0,2,0.00001,200.0,"
                << "100000,0.00001,0.0,1.0,0.01,1H,0.0,0.0;";

            // 参数区域需要填到72字符
            std::string gBody = gLine.str();
            while (static_cast<int>(gBody.size()) < 72)
                gBody += ' ';
            gBody += std::string("G") + std::string(7, ' ');
            while (static_cast<int>(gBody.size()) < 80)
                gBody += ' ';
            out << gBody << '\n';
        }

        // 写目录段（已在 addEntityToIges 中写入 paramStream 的是参数段，目录段需要重新写）
        // 实际上我们把目录段也写入 paramStream，然后一次性写入 out
        // 由于 addEntityToIges 直接写到 paramStream 中的目录段和参数段，
        // 我们需要重新组织。为了简化，我们把目录段写入 out，参数段写入 paramStream。
        //
        // 重来：先把目录段写到一个单独的流
        out.close();

        // 重新打开文件，完整写入
        std::ofstream outFinal(fsPath, std::ios::binary | std::ios::trunc);
        if (!outFinal)
        {
            return WriteResult::fail("Cannot open file for writing: " + filePath);
        }

        // S 段
        {
            std::ostringstream line;
            line << "1H,";
            std::string s = line.str();
            while (static_cast<int>(s.size()) < 72)
                s += ' ';
            s += "S";
            while (static_cast<int>(s.size()) < 80)
                s += ' ';
            outFinal << s << '\n';
        }

        // G 段
        {
            std::ostringstream line;
            line << "1H,1HSanYi CAD,1HIGES,17,7,2,,1.0,2,0.00001,200.0,"
                << "100000,0.00001,0.0,1.0,0.01,1H,0.0,0.0;";
            std::string s = line.str();
            while (static_cast<int>(s.size()) < 72)
                s += ' ';
            s += "G";
            while (static_cast<int>(s.size()) < 80)
                s += ' ';
            outFinal << s << '\n';
        }

        // D 段（目录段）+ P 段（参数段）需要重新生成
        {
            int dSeq = 0;
            int pSeq = 0;
            std::ostringstream dirStream;
            std::ostringstream paramOut;

            for (const auto& entity : entities)
            {
                if (!entity)
                    continue;

                switch (entity->eType)
                {
                    case Eg::EType::LINE:
                    {
                        const auto* line = static_cast<const Eg::SyLine*>(entity.get());
                        if (line->vPoints.size() < 2)
                            break;
                        for (size_t i = 1; i < line->vPoints.size(); ++i)
                        {
                            std::ostringstream params;
                            params << ",1,1,"
                                << igesDouble(line->vPoints[i - 1].x()) << ","
                                << igesDouble(line->vPoints[i - 1].y()) << ","
                                << igesDouble(0.0) << ","
                                << igesDouble(line->vPoints[i].x()) << ","
                                << igesDouble(line->vPoints[i].y()) << ","
                                << igesDouble(0.0) << ";";

                            writeDirectoryEntry(dirStream, dSeq, 116, pSeq + 1, 1);
                            writeParameterLine(paramOut, 116, params.str(), pSeq, true);
                        }
                        break;
                    }
                    case Eg::EType::CIRCLE:
                    {
                        const auto* circle = static_cast<const Eg::SyCircle*>(entity.get());
                        std::ostringstream params;
                        params << ",0,2,1,0,"
                            << "1,100,1,"
                            << "0,"
                            << igesDouble(circle->basePoint.x()) << ","
                            << igesDouble(circle->basePoint.y()) << ","
                            << igesDouble(0.0) << ","
                            << igesDouble(circle->dRadius) << ","
                            << igesDouble(0.0) << ","
                            << igesDouble(2.0 * M_PI) << ";";

                        writeDirectoryEntry(dirStream, dSeq, 102, pSeq + 1, 1);
                        writeParameterLine(paramOut, 102, params.str(), pSeq, true);
                        break;
                    }
                    case Eg::EType::ARC:
                    {
                        const auto* arc = static_cast<const Eg::SyArc*>(entity.get());
                        std::ostringstream params;
                        params << ",0,2,1,0,"
                            << "1,100,1,"
                            << "0,"
                            << igesDouble(arc->basePoint.x()) << ","
                            << igesDouble(arc->basePoint.y()) << ","
                            << igesDouble(0.0) << ","
                            << igesDouble(arc->dRadius) << ","
                            << igesDouble(arc->dStartAngle) << ","
                            << igesDouble(arc->dEndAngle) << ";";

                        writeDirectoryEntry(dirStream, dSeq, 102, pSeq + 1, 1);
                        writeParameterLine(paramOut, 102, params.str(), pSeq, true);
                        break;
                    }
                    case Eg::EType::ELLIPSE:
                    {
                        const auto* ellipse = static_cast<const Eg::SyEllipse*>(entity.get());
                        std::ostringstream params;
                        params << ",0,"
                            << igesDouble(ellipse->basePoint.x()) << ","
                            << igesDouble(ellipse->basePoint.y()) << ","
                            << igesDouble(0.0) << ","
                            << igesDouble(ellipse->dRadiusX * std::cos(ellipse->dRotation)) << ","
                            << igesDouble(ellipse->dRadiusX * std::sin(ellipse->dRotation)) << ","
                            << igesDouble(ellipse->dRadiusY / std::max(ellipse->dRadiusX, 1e-9)) << ","
                            << igesDouble(ellipse->dStartAngle) << ","
                            << igesDouble(ellipse->dEndAngle) << ";";

                        writeDirectoryEntry(dirStream, dSeq, 104, pSeq + 1, 1);
                        writeParameterLine(paramOut, 104, params.str(), pSeq, true);
                        break;
                    }
                    case Eg::EType::POLYGON:
                    {
                        const auto* polygon = static_cast<const Eg::SyPolygon*>(entity.get());
                        if (polygon->vVertices.size() < 2)
                            break;
                        std::ostringstream params;
                        params << "," << (polygon->bClosed ? 1 : 0) << ","
                            << polygon->vVertices.size();
                        for (const auto& pt : polygon->vVertices)
                        {
                            params << "," << igesDouble(pt.x()) << ","
                                << igesDouble(pt.y());
                        }
                        params << ";";

                        writeDirectoryEntry(dirStream, dSeq, 106, pSeq + 1, 1);
                        writeParameterLine(paramOut, 106, params.str(), pSeq, true);
                        break;
                    }
                    case Eg::EType::BEZIER:
                    {
                        const auto* bezier = static_cast<const Eg::SyBezier*>(entity.get());
                        constexpr int kSegCount = 20;
                        std::ostringstream params;
                        params << "," << kSegCount << ",0,0,1,0,0,3,"
                            << kSegCount + 1;
                        for (int i = 0; i <= kSegCount; ++i)
                        {
                            const double t = static_cast<double>(i) / kSegCount;
                            const double u = 1.0 - t;
                            const double x = u * u * u * bezier->basePoint.x()
                                + 3.0 * u * u * t * bezier->ptCtrl0.x()
                                + 3.0 * u * t * t * bezier->ptCtrl1.x()
                                + t * t * t * bezier->ptEnd.x();
                            const double y = u * u * u * bezier->basePoint.y()
                                + 3.0 * u * u * t * bezier->ptCtrl0.y()
                                + 3.0 * u * t * t * bezier->ptCtrl1.y()
                                + t * t * t * bezier->ptEnd.y();
                            params << "," << igesDouble(x) << "," << igesDouble(y);
                        }
                        params << ";";

                        writeDirectoryEntry(dirStream, dSeq, 126, pSeq + 1, 1);
                        writeParameterLine(paramOut, 126, params.str(), pSeq, true);
                        break;
                    }
                    case Eg::EType::BEZIER2:
                    {
                        const auto* bezier2 = static_cast<const Eg::SyBezier2*>(entity.get());
                        constexpr int kSegCount = 20;
                        std::ostringstream params;
                        params << "," << kSegCount << ",0,0,1,0,0,3,"
                            << kSegCount + 1;
                        for (int i = 0; i <= kSegCount; ++i)
                        {
                            const double t = static_cast<double>(i) / kSegCount;
                            const double u = 1.0 - t;
                            const double x = u * u * bezier2->basePoint.x()
                                + 2.0 * u * t * bezier2->ptCtrl.x()
                                + t * t * bezier2->ptEnd.x();
                            const double y = u * u * bezier2->basePoint.y()
                                + 2.0 * u * t * bezier2->ptCtrl.y()
                                + t * t * bezier2->ptEnd.y();
                            params << "," << igesDouble(x) << "," << igesDouble(y);
                        }
                        params << ";";

                        writeDirectoryEntry(dirStream, dSeq, 126, pSeq + 1, 1);
                        writeParameterLine(paramOut, 126, params.str(), pSeq, true);
                        break;
                    }
                    case Eg::EType::SPLINE:
                    {
                        const auto* spline = static_cast<const Eg::SyNurbs*>(entity.get());
                        if (spline->vControlPoints.size() < 2)
                            break;
                        constexpr int kSegCount = 40;
                        std::ostringstream params;
                        params << "," << kSegCount << ",0,0,1,0,0,"
                            << std::max(1, spline->nDegree) << ","
                            << kSegCount + 1;
                        for (int i = 0; i <= kSegCount; ++i)
                        {
                            const double t = static_cast<double>(i) / kSegCount;
                            const auto pt = spline->value(t);
                            params << "," << igesDouble(pt.x()) << "," << igesDouble(pt.y());
                        }
                        params << ";";

                        writeDirectoryEntry(dirStream, dSeq, 126, pSeq + 1, 1);
                        writeParameterLine(paramOut, 126, params.str(), pSeq, true);
                        break;
                    }
                    case Eg::EType::SMARTLINE:
                    {
                        const auto* smartLine = static_cast<const Eg::SySmartLine*>(entity.get());
                        for (size_t si = 0; si < smartLine->segmentCount(); ++si)
                        {
                            const auto* seg = smartLine->segment(si);
                            if (!seg)
                                continue;
                            // 递归处理子段
                            switch (seg->eType)
                            {
                                case Eg::EType::LINE:
                                {
                                    const auto* ln = static_cast<const Eg::SyLine*>(seg);
                                    if (ln->vPoints.size() < 2)
                                        break;
                                    for (size_t pi = 1; pi < ln->vPoints.size(); ++pi)
                                    {
                                        std::ostringstream params;
                                        params << ",1,1,"
                                            << igesDouble(ln->vPoints[pi - 1].x()) << ","
                                            << igesDouble(ln->vPoints[pi - 1].y()) << ","
                                            << igesDouble(0.0) << ","
                                            << igesDouble(ln->vPoints[pi].x()) << ","
                                            << igesDouble(ln->vPoints[pi].y()) << ","
                                            << igesDouble(0.0) << ";";
                                        writeDirectoryEntry(dirStream, dSeq, 116, pSeq + 1, 1);
                                        writeParameterLine(paramOut, 116, params.str(), pSeq, true);
                                    }
                                    break;
                                }
                                case Eg::EType::ARC:
                                {
                                    const auto* arc = static_cast<const Eg::SyArc*>(seg);
                                    std::ostringstream params;
                                    params << ",0,2,1,0,"
                                        << "1,100,1,"
                                        << "0,"
                                        << igesDouble(arc->basePoint.x()) << ","
                                        << igesDouble(arc->basePoint.y()) << ","
                                        << igesDouble(0.0) << ","
                                        << igesDouble(arc->dRadius) << ","
                                        << igesDouble(arc->dStartAngle) << ","
                                        << igesDouble(arc->dEndAngle) << ";";
                                    writeDirectoryEntry(dirStream, dSeq, 102, pSeq + 1, 1);
                                    writeParameterLine(paramOut, 102, params.str(), pSeq, true);
                                    break;
                                }
                                case Eg::EType::CIRCLE:
                                {
                                    const auto* circle = static_cast<const Eg::SyCircle*>(seg);
                                    std::ostringstream params;
                                    params << ",0,2,1,0,"
                                        << "1,100,1,"
                                        << "0,"
                                        << igesDouble(circle->basePoint.x()) << ","
                                        << igesDouble(circle->basePoint.y()) << ","
                                        << igesDouble(0.0) << ","
                                        << igesDouble(circle->dRadius) << ","
                                        << igesDouble(0.0) << ","
                                        << igesDouble(2.0 * M_PI) << ";";
                                    writeDirectoryEntry(dirStream, dSeq, 102, pSeq + 1, 1);
                                    writeParameterLine(paramOut, 102, params.str(), pSeq, true);
                                    break;
                                }
                                default:
                                    break;
                            }
                        }
                        break;
                    }
                    default:
                        break;
                }
            }

            // 写 D 段
            outFinal << dirStream.str();
            // 写 P 段
            outFinal << paramOut.str();
        }

        // T 段
        writeTerminateSection(outFinal, dirSeq, paramSeq);

        outFinal.close();
        return WriteResult::ok();
    }
} // namespace Fio
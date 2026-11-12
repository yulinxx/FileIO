#include "FileIO/Writers/DxfFileWriter.h"

#include "Engine/Layer/AprLayer.h"
#include "Engine2D/AprEntity/AprLine.h"
#include "Engine2D/AprEntity/AprArc.h"
#include "Engine2D/AprEntity/AprCircle.h"
#include "Engine2D/AprEntity/AprEllipse.h"
#include "Engine2D/AprEntity/AprPolygon.h"
#include "Engine2D/AprEntity/AprBezier.h"
#include "Engine2D/AprEntity/AprBezier2.h"
#include "Engine2D/AprEntity/AprNurbs.h"
#include "Engine2D/AprEntity/AprSmartLine.h"
#include "Engine2D/AprEntity/AprText.h"

#include "Log/AprLogger.h"

#include "drw_objects.h"
#include "drw_textcodec.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <limits>
#include <map>
#include <sstream>

namespace Fio
{
    namespace
    {
        void writePair(std::ostream& out, int code, const std::string& value)
        {
            out << code << '\n' << value << '\n';
        }

        void writePair(std::ostream& out, int code, double value)
        {
            out << code << '\n' << value << '\n';
        }

        /// 整型组码必须走这个重载：走 double 重载时大整数会被 defaultfloat 的
        /// 6 位有效数字写成科学计数法（如 16711680 → "1.67117e+07"），DXF 直接损坏
        void writePair(std::ostream& out, int code, int value)
        {
            out << code << '\n' << value << '\n';
        }

        /// 把 0-1 浮点通道量化到 0-255
        int channel255(float value)
        {
            const int scaled = static_cast<int>(std::lround(static_cast<double>(value) * 255.0));
            return std::clamp(scaled, 0, 255);
        }

        /// 是否全部为 ASCII 字节（含空串）
        bool isAsciiText(const std::string& text)
        {
            for (unsigned char ch : text)
            {
                if (ch >= 0x80)
                {
                    return false;
                }
            }
            return true;
        }

        /// DXF 文本编码器：非 ASCII 字符统一转成 $DWGCODEPAGE 声明的 ANSI_936（GBK）。
        ///
        /// 版本选择依据（均以 libdxfrw 实际读回验证，非按规范推测）：
        /// 1. 本写出器只产出 R12 形状的文档：没有 CLASSES/OBJECTS 段、没有图元句柄、
        ///    没有 100 子类标记。把 $ACADVER 单独改成 AC1021 后，libdxfrw 判为结构非法，
        ///    整份文件读不回来（open=0），所以「直接升版本」不可行；
        /// 2. R12 下的字符串按 $DWGCODEPAGE 解释。写 ANSI_936 + GBK 字节后，
        ///    本工程导入器（libdxfrw）能把图层名逐字读回成 UTF-8，这也是中文 R12 DXF 的通行做法。
        ///
        /// 编码器按线程持有：导出可能发生在后台线程；每次 write() 开头调用 reset()。
        class DxfTextEncoder
        {
        public:
            DxfTextEncoder()
            {
                m_codec.setVersion(DRW::AC1009, true);
                m_codec.setCodePage("ANSI_936", true);
            }

            void reset()
            {
                m_sawNonAscii = false;
                m_lossy = false;
            }

            std::string encode(const std::string& utf8)
            {
                if (isAsciiText(utf8))
                {
                    return utf8;
                }
                m_sawNonAscii = true;
                const std::string encoded = m_codec.fromUtf8(utf8);
                // 反查一次：GBK 无法表示的字符会被替换，必须能让调用方知道
                if (m_codec.toUtf8(encoded) != utf8)
                {
                    m_lossy = true;
                }
                return encoded;
            }

            bool sawNonAscii() const
            {
                return m_sawNonAscii;
            }

            bool lossy() const
            {
                return m_lossy;
            }

        private:
            DRW_TextCodec m_codec;
            bool m_sawNonAscii{ false };
            bool m_lossy{ false };
        };

        DxfTextEncoder& dxfEncoder()
        {
            thread_local DxfTextEncoder encoder;
            return encoder;
        }

        /// 任意 UTF-8 字符串 → 当前导出编码（ASCII 原样返回）
        std::string encodeDxfText(const std::string& utf8)
        {
            return dxfEncoder().encode(utf8);
        }

        /// 图元所属图层名；无图层或空名时落到 DXF 约定的默认图层 "0"
        std::string layerNameOf(const Eg::AprEntity* entity)
        {
            const Eg::AprLayer* layer = entity != nullptr ? entity->layer() : nullptr;
            const char* name = layer != nullptr ? layer->getName() : nullptr;
            return encodeDxfText((name != nullptr && name[0] != '\0') ? name : "0");
        }

        /// RGB → 最接近的 ACI 索引（1-255）。
        ///
        /// 图层记录必须给 ACI（组码 62）；真彩色（组码 420）是 R2004+ 才有的码。
        /// 这里两个都写：420 保证颜色精确（本工程导入器优先取 420），
        /// 62 让只认 ACI 的读入器也能拿到近似色。
        int nearestAci(uint8_t r, uint8_t g, uint8_t b)
        {
            int best = 7;
            long bestDistance = std::numeric_limits<long>::max();
            for (int index = 1; index <= 255; ++index)
            {
                const auto& c = DRW::dxfColors[index];
                const long dr = static_cast<long>(c[0]) - static_cast<long>(r);
                const long dg = static_cast<long>(c[1]) - static_cast<long>(g);
                const long db = static_cast<long>(c[2]) - static_cast<long>(b);
                const long distance = dr * dr + dg * dg + db * db;
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = index;
                    if (distance == 0)
                    {
                        break;
                    }
                }
            }
            return best;
        }

        /// 写 DXF 真彩色（组码 420，值域 0x00RRGGBB）
        void writeTrueColor(std::ostream& out, const Ut::Color& color)
        {
            const int argb = (channel255(color.r()) << 16) | (channel255(color.g()) << 8) | channel255(color.b());
            writePair(out, 420, argb);
        }

        /// 图元公共头：类型 + 所属图层 +（必要时）显式颜色。
        ///
        /// 颜色只在「实体色与所属图层色不同」时才写：一致时保持 BYLAYER 语义，
        /// 这样文件回到软件里后改图层颜色仍能带动这些图元。
        void writeEntityHeader(std::ostream& out, const std::string& type, const Eg::AprEntity* entity)
        {
            writePair(out, 0, type);
            writePair(out, 8, layerNameOf(entity));

            const Eg::AprLayer* layer = entity != nullptr ? entity->layer() : nullptr;
            if (layer == nullptr || entity->getColor() != layer->getColor())
            {
                writeTrueColor(out, entity->getColor());
            }
        }

        /// 图层表记录：颜色 + 可见性（写出时都要落到 LAYER 表）
        struct LayerRecord
        {
            Ut::Color color{ Ut::Color::Black() };
            bool visible{ true };
        };

        /// 导出内容里是否含非 ASCII 字符串（决定是否写 $DWGCODEPAGE）
        bool entitiesNeedCodepage(const VecAprEntityPtr& entities)
        {
            for (const auto& entity : entities)
            {
                if (!entity)
                {
                    continue;
                }
                const Eg::AprLayer* layer = entity->layer();
                const char* name = layer != nullptr ? layer->getName() : nullptr;
                if (name != nullptr && !isAsciiText(name))
                {
                    return true;
                }
                if (entity->eType == Eg::EType::TEXT)
                {
                    const auto* text = static_cast<const Eg::AprText*>(entity.get());
                    if (!isAsciiText(text->textStr()))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        /// 写出 TABLES 段里的 LAYER 表，覆盖被图元引用到的所有图层。
        ///
        /// 只导出「有图元在用」的图层：写出器只拿得到图元列表、拿不到 LayerManager，
        /// 所以空图层不会出现在导出结果里（往返时这一点是有损的，已在图层文档中记录）。
        void writeLayerTable(std::ostream& out, const VecAprEntityPtr& entities)
        {
            // 用 map 而不是 unordered_map：输出顺序稳定，同一份文档导出字节可复现
            std::map<std::string, LayerRecord> layers;
            for (const auto& entity : entities)
            {
                if (!entity)
                {
                    continue;
                }
                const Eg::AprLayer* layer = entity->layer();
                LayerRecord record;
                record.color = layer != nullptr ? layer->getColor() : Ut::Color::Black();
                record.visible = layer != nullptr ? layer->isVisible() : true;
                layers.emplace(layerNameOf(entity.get()), record);
            }

            writePair(out, 0, "SECTION");
            writePair(out, 2, "TABLES");
            writePair(out, 0, "TABLE");
            writePair(out, 2, "LAYER");
            writePair(out, 70, static_cast<int>(layers.size()));

            for (const auto& [name, record] : layers)
            {
                writePair(out, 0, "LAYER");
                writePair(out, 2, name);
                // 70 组码：bit0 = 冻结（不可见），bit2 = 锁定。这里只映射可见性；
                // 「关层」的另一套语义（62 组码取负）不混用，否则读方会得到负 ACI 颜色。
                writePair(out, 70, record.visible ? 0 : 1);
                writePair(out,
                    62,
                    nearestAci(static_cast<uint8_t>(channel255(record.color.r())),
                        static_cast<uint8_t>(channel255(record.color.g())),
                        static_cast<uint8_t>(channel255(record.color.b()))));
                writeTrueColor(out, record.color);
                writePair(out, 6, "CONTINUOUS");
            }

            writePair(out, 0, "ENDTAB");
            writePair(out, 0, "ENDSEC");
        }
    }  // namespace

    FileFormat DxfFileWriter::format() const
    {
        return FileFormat::DXF;
    }

    size_t DxfFileWriter::formatName(char* buffer, size_t bufferSize) const
    {
        return copyToBuffer(buffer, bufferSize, "AutoCAD DXF");
    }

    size_t DxfFileWriter::defaultExtension(char* buffer, size_t bufferSize) const
    {
        return copyToBuffer(buffer, bufferSize, "dxf");
    }

    WriteResult DxfFileWriter::write(const char* filePath, const VecAprEntityPtr& entities)
    {
        if (entities.empty())
        {
            return WriteResult::fail("No entities to export");
        }

        std::filesystem::path fsPath = std::filesystem::u8path(filePath);
        std::ofstream out(fsPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return WriteResult::fail(std::string("Cannot open file for writing: ") + filePath);
        }

        // 默认精度只有 6 位有效数字，坐标会被静默截断（1234.5678 → 1234.57）。
        // DXF 的组码值全部按文本解析，必须给足有效位。
        // classic locale：DXF 坐标小数点必须是 '.'，不随系统 locale 变化。
        out.imbue(std::locale::classic());
        out << std::setprecision(15);

        // 本次导出的字符串编码状态按 write 调用重置（编码器按线程持有）
        dxfEncoder().reset();
        const bool needsCodepage = entitiesNeedCodepage(entities);

        writePair(out, 0, "SECTION");
        writePair(out, 2, "HEADER");
        writePair(out, 9, "$ACADVER");
        writePair(out, 1, "AC1009");
        if (needsCodepage)
        {
            // 非 ASCII 字符串按 ANSI_936（GBK）写出，必须显式声明代码页；
            // 只含 ASCII 时不写，保持与历史导出的字节兼容。
            writePair(out, 9, "$DWGCODEPAGE");
            writePair(out, 3, "ANSI_936");
        }
        writePair(out, 0, "ENDSEC");

        // TABLES 段必须在 ENTITIES 之前：DXF 规范要求图层表先于引用它的图元
        writeLayerTable(out, entities);

        writePair(out, 0, "SECTION");
        writePair(out, 2, "ENTITIES");

        int exported = 0;
        for (const auto& entity : entities)
        {
            if (!entity)
            {
                continue;
            }

            switch (entity->eType)
            {
            case Eg::EType::LINE:
            {
                const auto* line = static_cast<const Eg::AprLine*>(entity.get());
                if (line->pointRef().size() < 2)
                {
                    break;
                }

                for (size_t i = 1; i < line->pointRef().size(); ++i)
                {
                    writeEntityHeader(out, "LINE", entity.get());
                    writePair(out, 10, line->pointRef()[i - 1].x());
                    writePair(out, 20, line->pointRef()[i - 1].y());
                    writePair(out, 30, 0.0);
                    writePair(out, 11, line->pointRef()[i].x());
                    writePair(out, 21, line->pointRef()[i].y());
                    writePair(out, 31, 0.0);
                    ++exported;
                }
                break;
            }
            case Eg::EType::CIRCLE:
            {
                const auto* circle = static_cast<const Eg::AprCircle*>(entity.get());
                writeEntityHeader(out, "CIRCLE", entity.get());
                writePair(out, 10, circle->basePoint.x());
                writePair(out, 20, circle->basePoint.y());
                writePair(out, 30, 0.0);
                writePair(out, 40, circle->dRadius);
                ++exported;
                break;
            }
            case Eg::EType::ARC:
            {
                const auto* arc = static_cast<const Eg::AprArc*>(entity.get());
                writeEntityHeader(out, "ARC", entity.get());
                writePair(out, 10, arc->basePoint.x());
                writePair(out, 20, arc->basePoint.y());
                writePair(out, 30, 0.0);
                writePair(out, 40, arc->dRadius);
                writePair(out, 50, arc->dStartAngle * 180.0 / M_PI);
                writePair(out, 51, arc->dEndAngle * 180.0 / M_PI);
                ++exported;
                break;
            }
            case Eg::EType::ELLIPSE:
            {
                const auto* ellipse = static_cast<const Eg::AprEllipse*>(entity.get());
                writeEntityHeader(out, "ELLIPSE", entity.get());
                writePair(out, 10, ellipse->basePoint.x());
                writePair(out, 20, ellipse->basePoint.y());
                writePair(out, 30, 0.0);
                writePair(out, 11, ellipse->dRadiusX * std::cos(ellipse->dRotation));
                writePair(out, 21, ellipse->dRadiusX * std::sin(ellipse->dRotation));
                writePair(out, 31, 0.0);
                writePair(out, 40, ellipse->dRadiusY / std::max(ellipse->dRadiusX, 1e-9));
                ++exported;
                break;
            }
            case Eg::EType::POLYGON:
            {
                const auto* polygon = static_cast<const Eg::AprPolygon*>(entity.get());
                const auto& verts = polygon->vertices();
                if (verts.size() < 2)
                {
                    break;
                }
                writeEntityHeader(out, "LWPOLYLINE", entity.get());
                writePair(out, 90, static_cast<int>(verts.size()));
                writePair(out, 70, polygon->bClosed ? 1 : 0);
                const bool hasBulges = polygon->hasBulges();
                const auto& bulges = polygon->bulges();
                for (size_t i = 0; i < verts.size(); ++i)
                {
                    writePair(out, 10, verts[i].x());
                    writePair(out, 20, verts[i].y());
                    // bulge 挂在段起点顶点上（组码 42）。开放折线末点没有下一顶点，
                    // 即使元数据里残留值也不导出，免得读方把最后一段当成圆弧。
                    if (hasBulges && i < bulges.size() && (polygon->bClosed || i + 1 < verts.size()))
                    {
                        const double bulge = bulges[i];
                        if (std::isfinite(bulge) && std::fabs(bulge) > 1e-12)
                        {
                            writePair(out, 42, bulge);
                        }
                    }
                }
                ++exported;
                break;
            }
            case Eg::EType::BEZIER:
            {
                const auto* bezier = static_cast<const Eg::AprBezier*>(entity.get());
                writeEntityHeader(out, "LWPOLYLINE", entity.get());
                constexpr int kSegCount = 20;
                writePair(out, 90, kSegCount + 1);
                writePair(out, 70, 0);
                for (int i = 0; i <= kSegCount; ++i)
                {
                    const double t = static_cast<double>(i) / kSegCount;
                    const double u = 1.0 - t;
                    const double x = u * u * u * bezier->basePoint.x() + 3.0 * u * u * t * bezier->ptCtrl0.x() +
                        3.0 * u * t * t * bezier->ptCtrl1.x() + t * t * t * bezier->ptEnd.x();
                    const double y = u * u * u * bezier->basePoint.y() + 3.0 * u * u * t * bezier->ptCtrl0.y() +
                        3.0 * u * t * t * bezier->ptCtrl1.y() + t * t * t * bezier->ptEnd.y();
                    writePair(out, 10, x);
                    writePair(out, 20, y);
                }
                ++exported;
                break;
            }
            case Eg::EType::BEZIER2:
            {
                const auto* bezier2 = static_cast<const Eg::AprBezier2*>(entity.get());
                writeEntityHeader(out, "LWPOLYLINE", entity.get());
                constexpr int kSegCount = 20;
                writePair(out, 90, kSegCount + 1);
                writePair(out, 70, 0);
                for (int i = 0; i <= kSegCount; ++i)
                {
                    const double t = static_cast<double>(i) / kSegCount;
                    const double u = 1.0 - t;
                    const double x =
                        u * u * bezier2->basePoint.x() + 2.0 * u * t * bezier2->ptCtrl.x() + t * t * bezier2->ptEnd.x();
                    const double y =
                        u * u * bezier2->basePoint.y() + 2.0 * u * t * bezier2->ptCtrl.y() + t * t * bezier2->ptEnd.y();
                    writePair(out, 10, x);
                    writePair(out, 20, y);
                }
                ++exported;
                break;
            }
            case Eg::EType::SPLINE:
            {
                const auto* spline = static_cast<const Eg::AprNurbs*>(entity.get());
                if (spline->controlPointCount() < 2)
                {
                    break;
                }
                constexpr int kSegCount = 40;
                writeEntityHeader(out, "LWPOLYLINE", entity.get());
                writePair(out, 90, kSegCount + 1);
                writePair(out, 70, spline->bClosed ? 1 : 0);
                for (int i = 0; i <= kSegCount; ++i)
                {
                    const double t = static_cast<double>(i) / kSegCount;
                    const auto pt = spline->value(t);
                    writePair(out, 10, pt.x());
                    writePair(out, 20, pt.y());
                }
                ++exported;
                break;
            }
            case Eg::EType::SMARTLINE:
            {
                const auto* smartLine = static_cast<const Eg::AprSmartLine*>(entity.get());
                for (size_t si = 0; si < smartLine->segmentCount(); ++si)
                {
                    const auto* seg = smartLine->segment(si);
                    if (!seg)
                    {
                        continue;
                    }
                    switch (seg->eType)
                    {
                    case Eg::EType::LINE:
                    {
                        const auto* ln = static_cast<const Eg::AprLine*>(seg);
                        if (ln->pointRef().size() < 2)
                        {
                            break;
                        }
                        for (size_t pi = 1; pi < ln->pointRef().size(); ++pi)
                        {
                            writeEntityHeader(out, "LINE", entity.get());
                            writePair(out, 10, ln->pointRef()[pi - 1].x());
                            writePair(out, 20, ln->pointRef()[pi - 1].y());
                            writePair(out, 30, 0.0);
                            writePair(out, 11, ln->pointRef()[pi].x());
                            writePair(out, 21, ln->pointRef()[pi].y());
                            writePair(out, 31, 0.0);
                        }
                        break;
                    }
                    case Eg::EType::ARC:
                    {
                        const auto* arc = static_cast<const Eg::AprArc*>(seg);
                        writeEntityHeader(out, "ARC", entity.get());
                        writePair(out, 10, arc->basePoint.x());
                        writePair(out, 20, arc->basePoint.y());
                        writePair(out, 30, 0.0);
                        writePair(out, 40, arc->dRadius);
                        writePair(out, 50, arc->dStartAngle * 180.0 / M_PI);
                        writePair(out, 51, arc->dEndAngle * 180.0 / M_PI);
                        break;
                    }
                    case Eg::EType::CIRCLE:
                    {
                        const auto* circle = static_cast<const Eg::AprCircle*>(seg);
                        writeEntityHeader(out, "CIRCLE", entity.get());
                        writePair(out, 10, circle->basePoint.x());
                        writePair(out, 20, circle->basePoint.y());
                        writePair(out, 30, 0.0);
                        writePair(out, 40, circle->dRadius);
                        break;
                    }
                    default:
                        break;
                    }
                }
                ++exported;
                break;
            }
            case Eg::EType::TEXT:
            {
                // 单行 TEXT：多行内容里的换行会破坏 DXF 的「组码/值」行结构，先压平成空格。
                const auto* text = static_cast<const Eg::AprText*>(entity.get());
                std::string content = text->textStr();
                for (char& ch : content)
                {
                    if (ch == '\r' || ch == '\n')
                    {
                        ch = ' ';
                    }
                }
                if (content.empty() || !std::isfinite(text->dHeight) || text->dHeight <= 0.0)
                {
                    // 空文本/非法高度写出去就是无效 TEXT，与其它不可写图元一样跳过
                    break;
                }
                writeEntityHeader(out, "TEXT", entity.get());
                writePair(out, 10, text->basePoint.x());
                writePair(out, 20, text->basePoint.y());
                writePair(out, 30, 0.0);
                writePair(out, 40, text->dHeight);
                writePair(out, 1, encodeDxfText(content));
                writePair(out, 50, text->dRotation * 180.0 / M_PI);
                writePair(out, 7, "Standard");
                ++exported;
                break;
            }
            default:
                break;
            }
        }

        writePair(out, 0, "ENDSEC");
        writePair(out, 0, "EOF");

        if (dxfEncoder().lossy())
        {
            APR_WARNF("[DxfFileWriter] Some non-ASCII characters cannot be represented in ANSI_936 (GBK) "
                      "and were replaced while writing '%s'",
                filePath);
        }

        if (exported == 0)
        {
            return WriteResult::fail("No supported entities to export as DXF");
        }

        return WriteResult::ok();
    }
}  // namespace Fio
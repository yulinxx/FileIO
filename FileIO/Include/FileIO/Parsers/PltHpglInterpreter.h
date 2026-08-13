#pragma once

#ifndef _USE_MATH_DEFINES
    #define _USE_MATH_DEFINES
#endif

#include "FileIO/FioTypes.h"
#include "Ut/Vec.h"
#include "FileIO/FileFormat.h"

#include <string>
#include <vector>
#include <regex>
#include <cmath>
#include <algorithm>
#include <cctype>

namespace Fio
{
    inline std::string pltToUpper(const std::string& str)
    {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return std::toupper(c);
        });
        return result;
    }

    inline std::string pltTrim(const std::string& str)
    {
        size_t start = str.find_first_not_of(" \t\n\r");
        size_t end = str.find_last_not_of(" \t\n\r");
        if (start == std::string::npos)
        {
            return "";
        }
        return str.substr(start, end - start + 1);
    }

    inline std::vector<std::string> pltSplit(const std::string& str, const std::regex& pattern)
    {
        std::vector<std::string> result;
        std::sregex_token_iterator it(str.begin(), str.end(), pattern, -1);
        std::sregex_token_iterator end;
        for (auto i = it; i != end; ++i)
        {
            if (!(*i).str().empty())
            {
                result.push_back((*i).str());
            }
        }
        return result;
    }

    /// HPGL 指令解释器 — 输出中立 IR（EntityInfo），不依赖 Engine2D 类型
    // ABI 说明：本类为 header-only 内联实现，STL 分配/释放发生在编译方，不跨 DLL 边界。
    class PltHpglInterpreter
    {
    public:
        PltHpglInterpreter(std::vector<EntityInfo>& outEntities, std::vector<std::string>& warnings)
            : m_penDown(false)
            , m_currentPos(0.0, 0.0)
            , m_lastPos(0.0, 0.0)
            , m_scale(0.025)
            , m_defaultScale(0.025)
            , m_currentPen(1)
            , m_lineWidth(0.5)
            , m_outEntities(outEntities)
            , m_warnings(warnings)
        {
        }

        void processLine(const std::string& rawLine, int lineIdx)
        {
            std::string line = pltToUpper(pltTrim(rawLine));
            if (line.empty())
            {
                return;
            }

            size_t pos = 0;
            while (pos < line.size())
            {
                while (pos < line.size() && (line[pos] == ';' || line[pos] == ' ' || line[pos] == '\t'))
                {
                    ++pos;
                }

                if (pos >= line.size())
                {
                    break;
                }

                if (!std::isalpha(static_cast<unsigned char>(line[pos])))
                {
                    ++pos;
                    continue;
                }

                size_t cmdStart = pos;
                while (pos < line.size() && std::isalpha(static_cast<unsigned char>(line[pos])))
                {
                    ++pos;
                }

                std::string cmd = line.substr(cmdStart, pos - cmdStart);

                if (cmd.size() > 2)
                {
                    std::string firstTwo = cmd.substr(0, 2);
                    if (isKnownCommand(firstTwo))
                    {
                        cmd = firstTwo;
                        pos = cmdStart + 2;
                    }
                    else
                    {
                        m_warnings.push_back(
                            "Line " + std::to_string(lineIdx + 1) + ": Unknown HPGL command '" + cmd + "'");
                        continue;
                    }
                }
                else if (cmd.size() == 1)
                {
                    m_warnings.push_back("Line " + std::to_string(lineIdx + 1) + ": Unknown HPGL command '" + cmd + "'");
                    continue;
                }

                while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
                {
                    ++pos;
                }

                size_t paramsStart = pos;
                while (pos < line.size() && line[pos] != ';')
                {
                    if (std::isalpha(static_cast<unsigned char>(line[pos])))
                    {
                        if (pos + 1 < line.size())
                        {
                            std::string maybeCmd = line.substr(pos, 2);
                            if (isKnownCommand(maybeCmd))
                            {
                                break;
                            }
                        }
                    }
                    ++pos;
                }

                std::string paramsStr = line.substr(paramsStart, pos - paramsStart);
                paramsStr = pltTrim(paramsStr);

                std::vector<std::string> params = pltSplit(paramsStr, m_regexCommaSpace);

                try
                {
                    if (cmd == "IN")
                    {
                        handleIN(params);
                    }
                    else if (cmd == "PU")
                    {
                        handlePU(params);
                    }
                    else if (cmd == "PD")
                    {
                        handlePD(params);
                    }
                    else if (cmd == "PA")
                    {
                        handlePA(params);
                    }
                    else if (cmd == "PR")
                    {
                        handlePR(params);
                    }
                    else if (cmd == "AA")
                    {
                        handleAA(params);
                    }
                    else if (cmd == "AR")
                    {
                        handleAR(params);
                    }
                    else if (cmd == "CI")
                    {
                        handleCI(params);
                    }
                    else if (cmd == "SP")
                    {
                        handleSP(params);
                    }
                    else if (cmd == "PT")
                    {
                        handlePT(params);
                    }
                    else if (cmd == "SC")
                    {
                        handleSC(params);
                    }
                    else if (cmd == "LT")
                    {
                        handleLT(params);
                    }
                    else if (cmd == "LB")
                    {
                        handleLB(params, lineIdx);
                    }
                    else if (cmd == "DI")
                    {
                        handleDI(params);
                    }
                    else if (cmd == "VS")
                    {
                        handleVS(params);
                    }
                    else if (cmd == "WU")
                    {
                        handleWU(params);
                    }
                    else if (cmd == "PW")
                    {
                        handlePW(params);
                    }
                    else
                    {
                        m_warnings.push_back(
                            "Line " + std::to_string(lineIdx + 1) + ": Unknown HPGL command '" + cmd + "'");
                    }
                }
                catch (const std::exception& e)
                {
                    m_warnings.push_back(
                        "Line " + std::to_string(lineIdx + 1) + ": Error parsing command '" + cmd + "': " + e.what());
                }
            }
        }

        void finalize()
        {
            if (m_penDown && (m_currentPos.x() != m_lastPos.x() || m_currentPos.y() != m_lastPos.y()))
            {
                emitLine(m_lastPos, m_currentPos);
            }
        }

    private:
        bool m_penDown;
        Ut::Vec2d m_currentPos;
        Ut::Vec2d m_lastPos;
        double m_scale;
        double m_defaultScale;
        int m_currentPen;
        double m_lineWidth;

        std::vector<EntityInfo>& m_outEntities;
        std::vector<std::string>& m_warnings;

        static const std::regex m_regexCommaSpace;

        static bool isKnownCommand(const std::string& cmd)
        {
            return cmd == "IN" || cmd == "PU" || cmd == "PD" || cmd == "PA" || cmd == "PR" || cmd == "AA" ||
                cmd == "AR" || cmd == "CI" || cmd == "SP" || cmd == "PT" || cmd == "SC" || cmd == "LT" || cmd == "LB" ||
                cmd == "DI" || cmd == "VS" || cmd == "WU" || cmd == "PW";
        }

        static double getParam(const std::vector<std::string>& params, int i, double def = 0.0)
        {
            if (i >= 0 && i < static_cast<int>(params.size()))
            {
                try
                {
                    return std::stod(params[i]);
                }
                catch (...)
                {
                    return def;
                }
            }
            return def;
        }

        // 输出 Line 类型 EntityInfo（两点确定线段）
        void emitLine(const Ut::Vec2d& from, const Ut::Vec2d& to)
        {
            Ut::Vec2d p1 = from * m_scale;
            Ut::Vec2d p2 = to * m_scale;

            EntityInfo info{};
            info.type = EntityType::Line;
            info.sourceId = static_cast<uint64_t>(m_outEntities.size());
            info.visible = true;
            info.line.x1 = p1.x();
            info.line.y1 = p1.y();
            info.line.x2 = p2.x();
            info.line.y2 = p2.y();
            m_outEntities.push_back(info);
        }

        // 输出 Arc 类型 EntityInfo
        void emitArc(const Ut::Vec2d& center, double radius, double startAngle, double endAngle)
        {
            Ut::Vec2d scaledCenter = center * m_scale;
            double scaledRadius = radius * m_scale;

            EntityInfo info{};
            info.type = EntityType::Arc;
            info.sourceId = static_cast<uint64_t>(m_outEntities.size());
            info.visible = true;
            info.arc.cx = scaledCenter.x();
            info.arc.cy = scaledCenter.y();
            info.arc.r = scaledRadius;
            info.arc.sa = startAngle;
            info.arc.ea = endAngle;
            m_outEntities.push_back(info);
        }

        // 输出 Circle 类型 EntityInfo
        void emitCircle(const Ut::Vec2d& center, double radius)
        {
            Ut::Vec2d scaledCenter = center * m_scale;
            double scaledRadius = radius * m_scale;

            EntityInfo info{};
            info.type = EntityType::Circle;
            info.sourceId = static_cast<uint64_t>(m_outEntities.size());
            info.visible = true;
            info.circle.cx = scaledCenter.x();
            info.circle.cy = scaledCenter.y();
            info.circle.r = scaledRadius;
            m_outEntities.push_back(info);
        }

        void handleIN(const std::vector<std::string>& /*params*/)
        {
            m_penDown = false;
            m_currentPos = Ut::Vec2d(0.0, 0.0);
            m_lastPos = m_currentPos;
            m_currentPen = 1;
            m_scale = m_defaultScale;
        }

        void handlePU(const std::vector<std::string>& params)
        {
            if (m_penDown && (m_currentPos.x() != m_lastPos.x() || m_currentPos.y() != m_lastPos.y()))
            {
                emitLine(m_lastPos, m_currentPos);
            }
            m_penDown = false;

            if (params.size() >= 2)
            {
                m_currentPos = Ut::Vec2d(getParam(params, 0), getParam(params, 1));
                m_lastPos = m_currentPos;
            }
        }

        void handlePD(const std::vector<std::string>& params)
        {
            m_penDown = true;

            for (size_t i = 0; i + 1 < params.size(); i += 2)
            {
                m_lastPos = m_currentPos;
                m_currentPos =
                    Ut::Vec2d(getParam(params, static_cast<int>(i)), getParam(params, static_cast<int>(i + 1)));
                emitLine(m_lastPos, m_currentPos);
            }
        }

        void handlePA(const std::vector<std::string>& params)
        {
            for (size_t i = 0; i + 1 < params.size(); i += 2)
            {
                m_lastPos = m_currentPos;
                m_currentPos =
                    Ut::Vec2d(getParam(params, static_cast<int>(i)), getParam(params, static_cast<int>(i + 1)));

                if (m_penDown)
                {
                    emitLine(m_lastPos, m_currentPos);
                }
            }
        }

        void handlePR(const std::vector<std::string>& params)
        {
            for (size_t i = 0; i + 1 < params.size(); i += 2)
            {
                m_lastPos = m_currentPos;
                m_currentPos = m_currentPos +
                    Ut::Vec2d(getParam(params, static_cast<int>(i)), getParam(params, static_cast<int>(i + 1)));

                if (m_penDown)
                {
                    emitLine(m_lastPos, m_currentPos);
                }
            }
        }

        void handleAA(const std::vector<std::string>& params)
        {
            if (params.size() < 3)
            {
                return;
            }

            double cx = getParam(params, 0);
            double cy = getParam(params, 1);
            double arcAngle = getParam(params, 2, 360.0);

            double radius = (m_currentPos - Ut::Vec2d(cx, cy)).length();
            double startAng = std::atan2(m_currentPos.y() - cy, m_currentPos.x() - cx);
            double endAng = startAng + arcAngle * M_PI / 180.0;

            if (m_penDown)
            {
                emitArc(Ut::Vec2d(cx, cy), radius, startAng, endAng);
            }

            m_currentPos = Ut::Vec2d(cx + radius * std::cos(endAng), cy + radius * std::sin(endAng));
        }

        void handleAR(const std::vector<std::string>& params)
        {
            if (params.size() < 3)
            {
                return;
            }

            double rx = getParam(params, 0);
            double ry = getParam(params, 1);
            double arcAngle = getParam(params, 2, 360.0);

            Ut::Vec2d center = m_currentPos + Ut::Vec2d(rx, ry);
            double radius = Ut::Vec2d(rx, ry).length();
            double startAng = std::atan2(-ry, -rx);
            double endAng = startAng + arcAngle * M_PI / 180.0;

            if (m_penDown)
            {
                emitArc(center, radius, startAng, endAng);
            }

            m_currentPos = Ut::Vec2d(center.x() + radius * std::cos(endAng), center.y() + radius * std::sin(endAng));
        }

        void handleCI(const std::vector<std::string>& params)
        {
            if (params.size() < 1)
            {
                return;
            }

            double radius = getParam(params, 0);
            if (radius > 0)
            {
                emitCircle(m_currentPos, radius);
            }
        }

        void handleSP(const std::vector<std::string>& params)
        {
            if (params.size() >= 1)
            {
                m_currentPen = static_cast<int>(getParam(params, 0, 1));
            }
        }

        void handlePT(const std::vector<std::string>& params)
        {
            if (params.size() >= 1)
            {
                m_lineWidth = getParam(params, 0, 0.5);
            }
        }

        void handleSC(const std::vector<std::string>& params)
        {
            if (params.size() >= 4)
            {
                double xMin = getParam(params, 0, 0.0);
                double xMax = getParam(params, 1, 100.0);
                double yMin = getParam(params, 2, 0.0);
                double yMax = getParam(params, 3, 100.0);
                if (xMax != xMin && yMax != yMin)
                {
                    m_scale = m_defaultScale;
                }
            }
            else if (params.size() >= 2)
            {
                double sx = getParam(params, 0, 1.0);
                double sy = getParam(params, 1, 1.0);
                if (sx > 0 && sy > 0)
                {
                    m_scale = m_defaultScale;
                }
            }
        }

        void handleLT(const std::vector<std::string>& /*params*/) {}

        void handleLB(const std::vector<std::string>& /*params*/, int lineIdx)
        {
            m_warnings.push_back(
                "Line " + std::to_string(lineIdx + 1) + ": LB (Label) command is not supported, text content ignored");
        }

        void handleDI(const std::vector<std::string>& /*params*/) {}

        void handleVS(const std::vector<std::string>& /*params*/) {}

        void handleWU(const std::vector<std::string>& /*params*/) {}

        void handlePW(const std::vector<std::string>& /*params*/) {}
    };

    inline const std::regex PltHpglInterpreter::m_regexCommaSpace{ "[,\\s]+" };
}  // namespace Fio

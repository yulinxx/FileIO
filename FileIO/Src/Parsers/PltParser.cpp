#include "FileIO/Parsers/PltParser.h"

#include "Engine/SyEntity/SyLine.h"
#include "Engine/SyEntity/SyArc.h"
#include "Engine/SyEntity/SyCircle.h"
#include "Engine/SyEntity/SyPoint.h"
#include "Ut/Vec.h"

#include <fstream>
#include <sstream>
#include <regex>
#include <cmath>
#include <algorithm>
#include <memory>
#include <chrono>
#include <cctype>

namespace Fio
{

inline std::string toUpper(const std::string& str)
{
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return std::toupper(c); });
    return result;
}

inline std::string trim(const std::string& str)
{
    size_t start = str.find_first_not_of(" \t\n\r");
    size_t end = str.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    return str.substr(start, end - start + 1);
}

inline std::vector<std::string> split(const std::string& str, const std::regex& pattern)
{
    std::vector<std::string> result;
    std::sregex_token_iterator it(str.begin(), str.end(), pattern, -1);
    std::sregex_token_iterator end;
    for (auto i = it; i != end; ++i) {
        if (!(*i).str().empty()) {
            result.push_back((*i).str());
        }
    }
    return result;
}

class PltHpglInterpreter
{
public:
    PltHpglInterpreter(VecSyEntityPtr& outEntities, std::vector<std::string>& warnings)
        : m_outEntities(outEntities)
        , m_warnings(warnings)
        , m_penDown(false)
        , m_currentPos(0.0, 0.0)
        , m_lastPos(0.0, 0.0)
        , m_scale(0.025)
        , m_currentPen(1)
        , m_lineWidth(0.5)
    {
    }

    void processLine(const std::string& rawLine, int lineIdx)
    {
        std::string line = toUpper(trim(rawLine));
        if (line.empty())
            return;

        std::string cmd;
        std::string paramsStr;

        size_t idx = 0;
        while (idx < line.size() && std::isalpha(line[idx])) {
            cmd += line[idx];
            ++idx;
        }
        paramsStr = line.substr(idx);

        std::vector<std::string> params = split(paramsStr, std::regex("[,\\s]+"));

        try {
            if (cmd == "IN")
                handleIN(params);
            else if (cmd == "PU")
                handlePU(params);
            else if (cmd == "PD")
                handlePD(params);
            else if (cmd == "PA")
                handlePA(params);
            else if (cmd == "PR")
                handlePR(params);
            else if (cmd == "AA")
                handleAA(params);
            else if (cmd == "AR")
                handleAR(params);
            else if (cmd == "CI")
                handleCI(params);
            else if (cmd == "SP")
                handleSP(params);
            else if (cmd == "PT")
                handlePT(params);
            else if (cmd == "SC")
                handleSC(params);
            else if (cmd == "LT")
                handleLT(params);
            else if (cmd == "LB")
                handleLB(params, lineIdx);
            else if (cmd == "DI")
                handleDI(params);
            else if (cmd == "VS")
                handleVS(params);
            else if (cmd == "WU")
                handleWU(params);
            else if (cmd == "PW")
                handlePW(params);
            else if (!cmd.empty() && cmd[0] != ';') {
                char buffer[100];
                sprintf(buffer, "Line %d: Unknown HPGL command '%s'", lineIdx + 1, cmd.c_str());
                m_warnings.push_back(buffer);
            }
        } catch (const std::exception& e) {
            char buffer[200];
            sprintf(buffer, "Line %d: Error parsing command '%s': %s", lineIdx + 1, cmd.c_str(), e.what());
            m_warnings.push_back(buffer);
        }
    }

    void finalize()
    {
        if (m_penDown && (m_currentPos.x() != m_lastPos.x() || m_currentPos.y() != m_lastPos.y())) {
            emitLine(m_lastPos, m_currentPos);
        }
    }

private:
    bool m_penDown;
    Ut::Vec2d m_currentPos;
    Ut::Vec2d m_lastPos;
    double m_scale;
    int m_currentPen;
    double m_lineWidth;

    VecSyEntityPtr& m_outEntities;
    std::vector<std::string>& m_warnings;

    static double getParam(const std::vector<std::string>& params, int i, double def = 0.0)
    {
        if (i >= 0 && i < static_cast<int>(params.size())) {
            try {
                return std::stod(params[i]);
            } catch (...) {
                return def;
            }
        }
        return def;
    }

    void emitLine(const Ut::Vec2d& from, const Ut::Vec2d& to)
    {
        auto lineEnt = std::make_unique<Eg::SyLine>();
        Ut::Vec2d p1 = from * m_scale;
        Ut::Vec2d p2 = to * m_scale;
        lineEnt->vPoints.push_back(p1);
        lineEnt->vPoints.push_back(p2);
        lineEnt->basePoint = p1;
        m_outEntities.push_back(std::move(lineEnt));
    }

    void emitArc(const Ut::Vec2d& center, double radius, double startAngle, double endAngle)
    {
        auto arcEnt = std::make_unique<Eg::SyArc>();
        arcEnt->basePoint = center * m_scale;
        arcEnt->dRadius = radius * m_scale;
        arcEnt->dStartAngle = startAngle;
        arcEnt->dEndAngle = endAngle;
        m_outEntities.push_back(std::move(arcEnt));
    }

    void emitCircle(const Ut::Vec2d& center, double radius)
    {
        auto circleEnt = std::make_unique<Eg::SyCircle>();
        circleEnt->basePoint = center * m_scale;
        circleEnt->dRadius = radius * m_scale;
        m_outEntities.push_back(std::move(circleEnt));
    }

    void handleIN(const std::vector<std::string>& /*params*/)
    {
        m_penDown = false;
        m_currentPos = Ut::Vec2d(0.0, 0.0);
        m_lastPos = m_currentPos;
        m_currentPen = 1;
    }

    void handlePU(const std::vector<std::string>& params)
    {
        if (m_penDown && (m_currentPos.x() != m_lastPos.x() || m_currentPos.y() != m_lastPos.y())) {
            emitLine(m_lastPos, m_currentPos);
        }
        m_penDown = false;

        if (params.size() >= 2) {
            m_currentPos = Ut::Vec2d(getParam(params, 0), getParam(params, 1));
            m_lastPos = m_currentPos;
        }
    }

    void handlePD(const std::vector<std::string>& params)
    {
        m_penDown = true;

        for (size_t i = 0; i + 1 < params.size(); i += 2) {
            m_lastPos = m_currentPos;
            m_currentPos = Ut::Vec2d(getParam(params, static_cast<int>(i)), getParam(params, static_cast<int>(i + 1)));
            emitLine(m_lastPos, m_currentPos);
        }
    }

    void handlePA(const std::vector<std::string>& params)
    {
        for (size_t i = 0; i + 1 < params.size(); i += 2) {
            m_lastPos = m_currentPos;
            m_currentPos = Ut::Vec2d(getParam(params, static_cast<int>(i)), getParam(params, static_cast<int>(i + 1)));

            if (m_penDown) {
                emitLine(m_lastPos, m_currentPos);
            }
        }
    }

    void handlePR(const std::vector<std::string>& params)
    {
        for (size_t i = 0; i + 1 < params.size(); i += 2) {
            m_lastPos = m_currentPos;
            m_currentPos = m_currentPos + Ut::Vec2d(getParam(params, static_cast<int>(i)), getParam(params, static_cast<int>(i + 1)));

            if (m_penDown) {
                emitLine(m_lastPos, m_currentPos);
            }
        }
    }

    void handleAA(const std::vector<std::string>& params)
    {
        if (params.size() < 3)
            return;

        double cx = getParam(params, 0);
        double cy = getParam(params, 1);
        double arcAngle = getParam(params, 2, 360.0);

        double radius = (m_currentPos - Ut::Vec2d(cx, cy)).length();
        double startAng = std::atan2(m_currentPos.y() - cy, m_currentPos.x() - cx);
        double endAng = startAng + arcAngle * M_PI / 180.0;

        if (m_penDown) {
            emitArc(Ut::Vec2d(cx, cy), radius, startAng, endAng);
        }

        m_currentPos = Ut::Vec2d(cx + radius * std::cos(endAng),
                                  cy + radius * std::sin(endAng));
    }

    void handleAR(const std::vector<std::string>& params)
    {
        if (params.size() < 3)
            return;

        double rx = getParam(params, 0);
        double ry = getParam(params, 1);
        double arcAngle = getParam(params, 2, 360.0);

        Ut::Vec2d center = m_currentPos + Ut::Vec2d(rx, ry);
        double radius = Ut::Vec2d(rx, ry).length();
        double startAng = std::atan2(-ry, -rx);
        double endAng = startAng + arcAngle * M_PI / 180.0;

        if (m_penDown) {
            emitArc(center, radius, startAng, endAng);
        }

        m_currentPos = Ut::Vec2d(m_currentPos.x() + rx + radius * std::cos(endAng),
                                  m_currentPos.y() + ry + radius * std::sin(endAng));
    }

    void handleCI(const std::vector<std::string>& params)
    {
        if (params.size() < 1)
            return;

        double radius = getParam(params, 0);
        if (radius > 0) {
            emitCircle(m_currentPos, radius);
        }
    }

    void handleSP(const std::vector<std::string>& params)
    {
        if (params.size() >= 1) {
            m_currentPen = static_cast<int>(getParam(params, 0, 1));
        }
    }

    void handlePT(const std::vector<std::string>& params)
    {
        if (params.size() >= 1) {
            m_lineWidth = getParam(params, 0, 0.5);
        }
    }

    void handleSC(const std::vector<std::string>& params)
    {
        if (params.size() >= 2) {
            m_scale = getParam(params, 0, m_scale) / getParam(params, 1, 100.0);
        }
    }

    void handleLT(const std::vector<std::string>& /*params*/)
    {
    }

    void handleLB(const std::vector<std::string>& /*params*/, int lineIdx)
    {
        char buffer[50];
        sprintf(buffer, "Line %d: LB (Label) command is not supported", lineIdx + 1);
        m_warnings.push_back(buffer);
    }

    void handleDI(const std::vector<std::string>& /*params*/)
    {
    }

    void handleVS(const std::vector<std::string>& /*params*/)
    {
    }

    void handleWU(const std::vector<std::string>& /*params*/)
    {
    }

    void handlePW(const std::vector<std::string>& /*params*/)
    {
    }
};

FileFormat PltParser::format() const
{
    return FileFormat::PLT;
}

std::string PltParser::formatName() const
{
    return "HPGL Plot (PLT)";
}

std::vector<std::string> PltParser::supportedExtensions() const
{
    return { "plt", "hpgl" };
}

ParseResult PltParser::parse(const std::string& filePath, VecSyEntityPtr& outEntities)
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file)
        return ParseResult::fail("Cannot open PLT file: " + filePath);

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    if (content.empty())
        return ParseResult::fail("PLT file is empty: " + filePath);

    int nonPrintable = 0;
    size_t checkSize = std::min(content.size(), static_cast<size_t>(4096));
    for (size_t i = 0; i < checkSize; ++i) {
        unsigned char c = static_cast<unsigned char>(content[i]);
        if (c < 32 && c != '\t' && c != '\n' && c != '\r')
            nonPrintable++;
    }
    bool likelyBinary = (content.size() > 256 && nonPrintable > content.size() / 10);

    std::vector<std::string> warnings;

    if (likelyBinary) {
        return ParseResult::fail(
            "File appears to be binary DMPL format, not text HPGL. "
            "Binary PLT format is not yet supported.");
    }

    std::replace(content.begin(), content.end(), ';', '\n');

    std::vector<std::string> lines = split(content, std::regex("[\r\n]+"));

    const int MAX_ENTITIES = 1000000;
    try {
        PltHpglInterpreter interpreter(outEntities, warnings);

        for (size_t lineIdx = 0; lineIdx < lines.size(); ++lineIdx) {
            if (outEntities.size() >= MAX_ENTITIES) {
                char buffer[100];
                sprintf(buffer, "Entity limit (%d) reached, stopping parse.", MAX_ENTITIES);
                warnings.push_back(buffer);
                break;
            }
            interpreter.processLine(lines[lineIdx], static_cast<int>(lineIdx));
        }

        interpreter.finalize();

    } catch (const std::exception& ex) {
        return ParseResult::fail(
            std::string("Exception during PLT parsing: ") + ex.what(),
            warnings);
    }

    ParseResult result = ParseResult::ok();
    result.warnings = warnings;
    return result;
}

} // namespace Fio

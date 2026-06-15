#include "FileIO/Parsers/PltParser.h"
#include "FileIO/Parsers/PltHpglInterpreter.h"

#include <fstream>
#include <sstream>
#include <regex>
#include <cmath>
#include <algorithm>
#include <memory>
#include <cctype>
#include <filesystem>

namespace Fio
{
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
        std::filesystem::path fsPath = std::filesystem::u8path(filePath);
        std::ifstream file(fsPath, std::ios::binary);
        if (!file)
            return ParseResult::fail("Cannot open PLT file: " + filePath);

        std::vector<std::string> warnings;
        const int MAX_ENTITIES = 1000000;

        {
            std::vector<char> headerBuf(4096);
            file.read(headerBuf.data(), static_cast<std::streamsize>(headerBuf.size()));
            std::streamsize bytesRead = file.gcount();

            if (bytesRead == 0)
                return ParseResult::fail("PLT file is empty: " + filePath);

            int nonPrintable = 0;
            for (std::streamsize i = 0; i < bytesRead; ++i)
            {
                unsigned char c = static_cast<unsigned char>(headerBuf[i]);
                if (c < 32 && c != '\t' && c != '\n' && c != '\r')
                    nonPrintable++;
            }
            bool likelyBinary = (bytesRead > 256 && nonPrintable > bytesRead / 10);

            if (likelyBinary)
            {
                return ParseResult::fail(
                    "File appears to be binary DMPL format, not text HPGL. "
                    "Binary PLT format is not yet supported.");
            }

            file.clear();
            file.seekg(0, std::ios::beg);
        }

        try
        {
            PltHpglInterpreter interpreter(outEntities, warnings);

            std::string line;
            int lineIdx = 0;

            while (std::getline(file, line))
            {
                if (outEntities.size() >= MAX_ENTITIES)
                {
                    warnings.push_back("Entity limit (" + std::to_string(MAX_ENTITIES) + ") reached, stopping parse.");
                    break;
                }

                line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
                interpreter.processLine(line, lineIdx);
                ++lineIdx;
            }

            interpreter.finalize();
        }
        catch (const std::exception& ex)
        {
            return ParseResult::fail(
                std::string("Exception during PLT parsing: ") + ex.what(),
                warnings);
        }

        ParseResult result = ParseResult::ok();
        result.warnings = warnings;
        return result;
    }
} // namespace Fio
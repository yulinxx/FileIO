#include "FileIO/Parsers/UgParser.h"

namespace Fio
{
    ParseResult UgParser::parse(const std::string& filePath, VecSyEntityPtr& outEntities)
    {
        (void)outEntities;
        (void)filePath;

        return ParseResult::fail(
            "UG/NX file parsing is not yet implemented.\n\n"
            "Options:\n"
            "1. For .prt files: NX Open API (requires Siemens NX license)\n"
            "2. For IGES/STEP: Integrate Open CASCADE (OCCT) library\n"
            "   Install via vcpkg: vcpkg install opencascade\n"
            "   Then add the parser implementation."
        );
    }
} // namespace Fio

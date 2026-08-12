#include "FileIO/Parsers/UgParser.h"

namespace Fio
{
    ParseResult UgParser::parse(const char* /*filePath*/, VecSyEntityPtr& /*outEntities*/)
    {

        return ParseResult::fail(
            "UG/NX file parsing is not yet implemented.\n\n"
            "Options:\n"
            "1. For .prt files: NX Open API (requires Siemens NX license)\n"
            "2. For IGES: Integrate Open CASCADE IGES reader\n"
            "3. For STEP/STP: use the dedicated STEP importer (File → Open *.stp)"
        );
    }
} // namespace Fio
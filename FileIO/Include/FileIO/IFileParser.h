#pragma once

#include "FileIO/FileIOAPI.h"
#include "FileIO/FileFormat.h"
#include "FileIO/FileIOError.h"

#include <string>
#include <vector>
#include <memory>

namespace Fio
{
    class FILEIO_API IFileParser
    {
    public:
        virtual ~IFileParser() = default;

        virtual FileFormat format() const = 0;

        virtual std::vector<std::string> supportedExtensions() const = 0;

        virtual std::string formatName() const = 0;

        virtual ParseResult parse(const std::string& filePath, VecSyEntityPtr& outEntities) = 0;
    };
} // namespace Fio

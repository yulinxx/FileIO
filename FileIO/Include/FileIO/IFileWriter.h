#pragma once

#include "FileIO/FileIOAPI.h"
#include "FileIO/FileFormat.h"
#include "FileIO/FileIOError.h"

#include <string>
#include <vector>
#include <memory>

namespace Fio
{
    class FILEIO_API IFileWriter
    {
    public:
        virtual ~IFileWriter() = default;

        virtual FileFormat format() const = 0;

        virtual std::string formatName() const = 0;

        virtual std::string defaultExtension() const = 0;

        virtual WriteResult write(const std::string& filePath, const VecSyEntityPtr& entities) = 0;
    };
} // namespace Fio

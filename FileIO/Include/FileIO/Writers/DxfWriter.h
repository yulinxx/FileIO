#pragma once

#include "FileIO/IFileWriter.h"

namespace Fio
{
    class FILEIO_API DxfWriter : public IFileWriter
    {
    public:
        FileFormat format() const override;
        std::string formatName() const override;
        std::string defaultExtension() const override;
        WriteResult write(const std::string& filePath, const VecSyEntityPtr& entities) override;
    };
} // namespace Fio

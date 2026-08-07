#pragma once

#include "FileIO/IFileWriter.h"
#include "FileIOInternal.h"

namespace Fio
{
    class FILEIO_API SvgWriter : public IFileWriter, public ILegacyWriter
    {
    public:
        FileFormat format() const override;
        size_t formatName(char* buffer, size_t bufferSize) const override;
        size_t defaultExtension(char* buffer, size_t bufferSize) const override;
        WriteResult write(const char* filePath, const VecSyEntityPtr& entities) override;
    };
} // namespace Fio
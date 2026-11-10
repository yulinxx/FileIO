#pragma once

#include "FileIO/IFileWriter.h"
#include "FileIO/ILegacyTypes.h"

namespace Fio
{
    /**
     * @brief UG/NX 文件写入器
     *
     * 输出 IGES 5.3 格式，Siemens NX / Unigraphics 可直接导入。
     * 支持的图元类型：
     *   - LINE (116)  → AprLine
     *   - CIRCLE (102/100) → AprCircle
     *   - ARC (102/100) → AprArc
     *   - ELLIPSE (102/104) → AprEllipse
     *   - POLYGON → 离散为 LWPOLYLINE (106)
     *   - BEZIER/BEZIER2/SPLINE → 离散为 SPLINE (126)
     *   - SMARTLINE → 递归导出子段
     */
    class FILEIO_API UgWriter : public IFileWriter, public ILegacyWriter
    {
    public:
        FileFormat format() const override;
        size_t formatName(char* buffer, size_t bufferSize) const override;
        size_t defaultExtension(char* buffer, size_t bufferSize) const override;
        WriteResult write(const char* filePath, const VecAprEntityPtr& entities) override;

    private:
        struct IgesEntity
        {
            int sequenceNumber = 0;
            int entityType = 0;
            std::string parameterData;
            int directoryPointer = 0;
        };
    };
}  // namespace Fio
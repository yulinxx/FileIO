/**
 * @file UnitTransform.cpp
 * @brief 导入单位换算实现：单位名换算 + 对 IR 的统一缩放
 */
#include "FileIO/UnitTransform.h"

#include "IrProjector.h"
#include "IrTransform.h"

#include "Log/SyLogger.h"

#include <cmath>
#include <cstring>

namespace Fio
{
    namespace
    {
        /// 单位名 → 毫米因子；未知返回 0.0
        double lookupToMm(const char* name)
        {
            if (name == nullptr || name[0] == '\0')
            {
                return 0.0;
            }
            // 与 UnitManager（UI 层）的毫米基准保持一致：mm=1, cm=10, m=1000, in=25.4, mil, px
            if (std::strcmp(name, "mm") == 0)
            {
                return 1.0;
            }
            if (std::strcmp(name, "cm") == 0)
            {
                return 10.0;
            }
            if (std::strcmp(name, "dm") == 0)
            {
                return 100.0;
            }
            if (std::strcmp(name, "m") == 0)
            {
                return 1000.0;
            }
            if (std::strcmp(name, "dam") == 0)
            {
                return 10000.0;
            }
            if (std::strcmp(name, "hm") == 0)
            {
                return 100000.0;
            }
            if (std::strcmp(name, "km") == 0)
            {
                return 1000000.0;
            }
            if (std::strcmp(name, "mil") == 0)
            {
                return 0.0254;
            }
            if (std::strcmp(name, "um") == 0 || std::strcmp(name, "micron") == 0)
            {
                return 0.001;
            }
            if (std::strcmp(name, "nm") == 0)
            {
                return 1e-6;
            }
            if (std::strcmp(name, "angstrom") == 0)
            {
                return 1e-7;
            }
            if (std::strcmp(name, "inch") == 0 || std::strcmp(name, "in") == 0)
            {
                return 25.4;
            }
            if (std::strcmp(name, "ft") == 0 || std::strcmp(name, "foot") == 0)
            {
                return 304.8;
            }
            if (std::strcmp(name, "yd") == 0)
            {
                return 914.4;
            }
            if (std::strcmp(name, "px") == 0)
            {
                return 25.4 / 72.0;
            }
            return 0.0;
        }
    }  // namespace

    double unitToMillimeters(const char* unitName)
    {
        return lookupToMm(unitName);
    }

    double unitScaleRatio(const char* fromUnit, const char* toUnit)
    {
        const double fromMm = lookupToMm(fromUnit);
        const double toMm = lookupToMm(toUnit);
        // 任一单位无法识别（含源未声明单位）：视为与目标同单位，不做换算。
        // 这是保守默认：不能保证换算是正确的，就不换算，避免把几何放错比例。
        if (fromMm <= 0.0 || toMm <= 0.0)
        {
            return 1.0;
        }
        return fromMm / toMm;
    }

    bool scaleParseResult(FioParseResult* result, double scale)
    {
        if (result == nullptr)
        {
            SY_ERROR("[UnitTransform] scaleParseResult: null FioParseResult");
            return false;
        }
        if (!std::isfinite(scale) || scale <= 0.0)
        {
            SY_ERRORF("[UnitTransform] scaleParseResult: invalid scale=%f", scale);
            return false;
        }
        if (std::fabs(scale - 1.0) < 1e-12)
        {
            return true;
        }
        if (result->entityCount == 0)
        {
            return true;
        }

        // entities / extensionBlob 都是指向当前线程 thread_local 缓冲区的借用指针。
        // 本函数必须在「解析返回之后、下一次解析之前」的同线程调用（见头文件内存契约），
        // 因此这里就地修改是安全的；transformEntity 读旧 blob、追加新 blob 到同一发布器。
        auto& pub = IrPublisher::threadLocal();
        EntityInfo* entities = const_cast<EntityInfo*>(result->entities);
        const uint8_t* srcBlob = result->extensionBlob.data;
        const std::size_t srcBlobSize = result->extensionBlob.size;
        const IrXform xf = IrXform::scale(scale, scale);

        for (uint32_t i = 0; i < result->entityCount; ++i)
        {
            EntityInfo& info = entities[i];
            if (info.type == EntityType::Unknown)
            {
                continue;
            }
            if (!transformEntity(info, srcBlob, srcBlobSize, xf, pub, pub.warnings()))
            {
                // 无法安全变换（畸形扩展数据等）：置为 Unknown，消费侧会跳过，避免越界读取
                info.type = EntityType::Unknown;
                pub.warnings().push_back("Entity dropped: cannot be scaled safely");
            }
        }

        // 变换后的扩展数据追加到了发布器缓冲区，offset/size 已随各图元更新；
        // 结果里的 blob 指针与告警计数必须指向换算后的最新状态。
        result->extensionBlob.data = pub.blob().data();
        result->extensionBlob.size = pub.blob().size();
        result->warningCount = static_cast<uint32_t>(pub.warnings().size());
        return true;
    }
}  // namespace Fio
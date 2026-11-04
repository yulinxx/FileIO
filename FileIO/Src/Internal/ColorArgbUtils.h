#pragma once

#include "Ut/Color.h"

#include <cstdint>

namespace Fio
{
    namespace detail
    {
        // Ut::Color -> ARGB uint32（高 8 位为 alpha）
        inline uint32_t colorToArgb(const Ut::Color& c)
        {
            return (static_cast<uint32_t>(c.alpha255() & 0xFF) << 24) |
                (static_cast<uint32_t>(c.red255() & 0xFF) << 16) |
                (static_cast<uint32_t>(c.green255() & 0xFF) << 8) |
                (static_cast<uint32_t>(c.blue255() & 0xFF));
        }

        // 历史数据 alpha 位为 0 时按不透明处理：
        // 旧版本没有 alpha 入口，0 只可能来自缺省/未初始化，不可能是真实全透明
        inline uint32_t normalizeLegacyArgb(uint32_t argb)
        {
            return ((argb >> 24) == 0) ? (argb | 0xFF000000u) : argb;
        }

        // ARGB uint32 -> Ut::Color（alpha 保留，历史零值按不透明）
        inline Ut::Color argbToColor(uint32_t argb)
        {
            const uint32_t fixed = normalizeLegacyArgb(argb);
            return Ut::Color::fromRGB255(
                static_cast<int>((fixed >> 16) & 0xFF),
                static_cast<int>((fixed >> 8) & 0xFF),
                static_cast<int>(fixed & 0xFF),
                static_cast<int>((fixed >> 24) & 0xFF));
        }
    }  // namespace detail
}  // namespace Fio

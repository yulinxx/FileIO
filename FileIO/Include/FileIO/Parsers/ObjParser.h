#pragma once

#include "FileIO/IFileParser.h"

namespace Fio
{
    /// Wavefront OBJ 解析器
    ///
    /// 为什么要在 FileIO 里做：此前 OBJ 是唯一**绕过 FileIO** 的导入格式——
    /// 上层直接调 Engine3D 的 ObjLoader，于是 OBJ 既拿不到 IR 的群组/单位通道，
    /// 也没有 FileIO 这边的体量上限与越界保护，FormatRegistry 里注册的 .obj 形同虚设。
    ///
    /// 支持范围（与 STL 保持同一套 IR 出口）：
    ///   - `v` / `vn` / `f`（含多边形扇形三角化、负数索引、v/vt/vn 三段式索引）
    ///   - `o` 物体 → 顶层群组；`g` 组与 `usemtl` 材质切换 → 子群组
    ///   - 缺失法线时按面法线补齐
    /// 不支持（会记 warning 而非静默丢弃）：`vt` 纹理坐标、`mtllib` 材质库、
    /// 自由曲面（`curv`/`surf`）、平滑组 `s`。
    class FILEIO_API ObjParser : public IFileParser
    {
    public:
        ObjParser() = default;
        ~ObjParser() override = default;

        FileFormat format() const override;
        size_t formatName(char* buffer, size_t bufferSize) const override;
        void forEachSupportedExtension(void (*visitor)(const char*, void*), void* ctx) const override;

        FioParseResult parseToIR(const char* filePath) override;
    };
}  // namespace Fio

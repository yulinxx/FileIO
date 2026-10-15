#include "FileIO/FileIOManager.h"

#include <cstdio>
#include <fstream>

// ============================================================
//  FileIO 使用示例
//  编译后执行 SyFileIOExample.exe（在当前目录生成两个小文件）
//
//  演示 FileIOManager 的基本流程：
//    格式查询 -> 写一个最小 DXF -> 导入 -> 释放图元 -> 导出
// ============================================================

int main()
{
    std::printf("=== SyFileIOExample ===\n");

    // ---- 1. 写一个最小 DXF（一条 LINE）-----------------------------------
    const char* dxfPath = "SyFileIOExample.dxf";
    {
        std::ofstream out(dxfPath);
        out << "0\nSECTION\n2\nENTITIES\n"
            << "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n100\n21\n50\n"
            << "0\nENDSEC\n0\nEOF\n";
    }

    // ---- 2. 格式查询 ------------------------------------------------------
    Fio::FileIOManager manager;
    std::printf("canImport('%s') = %d, detectFormat = %d\n",
        dxfPath, (int)manager.canImport(dxfPath), (int)manager.detectFormat(dxfPath));

    char exts[256] = {};
    const size_t needed = manager.supportedImportExtensions(exts, sizeof(exts));
    std::printf("import extensions (%zu): %s\n", needed, exts);

    // ---- 3. 导入 ----------------------------------------------------------
    char error[512] = {};
    Eg::SyEntity** entities = nullptr;
    size_t count = 0;
    if (manager.importFile(dxfPath, Fio::FileFormat::DXF,
            &entities, &count, error, sizeof(error)))
    {
        std::printf("import ok, entities = %zu\n", count);
        // 成功导入取得图元所有权，用 deleteEntities 释放整个数组
        Fio::FileIOManager::deleteEntities(entities, count);
    }
    else
    {
        std::printf("import failed: %s\n", error);
    }

    // ---- 4. 导出（空图元集合）--------------------------------------------
    char exportError[512] = {};
    if (manager.exportFile("SyFileIOExample_out.dxf", Fio::FileFormat::DXF,
            nullptr, 0, exportError, sizeof(exportError)))
        std::printf("export ok -> SyFileIOExample_out.dxf\n");
    else
        std::printf("export failed: %s\n", exportError);

    return 0;
}

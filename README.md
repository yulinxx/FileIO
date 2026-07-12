# FileIO - 文件导入导出模块

## 概述

FileIO 是 SanYi CAD 系统的文件 I/O 动态库，负责各种格式文件的导入、导出和保存功能。采用**策略模式 + 工厂模式 + 外观模式**设计，实现高度解耦、易于扩展的架构。

---

## 目录结构

```
FileIO/
├── CMakeLists.txt                    # 外层项目配置
├── Config.txt                        # 模块配置文件
├── README.md                         # 本文档
└── FileIO/                           # 库源码目录
    ├── CMakeLists.txt                # 库构建配置
    ├── Include/FileIO/               # 头文件目录
    │   ├── FileIOAPI.h               # DLL 导出宏定义 (C++ API / C API)
    │   ├── FileIOExport.h            # C 语言跨 IDE 接口 (extern "C")
    │   ├── FileFormat.h              # 文件格式枚举
    │   ├── FileIOError.h             # 解析/写出结果结构
    │   ├── FileIOUtils.h             # 工具类 (TempFileCopy, generateHash)
    │   ├── IFileParser.h             # 文件解析器接口 (策略模式)
    │   ├── IFileWriter.h             # 文件写出器接口 (策略模式)
    │   ├── FileParserFactory.h       # 解析器工厂 (工厂模式)
    │   ├── FileWriterFactory.h       # 写出器工厂 (工厂模式)
    │   ├── FileIOManager.h           # 统一管理器 (外观模式)
    │   ├── ImageUtils.h              # 图片工具函数
    │   ├── SyDocument.h              # 文档数据模型
    │   ├── SyDocument3D.h            # 3D 文档扩展
    │   ├── SySerializer.h            # 序列化器
    │   ├── SyCryptoProvider.h        # 加密策略
    │   └── Parsers/                  # 各格式解析器头文件
    │   ├── Writers/                  # 各格式写出器头文件
    └── Src/                          # 源文件目录
        ├── FileIOManager.cpp         # 管理器实现
        ├── FileParserFactory.cpp     # 解析器工厂实现
        ├── FileWriterFactory.cpp     # 写出器工厂实现
        ├── FileIOExport.cpp          # C API 实现 (跨 IDE 接口)
        ├── FileSerializer.cpp        # 序列化器
        ├── SyDocument.cpp            # 文档模型
        ├── ImageUtils.cpp            # 图片工具
        ├── SyCryptoProvider.cpp      # 加密实现
        └── Parsers/                  # 各格式解析器实现
        └── Writers/                  # 各格式写出器实现
```

---

## 架构设计

### 设计模式

| 模式 | 类 | 用途 |
|------|-----|------|
| **策略模式** | `IFileParser` / `IFileWriter` | 每种文件格式独立实现解析/写出逻辑，互不干扰 |
| **工厂模式** | `FileParserFactory` / `FileWriterFactory` | 通过注册表动态创建解析器/写出器实例 |
| **外观模式** | `FileIOManager` | 对外提供统一简洁的 API，隐藏内部复杂性 |

### 类关系图

```
                    ┌─────────────────────┐
                    │   FileIOManager      │  ← 外观模式 (Facade)
                    │   (统一 API)          │
                    └──────┬──────┬────────┘
                           │      │
              ┌────────────┘      └────────────┐
              ▼                                ▼
    ┌─────────────────┐              ┌─────────────────┐
    │ FileParserFactory│              │ FileWriterFactory│  ← 工厂模式 (Factory)
    │ createParser()   │              │ createWriter()   │
    └────────┬────────┘              └────────┬────────┘
             │                                │
    ┌────────┼────────┬──────────┐            │
    ▼        ▼        ▼          ▼            ▼
┌───────┐┌───────┐┌───────┐┌──────────┐  ┌──────────┐
│ Dxf   ││ Plt   ││ Ug    ││ 未来扩展  │  │ 未来扩展  │
│Parser ││Parser ││Parser ││ SvgParser│  │ DxfWriter│  ← 策略模式
└───────┘└───────┘└───────┘└──────────┘  └──────────┘   (Strategy)
```

### 核心接口

#### IFileParser - 文件解析器接口

```cpp
class IFileParser
{
public:
    virtual FileFormat format() const = 0;                    // 返回格式类型
    virtual QString formatName() const = 0;                   // 返回格式名称
    virtual QStringList supportedExtensions() const = 0;      // 返回支持的扩展名
    virtual ParseResult parse(const QString& filePath, 
                              VecSyEntityPtr& outEntities) = 0;  // 解析文件
};
```

#### IFileWriter - 文件写出器接口

```cpp
class IFileWriter
{
public:
    virtual FileFormat format() const = 0;                    // 返回格式类型
    virtual QString formatName() const = 0;                   // 返回格式名称
    virtual QString defaultExtension() const = 0;            // 返回默认扩展名
    virtual WriteResult write(const QString& filePath,
                              const VecSyEntityPtr& entities) = 0;  // 写出文件
};
```

#### FileIOManager - 统一管理器

```cpp
class FileIOManager : public QObject
{
public:
    // 导入文件
    ParseResult importFile(const QString& filePath, VecSyEntityPtr& outEntities);
    ParseResult importFile(const QString& filePath, FileFormat format, VecSyEntityPtr& outEntities);
    
    // 导出文件
    WriteResult exportFile(const QString& filePath, const VecSyEntityPtr& entities);
    WriteResult exportFile(const QString& filePath, FileFormat format, const VecSyEntityPtr& entities);
    
    // 格式检测
    FileFormat detectFormat(const QString& filePath) const;
    
    // 查询
    QStringList supportedImportExtensions() const;
    QStringList supportedExportExtensions() const;
    bool canImport(const QString& filePath) const;
    bool canExport(FileFormat format) const;

signals:
    void sigImportStarted(const QString& filePath);
    void sigImportFinished(const QString& filePath, bool success);
    void sigExportStarted(const QString& filePath);
    void sigExportFinished(const QString& filePath, bool success);
};
```

---

## 文件导入流程

### 整体导入流程

```
用户操作 (菜单/工具栏)
        ↓
   FileManager.openFile() / importFile()
        ↓
   FileIOManager.importFile()
        ↓
   ┌─────────────────────────────────────────┐
   │ 1. 格式检测 (detectFormat)             │
   │    - 根据扩展名映射到 FileFormat       │
   └─────────────────────────────────────────┘
        ↓
   ┌─────────────────────────────────────────┐
   │ 2. 解析器创建 (FileParserFactory)       │
   │    - 通过工厂模式获取对应解析器        │
   └─────────────────────────────────────────┘
        ↓
   ┌─────────────────────────────────────────┐
   │ 3. 文件解析 (Parser.parse())            │
   │    - 读取文件内容                      │
   │    - 解析数据结构                      │
   │    - 转换为 SyEntity 图元              │
   └─────────────────────────────────────────┘
        ↓
   ┌─────────────────────────────────────────┐
   │ 4. 结果处理                            │
   │    - 成功: 返回实体列表                │
   │    - 失败: 返回错误信息                │
   └─────────────────────────────────────────┘
        ↓
   SceneManager.addEntities()
        ↓
   RenderWidget.update()
```

### DXF 格式导入流程

**文件结构**: DXF 是 AutoCAD 图形交换格式，采用 ASCII 或二进制编码。

**解析流程**:
```
1. 打开文件，检测编码格式 (ASCII/Binary)
2. 读取 HEADER 段 - 获取图纸基本信息
3. 读取 TABLES 段 - 获取图层、样式、线型定义
4. 读取 BLOCKS 段 - 获取块定义
5. 读取 ENTITIES 段 - 解析图形实体
   - LINE, POLYLINE, CIRCLE, ARC, TEXT 等
6. 读取 OBJECTS 段 - 处理扩展数据
```

**技术实现细节**:
- **坐标系统**: DXF 使用右手坐标系，Y轴向上；内部转换为系统坐标系
- **图层处理**: 自动创建图层，保持原始颜色和线型
- **颜色转换**: DXF 颜色索引 → RGB 值
- **块引用**: 支持 INSERT 实体，递归展开块定义

### PLT 格式导入流程

**文件结构**: HPGL (Hewlett-Packard Graphics Language) 是绘图仪控制语言。

**解析流程**:
```
1. 逐行读取 HPGL 命令
2. 解析命令参数 (如 PA, PD, PU, CI 等)
3. 维护当前画笔位置状态
4. 转换命令为图形实体：
   - PA (Plot Absolute): 移动画笔
   - PD (Pen Down): 开始绘制
   - PU (Pen Up): 停止绘制
   - CI (Circle): 绘制圆
   - AR (Arc): 绘制圆弧
   - LT (Line Type): 设置线型
```

**技术实现细节**:
- **单位转换**: PLT 使用绘图单位，需转换为系统单位 (mm)
- **画笔映射**: PLT 画笔号 → 颜色/线型映射
- **圆弧逼近**: HPGL AR 命令使用中心+角度，转换为圆弧实体
- **多笔处理**: 支持多画笔切换，映射到不同图层

### 位图格式导入流程 (PNG/JPG/BMP 等)

**特殊说明**: 位图导入不经过 FileIO 模块，直接由 UI 层处理。

**流程**:
```
FileMenuAdapter::sigImportImage / BitmapInputTool
        ↓
   QFileDialog 选择文件
        ↓
   QImage 加载图片
        ↓
   转换为 RGBA8888 格式
        ↓
   RenderWidget.setBitmapRGBA()
        ↓
   OpenGL 纹理上传与渲染
```

**技术实现细节**:
- **格式支持**: 支持 PNG, JPG, JPEG, BMP, TGA, TIFF, GIF, WebP 等
- **颜色空间**: 统一转换为 RGBA 格式
- **渲染方式**: 使用 OpenGL 纹理贴图，作为背景层显示
- **坐标映射**: 图片左下角对齐到视图原点
- **自动适配**: 导入后自动 zoomToFit 适配视图

### 导入流程时序图

```
用户      FileManager   FileIOManager   ParserFactory    Parser      SceneManager
  │           │               │                │             │             │
  │  openFile │               │                │             │             │
  │──────────>│               │                │             │             │
  │           │  importFile   │                │             │             │
  │           │──────────────>│                │             │             │
  │           │               │  detectFormat  │             │             │
  │           │               │───────────────>│             │             │
  │           │               │   format       │             │             │
  │           │               │<───────────────│             │             │
  │           │               │  createParser  │             │             │
  │           │               │───────────────>│             │             │
  │           │               │   parser       │             │             │
  │           │               │<───────────────│             │             │
  │           │               │    parse()     │             │             │
  │           │               │──────────────────────────────>│             │
  │           │               │                │             │  解析文件   │
  │           │               │                │             │─────────────>│
  │           │               │                │             │   entities  │
  │           │               │                │             │<─────────────│
  │           │               │   result       │             │             │
  │           │               │<──────────────────────────────│             │
  │           │   result      │                │             │             │
  │           │<──────────────│                │             │             │
  │           │ addEntities   │                │             │             │
  │           │────────────────────────────────────────────────────────────>│
  │ success   │               │                │             │             │
  │<──────────│               │                │             │             │
```

---

## 支持的文件格式

| 格式 | 扩展名 | 导入 | 导出 | 解析器 | 说明 |
|------|--------|------|------|--------|------|
| DXF | .dxf | ✅ | ❌ | `DxfParser` | AutoCAD DXF 格式，支持 ASCII 和 Binary |
| PLT | .plt, .hpgl | ✅ | ❌ | `PltParser` | HPGL 绘图仪控制语言 |
| UG/NX | .prt, .igs, .iges, .stp, .step | ⏳ | ❌ | - | Siemens NX 格式 (待实现) |
| SVG | .svg | ⏳ | ❌ | - | SVG 矢量图形格式 (待实现) |
| PDF | .pdf | ⏳ | ❌ | - | PDF 文档格式 (待实现) |
| AI | .ai | ⏳ | ❌ | - | Adobe Illustrator 格式 (待实现) |
| Native | .sy | ❌ | ❌ | - | SanYi 原生格式 (待实现) |
| BMP | .bmp | ❌ | ⏳ | - | 位图格式 |
| PNG | .png | ❌ | ⏳ | - | PNG 无损压缩格式 |

### 位图支持说明

位图文件（PNG/JPG/BMP 等）的导入不经过 FileIO 模块，而是由 UI 层直接处理：

| 格式 | 扩展名 | 导入方式 | 说明 |
|------|--------|----------|------|
| PNG | .png | UI层 | 通过 `QImage` 加载 |
| JPG/JPEG | .jpg, .jpeg | UI层 | 通过 `QImage` 加载 |
| BMP | .bmp | UI层 | 通过 `QImage` 加载 |
| TGA | .tga | UI层 | 通过 `QImage` 加载 |
| TIFF | .tiff, .tif | UI层 | 通过 `QImage` 加载 |
| GIF | .gif | UI层 | 通过 `QImage` 加载 |
| WebP | .webp | UI层 | 通过 `QImage` 加载 |

**位图导入入口**:
- 菜单: `File > Import > Import Image` → `FileMenuAdapter::sigImportImage`
- 工具栏: `Bitmap` 按钮 → `BitmapInputTool`

**位图渲染机制**:
1. 使用 `QImage` 读取图片文件
2. 转换为 `RGBA8888` 格式
3. 通过 `RenderWidget::setBitmapRGBA()` 上传到 OpenGL 纹理
4. 作为背景层渲染，不参与图形实体运算

---

## 使用示例

### 基本用法

```cpp
#include "FileIO/FileIOManager.h"

// 创建管理器
Fio::FileIOManager ioManager;

// 导入文件
VecSyEntityPtr entities;
auto result = ioManager.importFile("example.dxf", entities);
if (result.success) {
    qDebug() << "导入成功，共" << entities.size() << "个图元";
} else {
    qDebug() << "导入失败:" << result.errorMessage;
}

// 导出文件 (需要先实现对应的 Writer)
auto writeResult = ioManager.exportFile("output.dxf", entities);
```

### 使用信号槽监听进度

```cpp
Fio::FileIOManager* ioManager = new Fio::FileIOManager(this);

connect(ioManager, &Fio::FileIOManager::sigImportStarted, 
        this, [](const QString& path) {
    qDebug() << "开始导入:" << path;
});

connect(ioManager, &Fio::FileIOManager::sigImportFinished,
        this, [](const QString& path, bool success) {
    qDebug() << "导入完成:" << path << "成功:" << success;
});
```

---

## 扩展指南

### 新增文件格式 (3 步)

**步骤 1**: 创建解析器头文件 `Include/FileIO/Parsers/XxxParser.h`

```cpp
#pragma once
#include "FileIO/IFileParser.h"

namespace Fio
{
class XxxParser : public IFileParser
{
public:
    FileFormat format() const override { return FileFormat::XXX; }
    QString formatName() const override { return QStringLiteral("XXX Format"); }
    QStringList supportedExtensions() const override { return {QStringLiteral("xxx")}; }
    ParseResult parse(const QString& filePath, VecSyEntityPtr& outEntities) override;
};
}
```

**步骤 2**: 创建解析器源文件 `Src/Parsers/XxxParser.cpp`

```cpp
#include "FileIO/Parsers/XxxParser.h"

namespace Fio
{
ParseResult XxxParser::parse(const QString& filePath, VecSyEntityPtr& outEntities)
{
    // 实现解析逻辑
    // ...
    return ParseResult::ok();
}
}
```

**步骤 3**: 在 `FileParserFactory.cpp` 中注册

```cpp
// 添加头文件
#include "FileIO/Parsers/XxxParser.h"

// 在 initDefaults() 中添加注册
void FileParserFactory::initDefaults()
{
    // ... 其他注册 ...
    
    registerParser(FileFormat::XXX, []() {
        return std::make_unique<XxxParser>();
    });
    m_extToFormat[QStringLiteral("xxx")] = FileFormat::XXX;
}
```

**步骤 4**: 在 `FileFormat.h` 中添加枚举值

```cpp
enum class FileFormat
{
    // ...
    XXX,  // 新增格式
};
```

### 删除文件格式 (2 步)

1. 删除对应的 `.h` 和 `.cpp` 文件
2. 在 `FileParserFactory.cpp` 中移除 `#include` 和 `registerParser` 行

---

## 删除整个模块

### 步骤 1: 移除 CMake 配置

编辑根目录 `CMakeLists.txt`，删除以下行：

```cmake
add_subdirectory(FileIO)
```

### 步骤 2: 移除 Main 模块的链接

编辑 `Main/CMakeLists.txt`：

1. 从 `target_link_libraries` 中移除 `FileIO`
2. 从 DLL 复制命令中移除 `"$<TARGET_FILE:FileIO>"`

### 步骤 3: 删除源码目录

删除整个 `FileIO/` 目录。

---

## 第三方库

当前实现使用纯代码解析，无需额外第三方库。后续扩展可集成以下库：

| 库名 | 用途 | 安装方式 |
|------|------|----------|
| Open CASCADE (OCCT) | STEP/IGES 解析 | `vcpkg install opencascade` |
| libdxfrw | DXF 读写 | 手动集成到 ThirdParty/ |
| Siemens NX Open | .prt 文件解析 | 需要 NX 许可证 |

第三方库应放置在项目根目录的 `ThirdParty/` 下，与现有 `Clipper2`、`QrCodeGen` 等同级。

---

## 构建说明

### 依赖

- CMake 3.20+
- C++17
- Qt 5.15 (QtCore)
- Engine 模块 (SyEntity 图元类型)

### 编译

FileIO 作为 SanYi 项目的一部分自动编译：

```bash
cmake -B build -S .
cmake --build build
```

输出文件：
- `FileIO.dll` (Windows)
- `libFileIO.so` (Linux)
- `libFileIO.dylib` (macOS)

---

## DLL 导出约定

FileIO 使用两套导出宏，分别用于 **C++ API** 和 **C API**，确保跨 IDE / 跨编译器兼容性。

### 导出宏

| 宏 | 来源 | 用途 | 构建时 (DLL) | 使用时 (exe) | 跨编译器 |
|----|------|------|-------------|-------------|---------|
| `FILEIO_API` | `FileIOAPI.h` | C++ 类/函数导出 | `dllexport` / `visibility("default")` | `dllimport` | **否** |
| `_FILEIO_C_API` | `FileIOExport.h` | C API 导出 | `dllexport` | `dllimport` | **是** (标准 C ABI) |

### C++ API 导出 (`FILEIO_API`)

定义在 `FileIO/FileIOAPI.h` 中，用于 C++ 类的导出/导入：
- **Windows** (MSVC/Clang-CL/MinGW): `__declspec(dllexport)` / `__declspec(dllimport)`
- **Linux/macOS** (GCC/Clang): `__attribute__((visibility("default")))`

**限制**: C++ 名字修饰（name mangling）随编译器而异，因此 C++ API 要求使用者与 DLL 使用**同一编译器**（如 MSVC↔MSVC）。

### C API 导出 (`_FILEIO_C_API`)

定义在 `FileIO/FileIOExport.h` 中，用于与编译器无关的 C 语言函数：
- **Windows**: `__declspec(dllexport)` / `__declspec(dllimport)`
- **Linux/macOS**: `__attribute__((visibility("default")))`
- 整个头文件包裹在 `extern "C"` 块中，保证 C 链接约定

**优势**: 可在不同编译器/语言之间使用（MSVC↔MinGW、C#、Python 等）。

### 构建检测

`FILEIO_EXPORTS` 宏由 CMake 在构建 DLL 时通过 `target_compile_definitions(... PRIVATE FILEIO_EXPORTS)` 自动定义，库的使用者**无需**手动定义。`FileIOExport.h` 也依赖 `FILEIO_EXPORTS` 来切换导出/导入模式。

---

## C API 跨 IDE 使用

`FileIOExport.h` 提供与编译器无关的 C 语言接口，使 DLL 可在不同 IDE / 编译器 / 语言之间使用。

### 支持的场景

| 场景 | 支持 | 说明 |
|------|------|------|
| MSVC ↔ MSVC (VS, CLion, Qt Creator) | ✅ | C++ API / C API 均可 |
| MSVC 构建 → MinGW 使用 | ✅ | 仅 C API |
| MinGW 构建 → MSVC 使用 | ✅ | 仅 C API |
| C# P/Invoke | ✅ | C API |
| Python ctypes | ✅ | C API |
| Rust FFI | ✅ | C API |

### C API 快速入门

```c
#include "FileIO/FileIOExport.h"
#include <stdio.h>

int main()
{
    FioManager* mgr = fio_manager_create();
    if (!mgr) { fprintf(stderr, "Failed to create manager\n"); return 1; }

    // 检测文件格式
    FioFileFormat fmt = fio_manager_detect_format(mgr, "drawing.dxf");
    printf("Format: %s\n", fio_format_string(fmt));

    // 导入文件
    FioResult res = fio_manager_import(mgr, "drawing.dxf");
    if (res.success)
    {
        printf("Import OK, %d entities\n", fio_entity_count(mgr));
        // 导出为 SVG
        res = fio_manager_export(mgr, "output.svg", FIO_FORMAT_SVG);
        printf("Export: %s\n", res.success ? "OK" : res.error_message);
    }
    else
    {
        printf("Import failed: %s\n", res.error_message);
    }

    // 一步转换
    res = fio_manager_convert(mgr, "input.dxf", "output.svg", FIO_FORMAT_SVG);

    fio_manager_destroy(mgr);
    return 0;
}
```

### C API 参考

| 函数 | 说明 |
|------|------|
| `fio_manager_create()` | 创建管理器句柄 |
| `fio_manager_destroy()` | 销毁句柄 |
| `fio_manager_import()` | 导入文件 |
| `fio_manager_export()` | 导出文件 |
| `fio_manager_convert()` | 一步转换格式 |
| `fio_manager_detect_format()` | 检测文件格式 |
| `fio_manager_can_import()` | 检查导入支持 |
| `fio_manager_can_export()` | 检查导出支持 |
| `fio_manager_supported_import_extensions()` | 支持的导入扩展名 |
| `fio_manager_supported_export_extensions()` | 支持的导出扩展名 |
| `fio_manager_set_import_callback()` | 设置导入进度回调 |
| `fio_manager_set_export_callback()` | 设置导出进度回调 |
| `fio_format_string()` | 格式枚举 → 可读名称 |
| `fio_format_extension()` | 格式枚举 → 扩展名 |
| `fio_format_from_extension()` | 扩展名 → 格式枚举 |
| `fio_version()` | 获取 DLL 版本号 |

### 显式链接 (LoadLibrary)

```c
#include <windows.h>

typedef FioManager* (*CreateFn)();
typedef FioResult (*ImportFn)(FioManager*, const char*);

HMODULE dll = LoadLibraryA("FileIO.dll");
CreateFn create = (CreateFn)GetProcAddress(dll, "fio_manager_create");
ImportFn import = (ImportFn)GetProcAddress(dll, "fio_manager_import");

FioManager* mgr = create();
import(mgr, "file.dxf");
FreeLibrary(dll);
```

---

## 注意事项

1. **内存管理**: 解析器创建的 `SyEntity` 对象使用裸指针返回，调用方负责管理生命周期
2. **线程安全**: `FileIOManager` 不是线程安全的，多线程环境需自行加锁
3. **错误处理**: 所有操作返回 `ParseResult` 或 `WriteResult`，包含详细错误信息
4. **扩展性**: 新增格式无需修改现有代码，只需实现接口并注册

---

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0.0 | 2026-06-06 | 初始版本，支持 DXF、PLT 导入 |

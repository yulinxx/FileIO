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
    │   └── Writers/                  # 各格式写出器头文件
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
| **模板方法模式** | `PdfBasedParser` | PDF/AI 族解析器共用转换管道，子类仅覆写钩子方法 |

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
│ Dxf   ││ Plt   ││ Svg   ││ Pdf      │  │ DxfWriter│  ← 策略模式
│Parser ││Parser ││Parser ││Parser    │  │ SvgWriter│   (Strategy)
└───────┘└───────┘└───────┘└──────────┘  └──────────┘
```

### 两层架构说明

FileIO 模块分为两层：

| 层级 | 位置 | 职责 |
|------|------|------|
| **FileIO 核心层** | `FileIO/FileIO/` | 纯 C++ 解析/写出逻辑，不依赖 Qt Widgets、不依赖 Main 模块 |
| **Main 桥接层** | `Main/Src/Import/Readers/`、`Main/Src/Export/Writers/` | 实现 `IImportReader` / `IExportWriter` 接口，桥接 FileIO 核心层与 Main 的导入导出框架 |

**注意**: 新增格式时需要在两层都做相应实现：FileIO 层添加 Parser/Writer，Main 层添加 ImportReader/ExportWriter 并注册。

---

## 支持的文件格式

### 2D 矢量格式

| 格式 | 扩展名 | FileIO层导入 | FileIO层导出 | Main层导入 | Main层导出 | 依赖外部工具 | 说明 |
|------|--------|:------------:|:------------:|:----------:|:----------:|:------------:|------|
| **DXF** | .dxf | ✅ | ✅ | ✅ | ✅ | - | AutoCAD DXF 格式，支持 ASCII 和 Binary |
| **PLT/HPGL** | .plt, .hpgl | ✅ | ✅ | ✅ | ❌ | - | HPGL 绘图仪控制语言 |
| **SVG** | .svg, .svgz | ✅ | ✅ | ✅ | ✅ | - | SVG 矢量图形格式 |
| **PDF** | .pdf | ✅ | ❌ | ✅ | ✅ | pdftocairo | PDF 文档格式（导入需外部工具） |
| **AI** | .ai | ✅ | ❌ | ❌ | ❌ | pdftocairo + Ghostscript | Adobe Illustrator 格式（PDF 基 + PS 基） |
| **UG/NX** | .prt, .igs, .iges | ⏳ | ✅ | ❌ | ❌ | - | Siemens NX 格式（导出仅 IGS） |
| **STEP** | .stp, .step | ⏳ | ❌ | ✅ | ✅ | - | ISO-10303 STEP 格式 |
| **Native 2D** | .sy | ✅ | ✅ | ❌ | ❌ | - | SanYi 原生 2D 格式 |
| **Native 3D** | .syx | ✅ | ✅ | ❌ | ❌ | - | SanYi 原生 3D 格式 |

### 位图格式

| 格式 | 扩展名 | 导入 | 导出 | 说明 |
|------|--------|:----:|:----:|------|
| **BMP** | .bmp | ❌ | ✅ | 位图格式（导出走 Main 层 BmpExportWriter） |
| **PNG** | .png | ❌ | ✅ | PNG 无损压缩格式（导出走 Main 层 PngExportWriter） |
| **OBJ (3D)** | .obj | ✅ | ✅ | Wavefront OBJ 3D 模型格式（走 Main 层） |

### 位图导入说明

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

---

## 外部工具依赖

部分格式的导入/导出需要依赖外部命令行工具。这些工具不是程序内置的，但项目提供了**自动部署机制**。

### 推荐方式：ThirdParty 自动部署（零配置）

项目已集成 **CMake 自动部署机制**，只需将外部工具的二进制文件放入 `ThirdParty/` 对应目录，编译时会自动复制到输出目录，程序启动即可使用，无需任何人工配置。

**目录结构**（跨平台统一规范）：
```
ThirdParty/
├── poppler/                          # PDF → SVG 转换工具
│   ├── windows/
│   │   └── x64/
│   │       └── bin/                  # Windows: exe + dll 都放 bin/
│   ├── linux/
│   │   ├── x86_64/
│   │   │   ├── bin/                  # 可执行文件 (pdftocairo 等)
│   │   │   └── lib/                  # 依赖库 (.so)
│   │   └── arm64/
│   │       ├── bin/
│   │       └── lib/
│   └── macos/
│       ├── arm64/                    # Apple Silicon
│       │   ├── bin/
│       │   └── lib/
│       └── x86_64/                   # Intel
│           ├── bin/
│           └── lib/
├── ghostscript/                      # PS → PDF 转换（旧版 AI）
│   ├── windows/x64/bin/
│   ├── linux/{x86_64,arm64}/{bin,lib}/
│   └── macos/{arm64,x86_64}/{bin,lib}/
├── download_tools.ps1                # Windows 自动下载脚本
└── download_tools.sh                 # Linux/macOS 自动下载脚本
```

**一键下载脚本**：

```powershell
# Windows
cd ThirdParty
.\download_tools.ps1 -Tool all
```

```bash
# Linux / macOS
cd ThirdParty
chmod +x download_tools.sh
./download_tools.sh all
```

**工作原理**：

CMake 构建时自动检测并部署：
- **Windows**: `ThirdParty/<tool>/windows/<arch>/bin/` → 输出目录（exe + dll 同级）
- **Linux/macOS**: `ThirdParty/<tool>/<platform>/<arch>/` 下的 `bin/` + `lib/` → 输出目录的 `tools/<tool>/bin/` + `tools/<tool>/lib/`

代码层运行时查找优先级：
1. `SANYI_TOOLS_DIR` 环境变量（用户显式指定，最高优先级）
2. 应用程序同级目录（Windows 便携部署）
3. 结构化 `tools/<tool>/bin/` 目录（Linux/macOS 便携部署）
4. 应用程序同级 `tools/` 扁平目录（兼容旧版）
5. 系统 `PATH` 环境变量
6. 常见安装路径兜底查找

Linux/macOS 调用时会自动设置：
- `LD_LIBRARY_PATH`（Linux）
- `DYLD_LIBRARY_PATH`（macOS）

指向对应的 `tools/<tool>/lib/` 目录，确保依赖库正确加载。

**优点**：
- ✅ **零配置**：放入文件即可，不需要设置环境变量
- ✅ **跨平台**：Windows / Linux / macOS / x86_64 / arm64 全覆盖
- ✅ **可移植**：打包发布时工具随程序一起分发
- ✅ **不污染**：不修改系统 PATH，不影响其他程序
- ✅ **降级友好**：系统已安装工具时自动使用系统版本

---

### pdftocairo (Poppler)

| 项目 | 说明 |
|------|------|
| **用途** | PDF → SVG 转换（PDF/AI 导入的前置步骤） |
| **来源** | Poppler 工具集 |
| **影响格式** | PDF 导入、AI 导入 |
| **缺少时表现** | 导入 PDF/AI 时弹出错误提示，告知需要安装 pdftocairo |

**Windows 下载地址**:
- https://github.com/oschwartz10612/poppler-windows/releases

**查找顺序**（优先级从高到低）：
1. `SANYI_TOOLS_DIR` 环境变量（用户显式指定）
2. 应用程序同级目录（CMake 自动部署目标位置，推荐）
3. 应用程序同级 `tools/` 子目录
4. PATH 环境变量
5. 常见安装路径：
   - `C:/Program Files/poppler/bin/pdftocairo.exe`
   - `C:/Program Files (x86)/poppler/bin/pdftocairo.exe`
   - `C:/poppler/bin/pdftocairo.exe`
   - `C:/tools/poppler/bin/pdftocairo.exe`

**ThirdParty 部署（推荐）**:
1. 下载 poppler-windows 的 Release 压缩包
2. 解压后找到 `Library/bin/` 目录
3. 将该目录下的**所有文件**（`.dll` 和 `.exe`）复制到 `ThirdParty/poppler/windows/x64/bin/`
4. 重新编译项目 → 自动复制到输出目录

**Linux 安装**:
```bash
sudo apt install poppler-utils
```

**macOS 安装**:
```bash
brew install poppler
```

---

### Ghostscript

| 项目 | 说明 |
|------|------|
| **用途** | PostScript → PDF 转换（旧版 AI 文件导入） |
| **来源** | Ghostscript / GhostPDL |
| **影响格式** | AI 导入（仅 PostScript 基的旧版 AI 文件） |
| **缺少时表现** | 导入 PS 格式 AI 文件时失败；PDF 格式 AI 文件不受影响 |

> **注意**: 新版 AI 文件（AI 8+）基于 PDF 格式，只需 pdftocairo 即可导入，不需要 Ghostscript。

**Windows 下载地址**:
- https://github.com/ArtifexSoftware/ghostpdl-downloads/releases

**查找顺序**（优先级从高到低）：
1. `SANYI_TOOLS_DIR` 环境变量（用户显式指定）
2. 应用程序同级目录（CMake 自动部署目标位置，推荐）
3. 应用程序同级 `tools/` 子目录
4. PATH 环境变量
5. `C:/Program Files/gs/` 下的各版本 `bin/` 目录

**需要的文件**:
- `gswin64c.exe`（64位控制台版本）
- `gsdll64.dll`（64位核心 dll）

**ThirdParty 部署（推荐）**:
1. 下载 Ghostscript 安装包或便携版
2. 将 `gswin64c.exe` 和 `gsdll64.dll` 复制到 `ThirdParty/ghostscript/windows/x64/bin/`
3. 重新编译项目 → 自动复制到输出目录

**Linux 安装**:
```bash
sudo apt install ghostscript
```

**macOS 安装**:
```bash
brew install ghostscript
```

---

### 外部工具检查与提示

代码中通过 `PdfToSvgConverter::isPdftocairoAvailable()` 和 `PdfToSvgConverter::isGhostscriptAvailable()` 进行可用性检查。

如果工具不可用，`PdfBasedParser` 会返回包含安装提示的错误信息，用户可根据提示进行安装。

相关文件：
- [PdfToSvgConverter.h](FileIO/Include/FileIO/Parsers/PdfToSvgConverter.h)
- [PdfToSvgConverter.cpp](FileIO/Src/Parsers/PdfToSvgConverter.cpp)
- [PdfBasedParser.h](FileIO/Include/FileIO/Parsers/PdfBasedParser.h)

---

## 文件导入流程

### 整体导入流程

```
用户操作 (菜单/工具栏)
        ↓
   FileOperationRegistry 触发导入操作
        ↓
   ImportService.importFile()
        ↓
   ImportDispatcher.dispatch()
        ↓
   对应格式的 ImportReader.read()
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
   │    - 成功: 返回图元列表                │
   │    - 失败: 返回错误信息                │
   └─────────────────────────────────────────┘
        ↓
   SceneEditService.addEntities() (带 Undo)
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
5. 读取 ENTITIES 段 - 解析图形图元
   - LINE, POLYLINE, CIRCLE, ARC, TEXT 等
6. 读取 OBJECTS 段 - 处理扩展数据
```

**技术实现细节**:
- **坐标系统**: DXF 使用右手坐标系，Y轴向上；内部转换为系统坐标系
- **图层处理**: 自动创建图层，保持原始颜色和线型
- **颜色转换**: DXF 颜色索引 → RGB 值
- **块引用**: 支持 INSERT 图元，递归展开块定义

### PLT/HPGL 格式导入流程

**文件结构**: HPGL (Hewlett-Packard Graphics Language) 是绘图仪控制语言。

**解析流程**:
```
1. 逐字符读取 HPGL 命令流
2. 解析命令参数 (如 PA, PD, PU, CI 等)
3. PltHpglInterpreter 维护画笔状态机
4. 转换命令为图形图元：
   - PA (Plot Absolute): 移动画笔
   - PD (Pen Down): 开始绘制
   - PU (Pen Up): 停止绘制
   - CI (Circle): 绘制圆
   - AR (Arc): 绘制圆弧
   - LT (Line Type): 设置线型
   - SP (Select Pen): 选择画笔
```

**技术实现细节**:
- **单位转换**: PLT 使用绘图单位，需转换为系统单位 (mm)
- **画笔映射**: PLT 画笔号 → 颜色/线型映射
- **圆弧逼近**: HPGL AR 命令使用中心+角度，转换为圆弧图元
- **多笔处理**: 支持多画笔切换，映射到不同图层

**相关文件**:
- [PltParser.h](FileIO/Include/FileIO/Parsers/PltParser.h) / [PltParser.cpp](FileIO/Src/Parsers/PltParser.cpp)
- [PltHpglInterpreter.h](FileIO/Include/FileIO/Parsers/PltHpglInterpreter.h)

### SVG 格式导入流程

**文件结构**: SVG (Scalable Vector Graphics) 是基于 XML 的矢量图形格式。

**解析流程**:
```
1. 使用 XML 解析器读取 SVG 文件
2. 解析 <defs> 段 - 定义、渐变、图案、标记
3. 解析 <g> 分组 - 维护变换矩阵栈
4. 解析图形元素:
   - <line>, <polyline>, <polygon>
   - <circle>, <ellipse>
   - <rect>
   - <path> (贝塞尔曲线)
   - <text>
5. 应用样式 (fill, stroke, stroke-width 等)
6. 应用坐标变换 (translate, rotate, scale 等)
```

### PDF 格式导入流程

**特殊说明**: PDF 导入采用"外部工具转换 + SVG 解析"的方案，不直接解析 PDF 内容。

**解析流程**（模板方法模式，见 `PdfBasedParser`）:
```
1. 验证源文件格式 (检查 %PDF 魔数)
2. 检查 pdftocairo 外部工具是否可用
3. 调用 pdftocairo 将指定页转换为临时 SVG 文件
4. 委托给 SvgParser 解析生成的 SVG
5. 返回解析结果（图元列表）
```

**技术实现细节**:
- **外部工具**: 依赖 `pdftocairo`（Poppler 工具集）
- **临时文件**: 转换后的 SVG 存放在系统临时目录，使用 hash 命名
- **缓存机制**: 同一文件同一页会缓存，避免重复转换
- **页数**: 默认只导入第 1 页

**相关文件**:
- [PdfParser.h](FileIO/Include/FileIO/Parsers/PdfParser.h) / [PdfParser.cpp](FileIO/Src/Parsers/PdfParser.cpp)
- [PdfBasedParser.h](FileIO/Include/FileIO/Parsers/PdfBasedParser.h)
- [PdfToSvgConverter.h](FileIO/Include/FileIO/Parsers/PdfToSvgConverter.h) / [PdfToSvgConverter.cpp](FileIO/Src/Parsers/PdfToSvgConverter.cpp)

### AI 格式导入流程

**文件结构**: AI (Adobe Illustrator) 文件有两种格式：
- **新版 (AI 8+)**: 基于 PDF 格式，文件头为 `%PDF`
- **旧版 (AI 7-)**: 基于 PostScript 格式，文件头为 `%!PS`

**解析流程**:
```
1. 检测文件格式 (PDF 基 or PS 基)
2. PS 基 → 调用 Ghostscript 转为临时 PDF
3. PDF → 调用 pdftocairo 转为临时 SVG
4. 委托给 SvgParser 解析 SVG
5. 返回解析结果
```

**依赖工具**:
- PDF 基 AI: 仅需 pdftocairo
- PS 基 AI: 需要 pdftocairo + Ghostscript

### STEP 格式导入流程

**文件结构**: STEP (ISO-10303) 是工业标准的产品模型数据交换格式，通常用于 3D CAD 数据交换。

**当前状态**: Main 层已注册 `StepImportReader` 和 `StepExportWriter`，FileIO 层有 `StepParser` 定义，具体实现程度需视实际代码而定。

---

## 文件导出流程

### 整体导出流程

```
用户操作 (菜单/工具栏)
        ↓
   FileOperationRegistry 触发导出操作
        ↓
   ExportService.exportFile()
        ↓
   ExportDispatcher.dispatch()
        ↓
   对应格式的 ExportWriter.write()
        ↓
   FileIOManager.exportFile()
        ↓
   FileWriterFactory.createWriter()
        ↓
   Writer.write() 写出文件
```

### 已支持的导出格式

| 格式 | FileIO层 | Main层 | 说明 |
|------|:--------:|:------:|------|
| DXF | ✅ | ✅ | AutoCAD DXF 格式 |
| SVG | ✅ | ✅ | SVG 矢量图形 |
| PLT | ✅ | ❌ | HPGL 绘图语言（FileIO层有PltWriter，Main层未桥接） |
| PDF | ❌ | ✅ | PDF 格式（Main层直接渲染导出） |
| BMP | ❌ | ✅ | 位图（Main层直接渲染导出） |
| PNG | ❌ | ✅ | PNG 图片（Main层直接渲染导出） |
| UG/IGES | ✅ | ❌ | 导出为 IGS 格式 |
| Native 2D (.sy) | ✅ | ❌ | SanYi 原生 2D 格式 |
| Native 3D (.syx) | ✅ | ❌ | SanYi 原生 3D 格式 |
| OBJ | ❌ | ✅ | Wavefront OBJ 3D 模型 |
| STEP | ❌ | ✅ | STEP 3D 模型格式 |

---

## 使用示例

### 基本用法

```cpp
#include "FileIO/FileIOManager.h"

// 创建管理器（构造时自动初始化工厂默认注册）
Fio::FileIOManager ioManager;

// 导入文件
Fio::VecSyEntityPtr entities;
Fio::ParseResult result = ioManager.importFile("example.dxf", entities);
if (result.success) {
    qDebug() << "导入成功，共" << entities.size() << "个图元";
} else {
    qDebug() << "导入失败:" << QString::fromStdString(result.errorMessage);
}

// 导出文件
Fio::WriteResult writeResult = ioManager.exportFile("output.svg", entities);
if (writeResult.success) {
    qDebug() << "导出成功";
}
```

### 指定格式导入

```cpp
// 显式指定格式，跳过扩展名检测
Fio::ParseResult result = ioManager.importFile(
    "example.plt",
    Fio::FileFormat::PLT,
    entities);
```

### 检查是否可导入

```cpp
if (ioManager.canImport("file.pdf")) {
    qDebug() << "支持导入该格式";
} else {
    qDebug() << "不支持或缺少外部工具";
}
```

---

## 扩展指南

### 新增文件格式 (FileIO 层)

**步骤 1**: 在 `FileFormat.h` 中添加枚举值

```cpp
enum class FileFormat
{
    // ...
    XXX,  // 新增格式
};
```

**步骤 2**: 创建解析器头文件 `Include/FileIO/Parsers/XxxParser.h`

```cpp
#pragma once
#include "FileIO/IFileParser.h"

namespace Fio
{
class XxxParser : public IFileParser
{
public:
    FileFormat format() const override { return FileFormat::XXX; }
    std::string formatName() const override { return "XXX Format"; }
    std::vector<std::string> supportedExtensions() const override { return { "xxx" }; }
    ParseResult parse(const std::string& filePath, VecSyEntityPtr& outEntities) override;
};
}
```

**步骤 3**: 创建解析器源文件 `Src/Parsers/XxxParser.cpp`

```cpp
#include "FileIO/Parsers/XxxParser.h"

namespace Fio
{
ParseResult XxxParser::parse(const std::string& filePath, VecSyEntityPtr& outEntities)
{
    // 实现解析逻辑
    // ...
    return ParseResult::ok();
}
}
```

**步骤 4**: 在 `FileParserFactory.cpp` 中注册

```cpp
#include "FileIO/Parsers/XxxParser.h"

void FileParserFactory::initDefaults()
{
    // ... 其他注册 ...
    
    registerWithExtensions(FileFormat::XXX, []() {
        return std::make_unique<XxxParser>();
    }, { "xxx" });
}
```

**步骤 5**: （可选）如果需要导出，按同样方式添加 Writer 并在 `FileWriterFactory.cpp` 注册。

### 新增文件格式 (Main 桥接层)

**步骤 1**: 创建 ImportReader

在 `Main/Src/Import/Readers/` 下创建 `XxxImportReader.h` 和 `XxxImportReader.cpp`，参考 `DxfImportReader` 实现。

**步骤 2**: 在 `ApplicationCompositionRoot.cpp` 中注册

```cpp
#include "Import/Readers/XxxImportReader.h"

// 在 initDefaults() 中添加
m_importDispatcher->registerReader(std::make_unique<XxxImportReader>());
```

**步骤 3**: （可选）如果需要导出，按同样方式添加 ExportWriter 并注册。

---

## 第三方库

### 当前依赖

| 库名 | 用途 | 说明 |
|------|------|------|
| Qt Core | 字符串、文件IO、信号槽 | 项目全局依赖 |
| Clipper2 | 多边形布尔运算 | 位于 ThirdParty/ 目录 |

### 可选集成（未来扩展）

| 库名 | 用途 | 安装方式 |
|------|------|----------|
| Open CASCADE (OCCT) | STEP/IGES 3D 解析 | `vcpkg install opencascade` |
| libdxfrw | DXF 读写替代方案 | 手动集成到 ThirdParty/ |
| Siemens NX Open | .prt 文件解析 | 需要 NX 许可证 |
| PDFium / Poppler | 原生 PDF 解析 | 替代外部工具方案 |

第三方库应放置在项目根目录的 `ThirdParty/` 下，与现有 `Clipper2`、`QrCodeGen` 等同级。

---

## 构建说明

### 依赖

- CMake 3.20+
- C++17
- Qt 5.15 / Qt 6 (QtCore)
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
|------|:----:|------|
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

1. **内存管理**: 解析器创建的 `SyEntity` 对象使用 `unique_ptr` 管理，通过 `VecSyEntityPtr` 传递所有权
2. **线程安全**: `FileIOManager` 不是线程安全的，多线程环境需自行加锁
3. **错误处理**: 所有操作返回 `ParseResult` 或 `WriteResult`，包含详细错误信息和警告列表
4. **扩展性**: 新增格式无需修改现有代码，只需实现接口并注册
5. **外部工具**: PDF/AI 导入依赖 `pdftocairo`，旧版 AI 还需 `Ghostscript`，需确保工具可用
6. **临时文件**: PDF→SVG 转换产生的临时文件存放在系统临时目录，使用 hash 缓存避免重复转换

---

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.1.0 | 2026-07-24 | 新增 SVG、PDF、AI、STEP、Native 解析器；新增 PLT/SVG/DXF/UG/Native 写出器；PDF 导入依赖 pdftocairo 外部工具；新增模板方法模式的 PdfBasedParser |
| 1.0.0 | 2026-06-06 | 初始版本，支持 DXF、PLT 导入 |

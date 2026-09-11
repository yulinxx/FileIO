# FileIO —— 文件导入 / 导出 / 存盘模块

`FileIO` 是一个可独立编译的动态库（Windows 下为 `FileIO.dll`），负责把外部文件格式解析成
**中立 IR**（`Fio::FioParseResult`，纯 POD），以及把文档写出为外部格式。

模块的核心约束只有一条：**导入侧不向外暴露任何 Engine 类型**。解析器只产出 POD，
由调用方（`Eg::FioEntityConverter`，位于 `Engine/3D/Src/Import/`）把 IR 翻译成领域对象。
因此本库可以脱离主程序单独构建、单独跑测试，也可以被其它软件复用——接入方只需要
能读懂 `Include/FileIO/FioTypes.h`。


导出侧仍然直接吃 `Eg::SyEntity`（`Writers/` 里靠 `dynamic_cast` 分派子类），这是已知
技术债，见 [§11 分期计划](#11-分期计划)。

---

## 1. 目录结构

```
FileIO/
├── CMakeLists.txt                # 独立构建入口（project(SanYiFileIO)）
├── README.md                     # 本文件
└── FileIO/                       # 库本体
    ├── CMakeLists.txt            # sanyi_add_shared_library(FileIO)
    ├── Proto/SanYiDocument.proto # 原生格式（.sy/.syx）的 protobuf schema
    ├── Include/FileIO/           # 公开头文件（唯一对外契约）
    │   ├── FioTypes.h            # ★ 中立 IR：EntityType / IrLayerInfo / IrGroupInfo
    │   │                         #   / EntityInfo / BinaryBlob / FioParseResult
    │   ├── FileFormat.h          # FileFormat 枚举
    │   ├── FormatRegistry.h      # 扩展名 ↔ 格式 映射（单例）
    │   ├── IFileParser.h         # 解析器接口（parseToIR）
    │   ├── IFileWriter.h         # 写出器接口
    │   ├── FileParserFactory.h   # 格式 → 解析器 工厂
    │   ├── FileWriterFactory.h   # 格式 → 写出器 工厂
    │   ├── FileIOManager.h       # 门面：存盘 / 打开 / 导入 / 导出
    │   ├── FileImporter.h        # C 风格导入入口（含 ExportBlob/FreeBlob）
    │   ├── ImageUtils.h          # 位图解码 / 像素-物理单位换算
    │   ├── SySerializer.h        # protobuf 序列化核心
    │   ├── SyDocument.h / SyDocument3D.h
    │   ├── Parsers/              # 各格式解析器头
    │   └── Writers/              # 各格式写出器头
    ├── Src/
    │   ├── Internal/             # 内部实现，不对外导出
    │   │   ├── IrProjector.h/.cpp   # ★ IrPublisher（缓冲区所有者）+ IrProjector（投影）
    │   │   ├── IrTransform.h/.cpp   # ★ IrXform 2D 仿射变换 + transformEntity
    │   │   ├── ParsedGeometry.h     # 解析期中间结构 ParseData / ParsedEntity / ParsedGroup
    │   │   ├── FileIOInternal.h     # ILegacyParser / ILegacyWriter / 错误码
    │   │   ├── MetadataFiller.h
    │   │   └── SyDocumentData.h
    │   ├── Parsers/              # 12 个解析实现
    │   ├── Writers/              # 6 个写出实现
    │   └── *.cpp                 # 工厂 / 门面 / 序列化 / 图像工具
    └── Test/                     # GTest 用例（ctest）
```

---

## 2. 架构：三层 + 一个 IR 出口

```
        文件路径
           │
           ▼
   ┌───────────────────┐   扩展名 / 魔数
   │  FormatRegistry   │──────────────► FileFormat
   └───────────────────┘
           │
           ▼
   ┌───────────────────┐
   │ FileParserFactory │──────────────► IFileParser*
   └───────────────────┘
           │  parseToIR(path)
           ▼
   ┌──────────────────────────────────────────────┐
   │ 解析层（DxfParser / SvgParser / ObjParser …） │
   │   格式专属逻辑 → ParseData（解析期结构）      │
   │   或直接写入 IrPublisher                      │
   └──────────────────────────────────────────────┘
           │
           ▼
   ┌──────────────────────────────────────────────┐
   │ IrPublisher（thread_local 缓冲区唯一所有者）  │
   │ IrProjector::project(ParseData → IR)          │
   │   · 图层 / 群组 id 稠密重编号（1-based）      │
   │   · 父引用重映射、缺父降级、成环打断          │
   │   · 几何按类型投影，复杂数据进 extensionBlob   │
   └──────────────────────────────────────────────┘
           │  FioParseResult（纯 POD，只读借用）
           ▼  ── DLL 边界 ──────────────────────────
   FioEntityConverter（Engine3D） → SyEntity / SyMeshEntity
           │
           ▼
   ImportService → SceneManager + LayerManager + GroupManager
```

### 2.1 为什么一定要有 `IrPublisher`

`FioParseResult` 里的 `entities / layers / groups / extensionBlob.data` 都是**裸指针**。
指针必须指向一块在函数返回后依然存活的内存，否则调用方拿到的是悬垂指针。
早期各解析器各自定义 `thread_local std::vector`，结果是：

- 缓冲区所有者散落在多个 .cpp 里，生命周期规则靠注释口头约定；
- 每个解析器都要重写一遍「几何 → extensionBlob 布局」，布局一旦不一致，
  `FioEntityConverter` 读出来就是垃圾数据；
- 无法统一做 id 重编号与越界校验。

现在收敛为单点：

- `IrPublisher::threadLocal()` —— 每线程一份缓冲区，`reset()` 清空，`publish()` 产出
  `FioParseResult`。所有解析器只能通过它发布数据。
- `IrProjector::project(ParseData&, sourceFormat)` —— 走 `ParseData` 的解析器（DXF/SVG/
  PLT/OBJ/…）统一在这里投影；`StlParser` 等极简格式直接用 `IrPublisher` 追加。

### 2.2 `IrTransform` —— 2D 仿射变换的唯一实现

`IrXform` 是 `[a b e; c d f]` 形式的 2×3 仿射矩阵，`transformEntity()` 把一个
`EntityInfo`（含它的 extensionBlob 数据）变换后重新发布到 `IrPublisher`。

复用点：DXF `INSERT` 展开（含阵列与嵌套）、镜像、未来的 SVG `transform=` 与
`$INSUNITS` 单位缩放。规则：

- 相似变换（等比缩放 + 旋转 + 平移）下圆保持为圆、圆弧保持为圆弧；
  `det() < 0`（含镜像）时圆弧起止角互换，保证方向正确。
- 非等比缩放下圆退化为椭圆（`t = atan2(sgnY·sin a, sgnX·cos a)` 修正参数角）。
- 含剪切（`hasShear()`）时对圆/圆弧告警并按椭圆近似处理。

---

## 3. 跨 DLL 内存契约（必读）

`FioTypes.h` 里的所有类型都是 POD：无 `std::string` / `std::vector` / 虚函数，
字符串用固定长度 `char[]`，可变长数据用「偏移 + 长度」指向 `extensionBlob`。
POD **不加** `FILEIO_API`（避免把成员布局纳入 ABI 约束、避免 GCC/Clang 的
`attribute ignored` 警告），导出宏只加在类和自由函数上。

### 3.1 借用 vs 拥有

| 返回物 | 所有权 | 调用方义务 |
|---|---|---|
| `FioParseResult::entities / layers / groups` | 借用（FileIO 内部 thread_local） | 只读；**绝不可** `delete` |
| `FioParseResult::extensionBlob` | 借用（同上） | 只读；越界必须自校验 |
| `FileImporter::ExportBlob()` | 拥有 | 必须调 `FileImporter::FreeBlob()` 归还 |

新增任何返回 `BinaryBlob` 的接口，必须在该接口注释里写明属于哪一种。

### 3.2 生命周期

`FioParseResult` 的有效期 = **同一线程下一次调用任意解析入口之前**。

- 立即消费：拿到 IR 后马上转成领域对象（`Eg::FioEntityConverter::convertAll`）。
- 不可跨线程保存：要跨线程传的是转换后的对象，不是 IR。
- 不可缓存：第二次解析会复用同一块缓冲区，旧指针全部失效
  （`IrProjectorTests` 里有一条专门测这个失效行为）。

### 3.3 调用方必须做的校验

`extensionBlob` 是跨边界数据，畸形文件可以给出任意 offset/size。所有读取点都要先查

```cpp
extensionDataOffset + extensionDataSize <= extensionBlob.size
```

`FioEntityConverter::extensionRangeValid()` 是该校验的落地实现，Nurbs / Image /
Mesh3D / 折线顶点四条分支都经过它。

### 3.4 new/delete 同堆

工厂返回的 `IFileParser*` / `IFileWriter*` 在 FileIO.dll 内 `new`，必须交回
`FileParserFactory::destroyParser()` / `FileWriterFactory::destroyWriter()` 释放，
不可在调用方 `delete`（虚析构也定义在 .cpp、不在头文件内联），
避免 MD/MDd 运行库不一致时跨堆释放。


---

## 4. 格式支持矩阵

### 4.1 导入（`FileParserFactory::initDefaults`）

| 格式 | 扩展名 | 解析器 | 第三方依赖 | 备注 |
|---|---|---|---|---|
| DXF | `.dxf` | `DxfParser` | libdxfrw（仓内源码） | 覆盖面最广，见 §5.1 |
| PLT / HPGL | `.plt` `.hpgl` | `PltParser` + `PltHpglInterpreter` | 无 | 40 plu/mm，见 §5.2 |
| SVG / SVGZ | `.svg` `.svgz` | `SvgParser` | NanoSVG，可选 zlib | 见 §5.3 |
| PDF | `.pdf` | `PdfParser` : `PdfBasedParser` | 外部进程 `pdftocairo` | 转 SVG 后复用 `SvgParser` |
| AI | `.ai` | `AiParser` : `PdfBasedParser` | `pdftocairo` + Ghostscript | PDF 基 / PS 基两条路 |
| IGES | `.igs` `.iges` | `UgParser` | 无（自写文本解析） | 图元码 100/106/110/116 |
| STEP | `.stp` `.step` | `StepParser` | GeoModelCore（可选，底层 OCC） | 未启用时返回失败 |
| OBJ | `.obj` | `ObjParser` | 无 | 见 §5.4 |
| STL | `.stl` | `StlParser` | 无 | ASCII / 二进制自动识别 |
| 原生 2D | `.sy` | `NativeParser(Native)` | protobuf-lite | |
| 原生 3D | `.syx` | `NativeParser(Native3D)` | protobuf-lite | |

`FormatRegistry` 另外注册了 `BMP` / `PNG`（供扩展名识别与文件对话框过滤），
但工厂里**没有**对应 parser：位图解码走导出函数 `Fio::loadImageToRgba()`
（stb_image / libwebp / libtiff），由上层插入 `EntityType::Image` 图元时调用。

已知不一致：`FormatRegistry` 把 `.prt` 归到 `UG`，而 `UgParser::forEachSupportedExtension`
只报 `igs` / `iges`，所以 `.prt` 能被识别成 UG 但解析必然失败。

### 4.2 导出（`FileWriterFactory::initDefaults`）

DXF（手写文本，不经 libdxfrw）、SVG、PLT、IGES(`.igs`)、`.sy`、`.syx`。
圆/圆弧/椭圆在写出时按固定段数离散（一般 20 段，椭圆 40 段）。

---

## 5. 各格式处理细节

2D 与 3D 走的是**同一条链路**：`FormatRegistry → FileParserFactory → parseToIR →
IrPublisher/IrProjector → FioParseResult`。差别只在解析器内部产出什么几何：
2D 产出线/弧/椭圆/折线/NURBS/文本，3D 产出 `EntityType::Mesh3D`。
Main 侧同样统一：所有 reader 都是 `ImportReaderBase::readViaIR(context, format, out, collectLayers)`。

### 5.1 DXF

依赖 libdxfrw（源码在 `ThirdParty/libdxfrw`，随 FileIO 目标一起编译，无需外部安装）。
`DxfParser` 内有两个 `DRW_Interface` 实现：一个负责收集（含 BLOCK 定义），
一个负责发布；这样才能在 `endBlock` 之后再展开 `INSERT`。

**图元覆盖**

- 直接落地：`LINE` `POINT` `CIRCLE` `ARC` `ELLIPSE` `LWPOLYLINE` `POLYLINE`
  `SPLINE` `TEXT` `MTEXT` `SOLID` `TRACE` `3DFACE` `INSERT`（块展开）
- 告警后丢弃（不静默）：`HATCH` `IMAGE` `VIEWPORT` `LEADER` `RAY` `XLINE`
  以及全部 `DIMENSION` 变体（Aligned / Linear / Radial / Diametric / Angular /
  Angular3P / Ordinate）

**块引用（BLOCK / INSERT）**

按 AutoCAD 语义展开：先减去块基点 → 缩放 → 旋转 → 平移；
阵列（`colCount`/`rowCount`）的行列间距沿**旋转后的轴**计算。
每个 `INSERT` 实例产出一个 `IrGroupInfo`，嵌套块产出嵌套群组。
护栏：`kMaxBlockDepth = 16`、`kMaxExpandedEntities = 2'000'000`、`kMaxArrayCount = 4096`；
引用未定义块时告警并跳过。块定义自身**不**进入模型空间（修好的旧缺陷：
以前块定义里的图元会连同展开结果一起落地，导致重影）。

**颜色**

真彩色 > ACI 索引 > `BYLAYER(256)` > `BYBLOCK(0)`。图层 `"0"` 参与继承。
图元色写入 `EntityInfo::color`（0xAARRGGBB，0 表示未指定，由消费方回退到图层色），
以「覆盖色」形式应用，避免被图层去重逻辑吞掉。

**坐标系与单位**

`extrusion`（210/220/230）非 Z 轴时按 Arbitrary Axis Algorithm 做 OCS→WCS
（阈值 1/64）。`$INSUNITS` 从 `DRW_Header::vars` 读取（`getInt` 是私有的，
只能走公开的 vars map），写入 `FioParseResult::sourceUnit`。

**曲线**

`bulge` 段按 `bulge = tan(θ/4)` 反解圆弧并折线化；`LWPOLYLINE` 标志位 bit 0 = 闭合，
闭合折线投影为 `EntityType::Polygon`。`SPLINE` 优先用控制点构造 NURBS，
只有拟合点的变体降级为折线并告警。`SOLID`/`TRACE`/`3DFACE` 的角点顺序按
1-2-4-3 重排为四边形。

### 5.2 PLT / HPGL

无第三方依赖，`PltHpglInterpreter` 是一个 header-only 状态机，直接产出 `EntityInfo`。
支持 `PU`/`PD`/`PA`/`PR`/`AA`/`AR`/`CI`/`PE` 等常用指令，`kPluPerMm = 40.0`。

**已知缺口**：`SC`（用户缩放）与 `IP`（硬剪裁窗口）目前是占位实现，
不真正参与坐标变换——遇到带 `SC` 的图纸尺寸会偏。列为 P1，见 §11。

### 5.3 SVG / SVGZ

NanoSVG（vcpkg，`find_package(NanoSVG CONFIG REQUIRED)`）负责解析与路径展开。
`.svgz` 需要 zlib：`find_package(ZLIB QUIET)` 找到时定义 `FILEIO_HAS_ZLIB`
并按 16 KiB 块解压；没有 zlib 时 `.svg` 不受影响，`.svgz` 直接失败。

NanoSVG 会把所有路径统一成三次贝塞尔序列，因此当前导入结果里没有原生圆/圆弧
——圆弧回拟合列为 P1。`<g>` 应产出 `IrGroupInfo`（IR 通道已就绪），
但 SVG 侧的群组落地尚未接线，同列 P1。

**一条子路径 = 一个图元。** NanoSVG 把每个子路径（`M ... M ...` 之间）拆成一个
`NSVGpath`，一条子路径的所有段聚合成**一个** `EntityType::SmartLine`（复合曲线），
几何写进 `extensionBlob`：

- 布局按 `Fio::kSmartSegStride`(=9) **定长**排布，每段 `[标签][p0][p1][p2][p3]`，
  标签取 `Fio::SmartSegKind`（0=直线 / 1=二次贝塞尔 / 2=三次贝塞尔），以 double 存放；
  段数由 `extensionDataSize` 反算，不信任 `vertexCount`（畸形文件可以给任意值）。
- 只有**一段**时不套复合曲线容器，直接产出 `Line` 或 `Bezier`，少一层间接。
- NanoSVG 把直线也升阶成三次贝塞尔（`nsvg__lineTo` 把控制点放在 1/3、2/3 处），
  解析侧会识别回直线段（共线且控制点落在 `[p0,p1]` 内），渲染时不必再细分——
  多边形/矩形轮廓型 SVG 几乎全部命中。
- 闭合性由几何本身携带：NanoSVG 在 `addPath` 里已补上回到起点的闭合段，
  因此 IR 不需要额外传闭合标记。

为什么这么改：旧实现是「每段贝塞尔一个图元」，一条 10 段的 path 产出 10 个图元
——语义上选不中整条路径，落地阶段还要为每段各走一遍 clone / R-tree insert /
observer 通知。合成样本（1000 path × 10 段）实测：图元数 10000 → 1000，
解析 19.7 ms → 9.3 ms，转换 2.8 ms → 1.7 ms。

**图层按颜色划分，不按 `id`。** SVG 没有图层概念，早期实现拿 `shape->id` 当图层名，
这在 Illustrator 导出的文件上凑巧可用（NanoSVG 会把 `<g id>` 继承给无 id 的子 shape），
但 Figma / SVGO 导出的文件每个元素都带唯一 id，于是退化成「1 个图形 = 1 个图层」：
1000 个 shape 就产出 1000 个图层，直接撞穿 `LayerManager::kMaxLayerCount = 1024`。
现在的规则是：

- 图层键 = 图元颜色（有描边取 `stroke`，纯填充取 `fill` 且需开启 `setImportFillAsOutline`），
  图层名为 `#RRGGBB`，查找走 `unordered_map`（原线性扫描在多色文件上是 O(shape²)）；
- 颜色种类上限 `kMaxSvgLayers = 256`，超出部分并入第一个图层并只告警一次
  （不返回 0：0 是「未分配」哨兵，会让图元的图层归属显得凭空消失）；
- 原始 `shape->id` 降级写入 `EntityInfo::name`，出问题时仍能把画布图元对回 SVG 元素。

颜色是激光加工里唯一有工艺含义的分层维度（不同颜色 = 不同功率/速度），所以这是默认策略。
若将来确实需要按 `<g>` id 分层，做法是给 `SvgParser` 加一个 `setLayerStrategy` 开关，
而不是改回去。



### 5.4 OBJ

无第三方依赖，自写解析。按 `o` / `g` / `usemtl` 分段（chunk），每段产出一个
`EntityType::Mesh3D`：

- `o` 名称相同的分段复用同一个顶层群组；`g` + `usemtl` 产出名为 `"grp/mtl"` 的子群组；
  匿名单段文件不产出群组（避免给单个三角形套一层空壳）。
- 支持负索引（相对当前顶点表末尾）、`v` / `v/vt` / `v//vn` / `v/vt/vn` 四种面顶点写法；
  多边形面按扇形三角化，缺法线时按面法线补齐。
- 护栏：`kMaxTriangles = 20'000'000`、`kMaxFaceVerts = 1024`；索引越界只告警不整体失败；
  没有任何面时解析失败。

### 5.5 STL

ASCII / 二进制自动识别。**识别顺序很关键**：必须先验证
`84 + count * 50 == fileSize` 再判断三角形数上限，否则 ASCII 文本的第 80–84 字节
会被当成一个巨大的 count 而整体拒收——这正是以前「STL 必须绕过 FileIO 走
Engine3D 兜底」的真实原因。文件小于 15 字节直接返回失败；
`MAX_STL_TRIANGLES = 50'000'000`。

### 5.6 Mesh blob 布局（3D 共用）

```
extensionBlob[offset .. offset+size) =
    [vertices: vertCount * 3 * float]  [normals: vertCount * 3 * float]
```

注意是 **float 而非 double**，且按「每三角形每角点」平铺展开
（`FioEntityConverter` 不读索引）。OBJ 与 STL 必须产出完全一致的布局。

### 5.7 PDF / AI

两者都继承 `PdfBasedParser`：调用外部进程把文件转成 SVG，再委托 `SvgParser`。
PDF 用 `pdftocairo`（Poppler）；AI 分两种——PDF 基的 AI 走 `pdftocairo`，
PS 基的老 AI 需要 Ghostscript（`gswin64c`）。外部工具缺失时解析失败并给出提示，
不会崩溃。这是唯一依赖**外部可执行文件**的路径，部署时需随包提供或要求系统安装。

### 5.8 原生格式（.sy / .syx）

protobuf-lite + `Proto/SanYiDocument.proto`，2D/3D 共用 `NativeParser` / `NativeWriter`
（构造时传 `FileFormat::Native` 或 `Native3D`）。
`NativeParser3D` / `NativeWriter3D` 已被取代，未在任何工厂注册。

**原生格式不走中立 IR**：protobuf 文档直接反序列化成 Engine 图元，中间没有 IR 表达，
`NativeParser` 因此没有实现 `parseToIR`，`.sy` / `.syx` 导入走 `importFile` 旧路径
（`ImportReaderBase::readViaLegacy`，会打一条 WARN 说明"无 IR、不还原图层/群组"）。
`NativeImportReader` 已不再先尝试 IR——那只会每次白跑一次并留下误导性的失败日志。
后续计划见 §11 P1「Native 迁移到 `Engine/Persistence`」。
落到 `IFileParser::parseToIR` 默认实现的格式会打一条 ERROR（`[IFileParser] parseToIR not
implemented for format=...`），不再静默返回空结果。

---

## 6. 图层 / 颜色 / 群组 / 位图 的端到端通道

| 通道 | IR 载体 | 消费者 |
|---|---|---|
| 图层 | `FioParseResult::layers` + `EntityInfo::layerSourceId` | `ImportService::restoreImportedLayers`（按名 → 按色 → 新建三级复用） |
| 图元色 | `EntityInfo::color`（0 = 未指定） | 转换层按覆盖色应用，不被图层去重吞掉 |
| 群组 | `FioParseResult::groups` + `EntityInfo::groupSourceId` | `ImportService::restoreImportedGroups` → `Eg::GroupManager` |
| 位图 | `EntityType::Image` + `imageWidth/Height` + extensionBlob | `FioEntityConverter`（越界校验后建图元） |

**群组只单向记录成员关系**：图元记 `groupSourceId`，`IrGroupInfo` 只记 `parentSourceId`，
不存成员列表。理由是与 `layerSourceId` 同构、双向存储不一致时无裁决依据、
少一处越界校验点。IR 侧已保证 id 稠密、缺父降级为顶层并告警、成环打断并告警；
`ImportService` 侧再用 `SyGroup::wouldCreateCycle()` 兜底一次
（群组 id 不复用 IR 的 `sourceId`，因为它与运行时 `EntityId` 空间会撞车）。

**`IrPublisher` 侧的图层/群组闸门**（`addLayer` / `addGroup`，所有走 IR 的解析器共享）：

- `addLayer` 按名字查重（键取截断后的 `IrLayerInfo::name`，与 `findLayer` 同一把键，
  否则超长名字「登记得进、查不出来」，每个图元都会重复登记一次）；
  查找走 `unordered_map`，不再线性扫描（DXF 是「每图元 + 每 INSERT 各查一次」）。
- `kMaxLayers = 1024`，与 Engine 侧 `LayerManager::kMaxLayerCount` 对齐。触顶返回 0
  （未分配 → 落到下游默认图层），只在首次触顶记一条 warning。**图元不因图层超限而丢**。
- `kMaxGroups = 65536`。DXF 的 `INSERT` 阵列是「一次引用 = 一个群组」，
  `cols`/`rows` 各自可达 4096，而空块阵列不推进 `kMaxExpandedEntities`，
  所以群组必须单独设闸。触顶返回 `parentSourceId`：层级压平，但归属仍有效。
- 两个 warned 标志随 `reset()` 复位，否则连续导入时第二个文件撞上限会静默。


### 6.1 告警文本不跨 DLL

`FioParseResult` 只带 `warningCount`（数量），**不带告警文本数组**——文本是变长的，
放进 POD 契约就要引入指针数组和额外的生命周期规则。告警文本留在 FileIO 侧日志里，
数量由 `ImportReaderBase` 转成一条 `ImportResult` 警告
（`"<格式> parser reported N warning(s), see FileIO log for details"`），
供 UI 提示用户去看日志。

### 6.2 一次导入的日志足迹

日志前缀按层划分，排查时从外到内收敛（完整排查手册见
`Docs/04-测试与日志/log-policy.md` §6.3）：

```
[ImportService]      五阶段主流程 + 图元分拣/落地/图层群组还原统计
[ImportDispatcher]   命中哪个读取器、读取器总耗时
[ImportReader:DXF]   IR 解析耗时与统计、转换层丢弃差额、legacy 回退告警
[FileIO]             DLL 入口：parser 缺失 / 异常 / 零图元
[DxfParser] 等       单格式细节；所有 parser 的收尾文案统一为 parseToIR END
```

约定：每个 parser 必须有 `parseToIR START` 与 `parseToIR END` 成对日志，
END 至少给出图元数与耗时；提前 return 的分支一律要写明原因，不允许静默返回空结果。

### 6.3 `IrProjector` 的降级记账（「文件里 1000 个、界面只出现 800 个」怎么查）

上层 `[ImportReader:*]` 打的 `Converter dropped N of M` 里的 **M 是 `entityCount`**，
也就是**已经**扣掉投影层丢掉的量了。所以凡是 `IrProjector` 自己丢弃或降级的图元，
在上层日志里根本不会露头 —— 必须在投影层记账，否则这条链上就断了。

投影层收尾固定输出一条守恒口径的 INFO：

```
[IrProjector] Projected DXF: 1000 parsed -> 980 entities, 12 layers, 3 groups, 40960 blob bytes, 4 warnings
```

`parsed -> entities` 不相等即说明有图元没进 IR。此时紧跟一条 WARN 给出分类计数，
并对最要紧的三类各补一条「首个样本」，拿着 `sourceId` 可以回原文件定位：

```
[IrProjector] Degraded while projecting DXF: unknownType=20 smartLineSkipped=0 blobOverflow=0
              missingLayerRef=3 missingGroupRef=0 nameTruncated=1 groupParentMissing=0 groupCycleBroken=0
[IrProjector] First unknown geometry type: raw=17 (SmartLine), sourceId=41
```

分类含义与后果：

- `unknownType` —— 类型无法映射，**整条图元被丢弃**（`parsed` 与 `entityCount` 之差的主因）
- `smartLineSkipped` / `blobOverflow` —— 图元入表但几何为空，即「空壳图元」；
  保留 id 与图层归属，消费侧读到的是空几何
- `missingLayerRef` / `missingGroupRef` —— 断引用，落到「未分配 / 无群组」哨兵；
  典型现场是「导入后图元全挤在默认图层上」
- `nameTruncated` —— 名字超长被截断；图层名截短后按名字比对会失配，于是同一图层被重复登记
- `groupParentMissing` / `groupCycleBroken` —— 群组树被修复（降为顶层 / 断环）

**为什么是聚合计数而不是逐条日志**：畸形文件里这类问题每个图元都会触发一次，
几万条同样的 WARN 会把日志冲爆，反而看不出还有别的问题。解析是有明确边界的批处理
（不是长期运行的流），所以「分类计数 + 首个样本 sourceId」是更合适的形态。
同理，每个类别只往 `warnings` 里塞一条，`warningCount` 因此是**类别数**而非图元数。


---

## 7. 第三方依赖与跨平台

| 依赖 | 获取方式 | 用途 | 缺失后果 |
|---|---|---|---|
| libdxfrw | 仓内源码 `ThirdParty/libdxfrw`，随 FileIO 编译 | DXF 解析 | 无法构建 |
| protobuf-lite | vcpkg `find_package(protobuf CONFIG REQUIRED)` | `.sy`/`.syx` | 无法构建 |
| NanoSVG | vcpkg `find_package(NanoSVG CONFIG REQUIRED)` | SVG 解析 | 无法构建 |
| stb (stb_image) | vcpkg `find_package(Stb REQUIRED)` | 位图解码 | 无法构建 |
| libwebp | vcpkg `find_package(WebP CONFIG REQUIRED)` | webp 解码 | 无法构建 |
| libtiff | vcpkg `find_package(TIFF REQUIRED)` | tiff 解码 | 无法构建 |
| zlib | `find_package(ZLIB QUIET)` → `FILEIO_HAS_ZLIB` | `.svgz` | 仅 `.svgz` 不可用 |
| GeoModelCore | 仓内可选模块 → `FILEIO_HAS_GEOMODELCORE` | STEP 导入（底层 OCC） | STEP 导入返回失败 |
| pdftocairo (Poppler) | **外部可执行文件** | PDF / PDF 基 AI | 该格式导入失败并提示 |
| Ghostscript | **外部可执行文件** | PS 基 AI | 该格式导入失败并提示 |

跨平台状态：

- 解析/写出代码本身只用标准 C++17 + 上述跨平台库，没有 Win32 API、没有 Qt。
- 路径一律按 **UTF-8 `const char*`** 跨边界传递（`ImageUtils` 的注释里明确写了这一点），
  Windows 上由实现内部转宽字符打开文件。
- `FILEIO_API` 在非 MSVC 下退化为 `__attribute__((visibility("default")))`；
  POD 不加导出宏，正是为了避免 GCC/Clang 的 `attribute ignored` 警告。
- 外部进程依赖（pdftocairo / gswin64c）是唯一的平台差异点：可执行文件名与查找路径
  按平台不同，Linux/macOS 下对应 `pdftocairo` / `gs`。
- 未在 Linux CI 上验证过：当前只在 Windows + MSVC 上实际构建通过。

---

## 8. 构建与测试

### 8.1 独立构建（不需要主程序）

```bash
cmake -S FileIO -B FileIO/build
cmake --build FileIO/build --config Release
ctest --test-dir FileIO/build -C Release
cmake -S FileIO -B FileIO/build -DBUILD_FILEIO_TESTS=OFF   # 只编库
```

`FileIO/CMakeLists.txt` 复用仓库统一配置（`../Config.cmake` +
`../CMake/StandaloneInit.cmake`），并用 `_fileio_add_sibling()` 按依赖顺序拉入兄弟模块：

```
Utility / Log ──> EngineCommon ──> Engine2D ──> Engine3D ──> FileIO
```

`Main` / `UI` / `Renderx` 一概不参与。兄弟模块的测试目标被
`set(BUILD_XXX_TESTS OFF CACHE BOOL "" FORCE)` 强制关闭。
`Engine2D`/`Engine3D` 只被 `Writers/` 用于 `dynamic_cast` 具体图元子类——
导出侧也切到 IR 之后，依赖闭包可以缩到 `Utility + Log`。

GTest 缺失时 `BUILD_FILEIO_TESTS` 会被自动置 OFF（只编库仍然成功）。

### 8.2 测试

`Test/` 下的用例（`gtest_discover_tests` 注册到 ctest）：

- `IrProjectorTests.cpp`（16）—— IR 投影：id 稠密化、缺父/成环、闭合折线、
  NURBS/Mesh/Image 布局、名称截断、断引用与未知类型的降级记账、`reset()` 失效
- `DxfBlockTests.cpp`（9）—— 块定义不泄漏、变换/阵列/嵌套、镜像与非等比缩放
- `DxfEntityTests.cpp`（12）—— bulge 正负号、闭合标志、OCS→WCS、`$INSUNITS`、
  SOLID 角点顺序、拟合点 SPLINE 降级、HATCH 告警
- `Obj3DTests.cpp`（10）—— OBJ 分段/群组/负索引/三角化 + ASCII STL 回归
- `SySerializerTests.cpp`（30）、`FileIOUtilityTests.cpp`（31）、
  `FileIORegressionTests.cpp`（26）、`FioTypesTests.cpp`（18）、`FileImportTests.cpp`（15）

`Test/CMakeLists.txt` 直接把 `../Src/Internal/IrProjector.cpp` 编进测试可执行文件：
`IrPublisher` 是内部类且成员含 STL 容器，给它加 `dllexport` 得不偿失。

---

## 9. 使用示例

```cpp
#include "FileIO/FileParserFactory.h"
#include "FileIO/FormatRegistry.h"

// 1) 识别格式
const Fio::FileFormat fmt = Fio::FormatRegistry::instance().detectFormat(path);

// 2) 取解析器（工厂内 new，必须交回工厂 destroyParser，不可在调用方 delete）
Fio::FileParserFactory& factory = Fio::FileParserFactory::instance();
Fio::IFileParser* parser = factory.createParser(fmt);
if (!parser) { return; }

// 3) 解析出 IR —— 借用内存，立即消费
const Fio::FioParseResult ir = parser->parseToIR(path);
for (uint32_t i = 0; i < ir.entityCount; ++i)
{
    const Fio::EntityInfo& e = ir.entities[i];
    // 读 extensionBlob 前务必先校验范围
    if (static_cast<size_t>(e.extensionDataOffset) + e.extensionDataSize > ir.extensionBlob.size)
    {
        continue;  // 畸形数据
    }
    // ... 转换成自己的领域对象
}

factory.destroyParser(parser);
// 此处之后不要再保存 ir 里的任何指针

```

---

## 10. 扩展指南：新增一种导入格式

1. `FileFormat.h` 加枚举值。
2. `FormatRegistry.cpp` 的构造函数里 `registerFormat(...)` 登记扩展名与对话框标签。
3. `Include/FileIO/Parsers/XxxParser.h`：继承 `IFileParser`，实现
   `format()` / `formatName()` / `forEachSupportedExtension()` / `parseToIR()`。
   **需要被测试直接链接的类要加 `FILEIO_API`**，否则会以 LNK2001/LNK2019 形式暴露。
4. `Src/Parsers/XxxParser.cpp`：解析成 `ParseData` 后 `return IrProjector::project(data, "XXX")`；
   格式极简时也可以直接用 `IrPublisher::threadLocal()` 追加图元再 `publish()`。
   **不要自己定义 thread_local 缓冲区。**
5. `FileParserFactory.cpp` 的 `initDefaults()` 里 `registerParser(...)`。
6. `Test/` 加用例。CMake 用 `GLOB_RECURSE CONFIGURE_DEPENDS`，新文件无需改构建脚本。
7. Main 侧新增 reader 时只写一行：
   `return readViaIR(context, Fio::FileFormat::XXX, outEntities, collectLayers);`

约定：**注释用中文，日志文本用英文**；UTF-8 无 BOM；列宽 120；Allman 大括号。

---

## 11. 分期计划

已完成（P0，框架 + 主链路）：中立 IR 与内存契约、`IrPublisher` 单点发布、
`IrProjector` 投影与 id 规范化、`IrTransform` 仿射层、DXF 块展开与图元覆盖、
OBJ 接入 FileIO、STL 去重复实现、群组通道端到端（解析 → IR → 转换 → `SyGroup`）、
extensionBlob 越界校验、独立构建 + 163 条 ctest 用例。
转换层已下移到 `Engine/3D/Src/Import/FioEntityConverter.cpp`（`Eg::FioEntityConverter`），
`Engine/3D` 里重复的 `StlLoader`/`ObjLoader` 已删除，UI3D 的 OBJ/STL 导入改走本 DLL。
导入链路日志已统一（每个 parser 的 `parseToIR START/END` 成对、提前 return 一律写明原因、
DLL 入口与工厂的失败分支不再静默），前缀分层见 §6.2。

**P1（下一批，主链路上的正确性缺口）**

- PLT 的 `SC` / `IP` 真实实现（当前是占位，带 `SC` 的图纸尺寸会偏）。
- SVG：圆弧/椭圆回拟合、`<g>` → `IrGroupInfo` 落地、`transform=` 走 `IrXform`。
  （「一条子路径聚合成一个图元」与「直线段识别」已完成，见 §5.3。）
- DXF：`HATCH` 边界轮廓（至少取外边界）、`IMAGE` 落地为 `EntityType::Image`。
- Native（`.sy`）迁移到 `Engine/Persistence`。
- 把 `_fileio_add_sibling()` 与 DLL 拷贝块上提到 `CMake/StandaloneInit.cmake`，
  各模块独立入口共用。


**P2（架构收敛，非阻塞）**

- 导出侧改吃 IR，去掉 `Writers/` 对 Engine2D/Engine3D 的 `dynamic_cast` 依赖，
  依赖闭包缩到 `Utility + Log`。
- `Fio::VecSyEntityPtr` 从公开头文件移除（现存的 ABI 技术债）。
- 写出侧曲线离散段数改为按弦高误差自适应，替换当前固定 20/40 段。
- Linux/macOS 上跑一次完整构建与 ctest。

**永远不做**

- 不在 `FioTypes.h` 里引入 `std::` 容器或虚函数——一旦引入，跨 DLL 与跨编译器
  复用能力立刻消失，这是本模块存在的前提。
- 不让 FileIO 依赖 Qt。
- 不做 DWG（闭源格式，libdxfrw 不支持；需要时走外部转换工具）。
- 不做 `.prt` 等厂商私有装配格式的原生解析。
- 不为「未来可能的格式」预留抽象层：新增格式的成本就是上面第 10 节那 7 步。

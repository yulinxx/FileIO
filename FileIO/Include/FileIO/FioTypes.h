#pragma once

// ----
// ---- FioTypes.h — 中立 IR POD 的转发头 ----
//
// 真正的定义已下沉到 EngineCommon 公共头区（Engine/FioContract/FioTypes.h）：
// Engine 的公开转换器直接使用这些 POD，而 FileIO 又链接 Engine2D/Engine3D；
// 定义留在本模块公开头会形成 Engine ↔ FileIO 的类型级循环依赖。
//
// 本文件只做转发，FileIO 内部及既有消费方的 include 路径保持不变。
// ----

#include "Engine/FioContract/FioTypes.h"

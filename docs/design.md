# 设计与预算

当前库版本为 0.1.0，使用 C++17，设备部署仅支持 ESP32-P4 和 ESP32-S31。
P4 的 ESP-IDF 基线为 5.5.5；S31 需要 ESP-IDF 6.1 或更新的兼容版本及对应工具链。
可移植核心也可被桌面创作工具包含。

## 接口与职责

`Scene`/`Group` 组织节点但不拥有其内存，`Mesh` 引用几何与材质；相机、模型与动画
共同使用右手坐标。`Renderer` 管 CPU 光栅与输出帧，`NiceGfxPresenter`
管独立显示提交，Studio 的媒体桥管界面内嵌。模型、动画、显示的所有权各自独立。

`Renderer::render()` 和 `AnimationMixer::update()` 不分配模型或逐帧工作区；
初始化和加载时注册资源可以使用 STL。渲染辅助任务在首次需要时创建，栈在初始化时预留。
ESP-IDF 常用的关闭 C++ 异常配置下，STL 分配失败遵循工具链的处理策略；
不能因为 `Model` 提供 `Status` 状态接口，就认为所有 STL 内存耗尽都能恢复。
应允许动态分配使用 PSRAM、限制输入大小并留出系统内存。

## 帧预算

默认启用深度测试时，RGB565 颜色与 16 位深度共 `width × height × 4` 字节：

| 内部尺寸 | 两块缓冲合计 |
| --- | ---: |
| 240×240 | 225 KiB |
| 320×240 | 300 KiB |
| 400×240 | 375 KiB |
| 512×300 | 600 KiB |

关闭 `depthTest` 后，颜色缓冲为 `width × height × 2` 字节。以上预算没有计入
原生显示双/三缓冲、界面、图像池、顶点工作区、纹理和模型。
渲染器保留一份自有颜色缓冲；独立显示提交器在返回前完成源读取。Studio
有图像池时直接渲染到独占槽位，完成后发布；无图像池时复制输出为界面快照。
异步消费者必须持有自己的帧，消费结束前不可覆盖。

`renderToSurface(scene, camera, target)` 同步借用外部 RGB565 目标。目标尺寸必须与
渲染器相同、`stride == width`，像素指针须满足 `uint16_t` 对齐，且不能与深度缓冲、
临时工作区或辅助任务存储重叠。调用结束后 `frame()` 恢复指向自有颜色缓冲，
外部目标不会被复制回来；必要的自有颜色缓冲、深度缓冲和临时工作区分配仍保留。

工作区约为 `maxVertices × sizeof(ClipVertex)` 加 `maxObjects` 相关表，
实际数值由 `stats().scratchBytes` 给出。`maxVertices` 是单个图元的最大顶点数；
`maxObjects` 包含场景根、分组、网格、灯光和骨骼等节点。容量不足返回
`CapacityExceeded`，渲染时不自动扩容。骨骼另外为每个蒙皮网格保留
逐骨位置/法线矩阵；多个图元会有独立的骨骼矩阵表。

`stats().bufferBytes` 只计渲染器自有颜色/深度缓冲，`scratchBytes` 只计其顶点、绘制项和
对象遍历表；它们不包括模型、缓存、辅助任务栈、界面快照和显示缓冲。缓存统计分别由
`Model` 的静态几何/纹理接口给出，详见 [静态几何](StaticGeometry.md) 与
[项目说明](../README.md#性能与内存)。内存偏好不是地址保证，不能由空闲堆大小推断某个块
已经位于内部 RAM。RTC/LP 也可属于内部堆；`InternalOnly` 和内部字节计数不保证
分配块位于主高性能域内存（HP DRAM）。

原生默认颜色缓冲使用 `InternalFirst`、深度和临时工作区使用 `ExternalFirst`，由单任务光栅化。
大型模型可显式把颜色/深度缓冲设为 `ExternalFirst`，先给顶点临时工作区留出内部 RAM，再建缓存：

```cpp
using namespace nice_gfx_3d;
RendererOptions options;
options.width = 512;
options.height = 300;
options.maxVertices = maxPrimitiveVertices; // 实际单个图元的最大顶点数
options.maxObjects = sceneObjectCapacity;  // 含骨骼、分组、网格、灯光等
options.colorMemory = MemoryPreference::ExternalFirst;
options.depthMemory = MemoryPreference::ExternalFirst;
options.scratchMemory = MemoryPreference::InternalFirst;
options.rasterWorkers = 2;
options.parallelVertexWorkers = true;
options.balanceRasterWorkers = true;
options.asyncClear = true;
const Status ready = renderer.begin(options);
if (ready != Status::Ok) return ready;
// 模型已加载成功；记录可选缓存状态，不因 NoMemory 隐藏模型。
const Status staticCache = model.optimizeStaticGeometry();
const Status textureCache = model.optimizeTextures();
```

`begin()` 的必要缓冲分配失败返回 `NoMemory`，不会替换原先可用的渲染器；可选缓存
则可以失败后继续用未缓存路径。缓存建立时 `Model::status()` 会反映该次操作，因此应用
应单独保存缓存状态，用实际 `render()` 结果决定是否发布本帧。不要把缓存失败误报为
模型加载失败。缓存建立、销毁及工作区重建均应在渲染器所有者空闲时进行。

`ModelCacheWorkspace` 与上面的渲染器临时工作区是不同的对象：前者保留不可变模型的
优化缓存载荷，后者存储逐帧变换和光栅临时数据。工作区不会保留整个模型，也不会保留
动画混合器或动作对象。`retainOptimizations()` 可在首次普通优化成功后接管原缓存，避免为了接入
工作区重新分配载荷；也可从首次初始化就用带工作区的两个优化重载。

工作区最多保存 32 个类型槽，静态组/纹理组成功后分别冻结其布局，不是任意容量的通用池。
重新加载模型不会自动建立缓存，需再次调用 `optimizeStaticGeometry(workspace)` 和
`optimizeTextures(workspace)`。模型清理只归还租约；工作区保留的字节仍占内存，
应计入其他模型同时加载时的峰值。工作区保留量包含模型在用的缓存，不能重复相加。
`end()` 在任何租约未归还时返回 `Busy`，不会强制回收。详细匹配与降级边界见
[跨重载缓存](StaticGeometry.md#跨重载保留缓存)。

## 光栅

顶点变换后做齐次六平面裁剪，避免近裁剪面穿越造成爆面。
屏幕扫描线插值投影深度；纹理坐标和顶点光照使用按跨度校正的透视属性。
16 位深度使用投影空间分布，近裁剪距离 `near` 不应过小；根据模型规模收紧近、远裁剪距离 `near/far`。

`perspectiveSpan` 默认 8，支持 1～32；设为 1 使用逐像素透视校正。
整个网格位于裁剪体内时，渲染器会在当前顶点临时工作区中就地保存屏幕坐标和
透视属性，供三角形复用；这不增加持久缓存，也不跨帧保留投影。`cachedProjectionMeshes`
和 `cachedProjectionVertices` 可确认实际使用量。需要裁剪的网格使用齐次裁剪路径。

不透明物体从近到远排序，透明物体按网格中心从远到近排序，无法保证交叉透明面
或网格内部透明面的正确顺序。双面 Lambert 漫反射材质沿用正面法线光照；可改用
Basic 原色材质，或提供足够的环境光。
闭合、不透明且刚性的双面组件可以显式使用离线拓扑提示选择背面剔除；运行时条件
不满足时保留双面绘制。原生挂接方法见 [静态几何](StaticGeometry.md)，Studio 导出
配置见 [Studio 集成](studio-integration.md)。

## 平台与并发

每个渲染器有一个所有者任务。默认 `rasterWorkers=1`；设为 2 时，支持静态任务与
双核的 ESP-IDF 端在初始化预留 16 KiB 内部辅助栈及任务描述符，首次需要时创建一个
固定在所有者另一核心的辅助任务。所有者和辅助任务光栅化互不重叠的行；同一辅助任务
也可先协助顶点计算。`parallelVertexWorkers` 默认 `true`，但须有双核辅助任务才会生效。
辅助资源或平台条件不满足时采用单任务回退，不能仅凭配置选项推断已经并行。

`balanceRasterWorkers=true` 通过每个网格的两路粗粒度计时调整行分配，独立于
`measurePhases`。`rasterWorkersUsed`、`rasterWorkerJobs`/`rasterWorkerFallbacks`、
`vertexWorkerJobs`/`vertexWorkerFallbacks`、`rasterSplitRow` 和两路耗时可确认实际路径；开启
`measurePhases=true` 后还可查看世界矩阵更新、蒙皮、顶点和三角形处理等阶段统计。

`asyncClear=true` 在支持的 ESP-IDF/PPA 路径提交非阻塞颜色/深度填充，与不访问输出缓冲
的顶点阶段重叠。光栅开始前及所有返回路径都会等待 DMA 完成；失败采用阻塞或 CPU 清屏。
它不把 `render()` 变成异步接口。`asyncClearFills`/`asyncClearFallbacks` 和启用阶段计时时的
`clearSubmitMicros`/`clearWaitMicros` 可用于诊断。默认值为 `false`，开关只能在帧之间修改。

应用可选择自己的所有者任务；`render()` 会等待全部辅助与清屏工作结束才返回。
场景、模型、动画混合器、相机、材质和源数组在这一期间禁止被其他任务修改、释放或重载。
不要并发调用同一渲染器。内部任务不是第二个显示拥有者。

`RenderTask` 可将整次渲染器调用交给独立工作任务，默认 `worker=false`，同步执行。
启用工作任务时默认另需 16 KiB 任务栈，与上述光栅辅助栈分别计入预算；资源分配失败
会保留同步模式，`begin()` 返回 `Ok`，通过 `available()` 和 `workerStatus()` 检查实际路径。
`begin()` 可以先绑定稳定的渲染器对象、预留任务栈，再调用 `Renderer::begin()`。

`submit()` 返回 `Ok` 只表示接收了一帧任务；即使同步执行，也必须用 `takeResult()`
消费结果并检查 `Result.status` 后才能再次提交。绑定的渲染器、场景和相机对象须
存活至任务 `end()` 或析构；从接收任务到 `takeResult()` 消费结果期间，不修改场景或
相机，不释放或覆盖目标缓冲。`complete()` 返回 `true` 表示渲染结果已完成发布，
`takeResult()` 归还这次任务的控制权。
`waitForCompletion()` 最多等待一个实时操作系统时钟节拍，需要再次检查 `complete()`；`end()`
等待在途工作结束。工作任务只渲染，显示提交仍由应用或界面所有者负责。

PPA 用于二维填充和最终图片缩放，不执行三角形或深度测试。
显示提交器仅在缩放尺寸、对齐和缓冲条件满足时使用 PPA，并同步缓存。
CPU 回退保留精确目标尺寸，不会为了硬件步长偷偷裁掉边缘。

## 运行诊断

结合应用自己的模型检查近裁剪、透明度、蒙皮、持续动画和内存峰值。区分
`Renderer::stats().renderMicros`、应用发布/合成耗时与视图完成帧率；物理垂直同步（VSYNC）
可能重复帧，渲染器缓冲的像素对照也不能代替面板观测。

Windows 会按大小写不敏感解析文件名，数学头使用 `Math3D.h`，避免遮蔽
标准库的 `<math.h>`。

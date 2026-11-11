# Studio 3D 性能与生命周期

适用于 ESP32-P4、ESP32-S31 的普通 Studio 导出工程。设备加载 NG3D 几何、
纹理和动画，实时渲染。

## 选择运行方式

从组件库新插入的 3D 组件显式选择高性能和轨道交互：

```html
<gfx-model3d id="viewer" data-model3d="robot"
  data-model3d-performance="dual-core"
  data-model3d-controls="orbit"
  data-model3d-width="320" data-model3d-height="240"
  style="width:480px;height:300px"></gfx-model3d>
```

| 设置 | 行为 |
| --- | --- |
| `performance="dual-core"` | 按需创建渲染任务，使用可用的顶点/光栅并行、缓存、异步清屏和三槽图像池。 |
| `performance="standard"` | 同步渲染、复制不可变 UI 快照；不创建外层渲染任务和图像池，调度上限约每秒 30 次。 |
| HTML 省略渲染方式属性 | 使用 `standard`。 |
| `controls="orbit"` | 设备单指绕模型旋转，双指缩放；默认启用。 |
| `controls="none"` | 不接管模型手势，由原有 UI 和导航处理输入。 |

表中渲染方式和交互设置对应完整属性 `data-model3d-performance` /
`data-model3d-controls`。内部分辨率独立于 CSS 显示尺寸。材质仍默认 `source`；
只有显式 `data-model3d-material="basic"` 才关闭运行时光照。

GT911/GT1151 由导出工程的同一个触摸采样器读取完整报告，地图、模型和普通
UI 共用结果。不要另外轮询 `NiceGfxOrbitInput` 或第二个控制器实例。覆盖在
模型上的按钮保留命中优先级；普通模型点击仍会触发 `View` 事件，拖动后不会误点。
其他单点驱动可以拖动，双指需要提供完整多点报告。

## 渲染和存储

`RenderTask` 只渲染模型，原来的 `View` 继续拥有显示器。输入回调只累计旋转/
缩放量；任务完成后，由 `View` 所在线程更新相机、动画或模型并提交下一帧。
渲染任务直接写入三槽 `ImageFramePool` 的预留目标，完成后把 RGB565 帧交给
UI 持有，最后一个引用释放后才可复用该槽；槽满时等待下一次调度，不覆盖
UI 正在读取的画面。任务或图像池不可用时保留同步渲染或复制快照回退。

高性能路径为每个组件用 `ModelCacheWorkspace` 保留其配置中初始资源的首次
成功缓存，同页换资源再切回时复用分配；其他资源采用普通缓存，离页结束工作区。
换模型、增加同页组件、提高分辨率仍会增加内存和计算需求。任务、缓存或池
分配失败时，检查诊断中的实际工作任务和图像池状态（`worker` / `pooled`）及错误；配置名称不能证明
所有优化都已启用。页面、过渡和缓存可能继续持有上一帧，所以离页不保证立即
释放全部像素内存。

完整 Arduino/ESP-IDF 导出默认无损压缩模型存储，设备校验恢复后的 SHA-256，
正常加载复制完成后释放临时解压区。`Model3DStorage.json` 记录每个资源的原始
大小、存储大小和编码。没有解压器的环境可通过导出 API 选择原始字节：

```js
createArduinoFiles(project, gfxRuntime, {
  model3dRuntime: modelRuntime,
  model3dStorage: 'raw' // 默认 'compressed'；createIdfFiles 同样支持
});
```

两种模式保持模型内容一致，闪存容量检查按选定模式计数。构建报告的
`linkedResourceBytesLowerBound` 是下限，最终还需以实际链接结果检查应用分区。
运行包缺少任务、图像池、跨页清理、闭合表面或完整触摸接口时，导出会要求更新。
自有 C++ 项目也可分别使用公共 `RenderTask`、`ImageFramePool`、
`ModelCacheWorkspace` 和 `ModelAssetBuffer`，沿用相同的任务和借用内存约束。

导出器还为各资产生成闭合表面提示，在高性能模式初始化时挂接。
它只标注绕序一致、刚性且闭合的网格组件，不删除模型数据；渲染器在不透明
材质及保守边界等条件满足时使用，否则走普通路径。生成数量见
`Model3DStorage.json` 中的 `closedComponents`，挂接结果见
`runtimeStats(index).diagnostics.hintsStatus`。

## 控制模型

生成的 `Model3DRuntime.h` 提供下列接口。`index` 是工程中的 3D 组件序号，
按页面和节点的编译顺序排列；资源 ID 则来自 `project.models3d[].id`。
在应用中集中定义组件索引，修改组件数量或顺序后重新核对。

| 接口 | 用途 |
| --- | --- |
| `requestAsset(index, "asset_id")` | 请求换资源；也接受资源序号，换资源默认从动画片段 0 开始。 |
| `requestClip(index, clip)` | 请求切换零起始动画序号。 |
| `setPlaying(index, bool)` | 播放或暂停。 |
| `home(index)` | 恢复该模型加载后的相机视角。 |
| `setControls(index, Controls::Orbit/None)` | 运行时开关轨道交互输入。 |
| `busy(index)` | 查询模型渲染任务是否在运行。 |
| `status(index)` / `runtimeStats(index)` | 查询加载、渲染、发布状态和实际资源使用。 |

以上接口都在 `ui_models3d` 命名空间。请求返回 `nice_gfx_3d::Status`；
`Ok` 表示请求已接受，切换会在安全的空闲边界执行。只从拥有 `View` 的线程调用，
不要从中断服务程序或另一个任务直接修改模型、相机、缓存或 `Renderer`。

```cpp
constexpr size_t Viewer = 0;
// 可在 UI 回调里请求；无需自行等待当前帧结束。
if (ui_models3d::requestAsset(Viewer, "robot") == nice_gfx_3d::Status::Ok) {
  ui_models3d::requestClip(Viewer, 1);
  ui_models3d::setPlaying(Viewer, true);
}
```

## 开始、轮询和结束

完整导出的初始化入口（`setup` / `main`）已调用 `begin(view)`，并在 `pollUi()` 前后各调用一次
`ui_models3d::poll(view, now)`。保持这些调用，不再添加第二套输入或显示循环。
自有工程也采用同一顺序；一个运行时同时只绑定一个 `View`。

会动态切换多个资源的组件，可以在首次 `poll()` 前这样预留最大工作区：

```cpp
ui_models3d::begin(view, ui_models3d::preallocateAssets());
```

它根据本次导出的资源计算顶点/对象容量，避免切换模型时扩容重建渲染器。
已显式设置的更大容量和其他选项保留；隐藏组件不会因此创建渲染器或像素池。
单模型工程使用普通 `begin(view)`，避免为不需要的资源预留空间。换模型同时
要指定初始动作时，在同一次回调中依次 `requestAsset()` 和 `requestClip()`，
让首次定位使用目标动作，避免先按另一个动作定位。

预留容量用于同一个活动组件的资源切换。离开页面仍会释放渲染工作区，
重新进入时 `InternalFirst` 只是分配偏好；内部连续空间不足会退到外部内存，
帧率可能降低。需要分别测量首次进入、同页切换和跨页返回的速度。运行时
默认完整释放，避免为已离开的页面继续占用大块内部内存。

普通导出保持默认 `RuntimeOptions`。`externalFrameOwner=true` 供自有动画控制器
接管调度：它不创建通用任务、图像池或触摸绑定，也不自动执行排队请求。
控制器必须用自己的 `RenderTask::busy()` 判断安全边界；通用 `busy(index)`
不能看到外部任务。自有控制器负责动作、镜头和调度，可使用公共渲染任务、
像素池、解码和缓存 API。

换页时运行时会收取并丢弃旧页结果，停止旧任务，清理该组件的图像覆盖，按
引用归还情况释放池和模型。其他组件的图像不受清理影响。销毁、替换 `View` 或
结束整个运行时时，先调用 `ui_models3d::end(view)`：

- 返回 `Ok` 后才可销毁 `View` 或开始绑定另一个 `View`。
- 返回 `Busy` 时保留 `View` 和运行时，释放应用持有的图像引用、完成 UI 工作后
  重试。缓存可能长期持有旧图；必要时等刷新/转场完成，再以
  `view.configureRenderCache(nullptr, 0)` 成功返回为准停用缓存，随后重试结束。
- 不要强行释放池，或因为模型已离页就假定像素缓冲无人使用。

## 测量实际效果

这是软件 3D 渲染。三角形数量、蒙皮、材质、像素覆盖、UI 合成、PSRAM 带宽和
显示接口共同决定速度，不能对所有场景承诺每秒 30 帧。先从 240×180 / 320×240
开始，再在目标板上提高分辨率。

静态、暂停且没有相机变化的模型会停止重复发布相同画面；此时新增模型帧数
可以为零，输入轮询仍继续。拖动或缩放会重新请求渲染。

在固定页面用真实时间差分别统计 `runtimeStats(index).diagnostics.published`
和 `view.runtimeStats().completedFrames`，同时查看错误和丢弃计数；组件重建后
计数会重置。单次渲染耗时的倒数不能代替整体 UI 帧率。


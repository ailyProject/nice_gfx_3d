# NICE GFX Studio 集成

Studio 提供模型导入、组件属性、CPU 实时预览和原生导出桥接。浏览器预览用于编辑构图，设备运行原生 C++ 渲染器。

## 导入与编辑

在 Studio 中先选择 ESP32-P4 或 ESP32-S31 硬件。组件面板出现“3D 模型 / 动画”；插入后在右侧“3D 模型与动画”中选择“导入 GLB / glTF”。GLB 选一个文件；glTF 需要同时选中 `.gltf`、它引用的 `.bin` 和 PNG 贴图。导入器只读取用户选中的本地文件，不从资源 URI 请求网络。资源目录无法精确匹配时按文件名匹配；同名资源有歧义会报错。

导入通过与命令行相同的 `tools/compile-model.mjs` 转换成 NG3D。项目 JSON 的 `models3d` 保存 `{id,name,data,clips,warnings}`，`data` 为原生 NG3D 二进制的 Base64 编码，`clips` 为片段名称与秒数，`warnings` 为材质近似等转换提示。组件引用资源 ID，不存储截图、GIF 或预渲染帧。保存、撤销以及重新打开工程保留这些数据。

资源总量限 8 MiB，最多 32 个资产；每次导入源文件总量也限 8 MiB。Studio 使用最多 2048 个场景对象（含运行时根和灯光）以及每个图元最多 32768 个顶点的硬限制。这些是容量边界，不是流畅运行的建议规模。实际应从几百到几千个可见三角形和 240×180 内部画面开始。

当前导入 glTF 2.0 / GLB 的三角形网格、节点层级、基础颜色/PNG 纹理、顶点 RGB、四权重骨骼蒙皮，以及采用线性（`LINEAR`）或阶跃（`STEP`）插值的平移、旋转和缩放动画。基于物理的渲染（PBR）中的金属度/粗糙度仅近似处理并显示提示。法线/遮蔽/金属粗糙度等额外贴图、形态目标、Draco/meshopt 压缩和其他 glTF 扩展不在当前支持集；不支持的内容应先在建模软件中烘焙、简化或转换，导入器会报告具体问题。完整二进制能力见 [资源格式](asset-format.md)。

## 持久化属性

```html
<gfx-model3d id="robot"
  data-model3d="model3d"
  data-model3d-clip="0"
  data-model3d-autoplay="true"
  data-model3d-loop="true"
  data-model3d-rotate-y="0"
  data-model3d-spin="15"
  data-model3d-width="240"
  data-model3d-height="180"
  data-model3d-camera-distance="0"
  data-model3d-performance="dual-core"
  data-model3d-material="source"
  data-model3d-controls="orbit"
  style="width:480px;height:360px;background-color:#101723">
</gfx-model3d>
```

`data-model3d` 是 `project.models3d` 中的资源 ID。动画片段序号 `clip` 从 0 开始；自动播放和循环默认均开启。Y 轴初始角度与 `spin` 的单位分别是度、度/秒；自转独立于动画片段播放。内部渲染宽度限 32–640、高度限 32–480；CSS 尺寸是显示大小，二者独立。相机默认 50° 透视，`camera-distance=0` 根据初始世界空间/蒙皮后的包围盒自动取景；正数为模型单位中的手动距离。动画移动超出初始包围盒时需要手动调整距离。

右侧属性面板的“渲染方式”“材质显示”和“设备交互”是独立设置，随 HTML 和项目 JSON 保存。组件库新插入的组件显式使用 `dual-core/orbit`；HTML 中省略属性时使用下表默认值：

| 属性 | 默认 | 作用 |
| --- | --- | --- |
| `data-model3d-performance` | `standard` | `standard` 同步渲染并复制 UI 快照；`dual-core` 尝试使用渲染任务、两路光栅、并行顶点、负载平衡、异步清屏、缓存和三槽图像池。 |
| `data-model3d-material` | `source` | `source` 保留已导入的原生材质；`basic` 明确选择原色材质，保留纹理、透明度和顶点色，关闭运行时灯光。 |
| `data-model3d-controls` | `orbit` | `orbit` 使用单指旋转、双指缩放；`none` 交回原有 UI 和导航。接线与输入约定见 [交互指南](Interaction.md)。 |

双核不会更改材质。原色（Basic）模式会改变受光表面的外观，适用于明确希望展示原始贴图颜色的组件。当前 NG3D 导入使用 Lambert 漫反射材质，不能完整保留 glTF 的所有着色扩展；这里的 `source` 指转换后的材质。

双核模式先尝试预留外层渲染任务，再加载模型、创建 `Renderer` 和顶点临时工作区，随后尝试静态几何和纹理缓存。缓存返回 `NoMemory`、`CapacityExceeded` 或 `Unsupported` 时继续显示模型。`ui_models3d::staticCacheStatus(index)` 和 `textureCacheStatus(index)` 单独报告缓存结果；`runtimeStats(index)` 提供实际工作任务、图像池、缓存占用和发布诊断。`vertexParallelSupported()` 和 `rasterBalancingSupported()` 查询核心能力，实际任务或硬件资源是否启用仍以运行状态为准。负载平衡的独立任务计时不依赖开启 `measurePhases`。

每个双核组件通过 `ModelCacheWorkspace` 保留其配置中初始资源的首次成功缓存，同页切换到其他资源后，切回该初始资源时重新借用。其他资源使用普通缓存；离开页面会结束工作区。工作区保持已有缓冲地址，内部内存偏好不保证地址属于主高性能域内存（HP DRAM）。精确布局匹配和生命周期约束见 [跨重载缓存](StaticGeometry.md#跨重载保留缓存)。

导出器为每个资产分析绕序一致、刚性且闭合的网格组件，生成保守的闭合表面提示，并在双核初始化时挂接。它不删除顶点、索引或动作；仅在兼容的不透明材质、相机和近裁剪面位于保守边界外等条件成立时使用，否则保持普通渲染路径。`Model3DStorage.json` 的 `closedComponents` 记录每个资产生成的组件数量，运行时结果见 `runtimeStats(index).diagnostics.hintsStatus`。

其他 MCU 不显示组件入口，粘贴 HTML 或切换到其他目标时编译诊断也会拒绝 3D。标准 ESP-Mosaico 的 ESP32-S31 板型在允许范围中。

## 实时预览

浏览器通过 Studio 项目的 `packages/html-ui/src/model3d-software-preview.mjs` 使用同一 NG3D 数据进行 CPU 光栅化，含节点动画、骨骼蒙皮、透视 UV、深度测试、近裁剪、基础透明度和镜像节点剔除。资源默认（`source`）预览使用世界空间法线、逆转置法线矩阵和与原生组件相同方向的环境光/方向光近似；原色模式跳过灯光，切换不会修改资源材质。非均匀缩放的蒙皮法线也按逆转置处理。编辑状态显示初始姿态；进入播放模式后按实际经过时间计算所选动画片段和自转，最多每 33 毫秒请求一次刷新。离开播放模式回到初始姿态。

项目应用若在加载后显式将某些材质设为原色材质，可在资产上保存可选的 `previewUnlitMaterials:number[]`（材质索引）供预览匹配；它不改变 NG3D 字节或原生材质，普通导入不添加此标注。索引重复、非法数值和超出实际材质范围会被拒绝。

浏览器是独立的软件预览实现，与板端的灯光、纹理过滤、深度精度和像素取整不保证一致。它不是板端性能模拟器。动画资源仍是顶点/骨骼/关键帧数据，不会在浏览器中录制后转成图片序列。

## Arduino / ESP-IDF 导出

完整 Arduino / ESP-IDF 工程增加 `Model3DRuntime.h`、`Model3DStorage.json` 和 `Model3DReadme.md`。主程序在 UI 挂载后调用 `ui_models3d::begin(view)`，在 `pollUi()` 前后调用 `ui_models3d::poll(view, now)`。启用目标板的 PSRAM。导出自动包含核心源码和许可，不需要额外安装 NICE_GFX_3D 或全局 NICE_GFX 库。

Studio 项目的 `scripts/package-model3d-runtime.mjs` 从本库 `src/` 打包 3D 核心；启动、构建前通过 `scripts/prepare-model3d-runtime.mjs` 生成并校验 `public/assets/nice-gfx-3d/runtime.zip`。源码包由 Studio 保存和发布，Angular 资源路径为 `assets/nice-gfx-3d/runtime.zip`。导出时检查清单格式、版本、路径、文件大小与每个源文件的 SHA-256 摘要，保留清单和原始许可。Arduino 放入工程的 `src/nice_gfx_3d/`；ESP-IDF 放入 `components/nice_gfx_3d/src/` 并生成原生 CMake 依赖，不引入 Arduino。包包含核心头文件及 `Memory`、`Renderer`、`Model`、`ModelCacheWorkspace`、`RenderTask` 实现，不含另一个显示拥有者 `Presenter`；显示仍由原 UI 调度。

从 JavaScript API 直接导出时，在导出选项中传入 `model3dRuntime: Uint8Array`（源码 ZIP）；Studio 的导出工作线程会自动获取并传入。浏览器直接调用导出器时也可从同一资源地址获取。完整导出验证图像池、跨页清理、触摸和 3D 核心 API，缺少所需能力时要求更新运行包。P4 的原生 IDF 最低版本为 5.5.5，S31 为 6.1。

NG3D 数据以无损压缩形式嵌入闪存，加载前恢复并校验 SHA-256 摘要，`Model` 复制完成后释放临时解压区；导出 API 可用 `model3dStorage: 'raw'` 选择原始字节。桥接器把组件编译成现有动态媒体（`Media`）节点，使用原生 `Model`、`AnimationMixer`、`Renderer`，以 RGB565 原生字节顺序交给 NICE_GFX UI。页面合成、裁剪与路由仍由原 UI 执行。只有活动页面加载模型；页面退出并结束过渡后清理模型和渲染缓冲，图像池等待 UI 归还引用。每个模型的 `Renderer` 容量从实际图元最大顶点数、实例化对象总数计算。

`standard` 模式在 `View` 所有者任务同步更新动画和渲染，保留 33 毫秒调度门限；通过 `View::setImage(ImageView)` 复制 RGB565 表面，创建不可变 UI 快照。`dual-core` 模式按需使用公共 `RenderTask`，直接渲染到三槽 `ImageFramePool` 中预留的目标，任务完成后由 `View` 所有者发布图像。图像槽由 UI 持有期间不会被渲染任务覆盖。任务或池不可用时保留同步或复制回退。模型、相机、缓存和动画的更新仍须在安全的空闲边界执行。

颜色和深度至少各约 `W×H×2` 字节，再加模型、缓存、蒙皮、临时工作区、辅助任务栈、图像池或快照及屏幕缓冲。实际速度和峰值内存取决于模型、分辨率、PSRAM 与显示争用。`ui_models3d::status(index)` 可取得模型初始化或渲染的原生状态码（`Status`）；失败保留先前成功发布的 UI 图像。修改 3D 资产或属性时，需要一起替换 `ui.h` 和 `Model3DRuntime.h`。完整的运行方式、模型控制和回收约定见 [Studio 性能与生命周期](StudioPerformance.md)。

导出源码身份以清单为准。独立 `ui.h` 本身不加载模型，必须连同上述桥接或自己的应用逻辑使用。源码 ZIP 打包只复制文件并生成清单。

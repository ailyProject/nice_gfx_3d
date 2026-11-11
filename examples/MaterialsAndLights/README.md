# MaterialsAndLights：材质和灯光

默认绘制调度间隔为 16ms；实际帧率取决于渲染、显示提交和触摸读取。
输入配置与 UI 共用触摸见 [Interaction.md](../../docs/Interaction.md)。

本例独占整屏，使用 `presenter.presentFit(renderer.frame())` 保持比例，
每帧覆盖整个屏幕并把全部留边填黑；第二个参数可指定 RGB565 背景色。
需要保留外围内容时使用 `present(surface, viewport)`，不要使用整屏填边 API。

单指拖动绕三块材质的整体中心转动相机，双指捏合缩放；串口 `h` 恢复初始相机。
旋转光源与 `a` 环境光开关照常工作，相机不会被逐帧重置。触摸在帧门限前读取。
`Orbit touch probe: ready/unavailable` 启动日志显示控制器探测结果。
默认支持 P4 JC1060 GT911 / S31 Korvo GT1151；使用其他触摸屏需调整
`NiceGfxOrbitInput` 的控制器、地址和原始触摸尺寸。

用于产品展示和模型检视的材质入门示例。三块免素材方块隔离材质与灯光的差异，
适合先理解 API；复杂模型及其动画应使用真实模型播放器示例。这里三个方块几何与姿态相同：

| 位置 | 材质 | 用途 |
| --- | --- | --- |
| 左侧青色 | `MeshBasicMaterial` | 不受灯光影响，保留基础颜色 |
| 中间青色 | `MeshLambertMaterial` | 与左侧同色，观察表面朝向造成的亮暗 |
| 右侧暖色 | `MeshLambertMaterial` | 同一照明下使用另一种材质颜色 |

`AmbientLight` 给 Lambert 表面一个基础亮度。`DirectionalLight` 持续绕 Y 轴转动，
明暗随光照方向变化。发送串口字符 `a` 或 `A` 可打开/关闭环境光，Basic 方块不受影响。

## 运行

1. 安装 `nice_gfx` 与 `nice_gfx_3d`，使用支持 P4/S31 的 Arduino core 和板卡实际 PSRAM 配置。
2. 核对本目录 `Hardware.h`。默认 P4 对应 **JC1060P470C_I_W_Y**，默认 S31 对应
   **ESP32-S31-Korvo-1**。其他板卡须用真实的 nice_gfx 显示预设或配置替换 `display` 声明，
   本例没有臆定其他屏幕的引脚。
3. 打开 `MaterialsAndLights.ino`。串口监视器选 115200；无需外部模型或资源文件。

本例内部渲染为 320×240，显示时用 `presentFit()` 保持比例并每帧写入黑色留边。
`maxVertices=64` 为单个网格容量，`maxObjects=12` 包括场景、网格和灯光。

## API 要点

- `Color(0xRRGGBB)` 转换为库使用的 RGB565；直接整数颜色参数按 RGB565 解释。
- `sun.direction` 指光线传播方向，设定后归一化；`sun.rotation` 再把局部方向旋转到世界空间。
  平行方向光不随位置变化。本例不包含阴影贴图。
- Lambert 使用顶点法线和简单漫反射，几何的法线会影响外观。BoxGeometry 自带每面法线。
- 三个网格共享几何，材质是各自独立的。它们都是借用关系，需保持资源生命周期。

真实模型展示也可以复用这里的环境光和旋转方向光：把 `Model::scene()` 加入场景，
根据素材与预期展示风格调整光的强度和方向。Basic 适合不希望受光照影响的颜色预览；
Lambert 适合用明暗观察表面形状。模型加载器导入的材质仍由 Model 持有，保持 Model
存活；不要把本例三个方块的材质指针直接覆盖到需要不同贴图的全部模型网格上。

资源只构造一次，`setup()` 配置并连接场景，`loop()` 复用画布并只更新灯光旋转。
父对象先声明、子网格晚于几何和材质声明，避免先释放仍被借用的对象。
显示 `Busy` 会跳过一次提交；其他错误打印状态并停止绘制。


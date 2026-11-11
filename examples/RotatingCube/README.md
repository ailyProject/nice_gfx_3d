# 最小旋转网格

默认绘制调度间隔为 16ms；实际帧率取决于渲染、显示提交和触摸读取。
输入配置与 UI 共用触摸见 [Interaction.md](../../docs/Interaction.md)。

本例独占整屏，使用 `presenter.presentFit(renderer.frame())` 保持比例，
每帧覆盖整个屏幕并把全部留边填黑；第二个参数可指定 RGB565 背景色。
需要保留外围内容时使用 `present(surface, viewport)`，不要使用整屏填边 API。

单指拖动绕网格中心转动相机，双指捏合缩放；串口发送 `h` 恢复初始相机。
物体自身的旋转继续播放，相机视角会保留，不被动画覆盖。触摸在帧调度门限前读取。
启动串口的 `Orbit touch probe: ready/unavailable` 报告控制器探测结果；未接触摸也能运行。
默认触摸配置对应 P4 JC1060 的 GT911 与 S31 Korvo 的 GT1151。
更换板卡时同时调整 `NiceGfxOrbitInput` 的控制器、I2C 地址和原始触摸尺寸。

从本例了解一个完整帧的调用顺序：初始化 display → 注册场景对象 →
`Renderer::begin()` → loop 更新变换 → `render()` → `presentFit()`。
无需模型文件，适合先排除屏幕和 PSRAM 配置问题。

直接打开同名 `.ino`。安装两个库，选择实际 MCU/板卡并启用 PSRAM。
显示声明位于 `.ino` 顶部：P4 默认 JC1060，S31 默认 Korvo；其他板卡请替换。

本例的场景、几何、材质、相机和 Renderer 都创建一次。屏幕画面由低分辨率
RGB565 表面放大，`presentFit()` 保持模型比例并每帧填入留边颜色。
更完整的产品展示相机见 CameraViews；真实动画请看 ModelFromFlash /
ModelFromLittleFS / RobotExpressivePlayer。


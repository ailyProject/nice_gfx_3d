# 关键帧 API 入门

默认绘制调度间隔为 16ms；实际帧率取决于渲染、显示提交和触摸读取。
输入配置与 UI 共用触摸见 [Interaction.md](../../docs/Interaction.md)。

本例独占整屏，使用 `presenter.presentFit(renderer.frame())` 保持比例，
每帧覆盖整个屏幕并把全部留边填黑；第二个参数可指定 RGB565 背景色。
需要保留外围内容时使用 `present(surface, viewport)`，不要使用整屏填边 API。

单指拖动绕动画中心转动相机，双指捏合缩放；串口 `h` 仅恢复相机，`r` 仍重播动作。
暂停动画后也能调整观察视角。触摸读取位于 16ms 门限之前，不随动画暂停。
启动日志 `Orbit touch probe: ready/unavailable` 报告控制器探测状态。
默认适配 P4 JC1060 GT911 / S31 Korvo GT1151，更换板卡时也需调整
`NiceGfxOrbitInput` 的触摸控制器、I2C 地址和原始坐标尺寸。

本例用于阅读 Track / Clip / Action / Mixer 的对象关系。
真实角色的多个动作、骨骼与贴图请先看 ModelFromFlash 和 RobotExpressivePlayer。

打开同名 `.ino`，板卡选择与 `Hardware.h` 一致，启用 PSRAM。
原有 `KeyframeAnimation.h` 现在配有完整的 setup/loop，可独立作为 Arduino 示例打开。

关键代码在头文件中：

1. `times` 是秒，`positions` 按每个关键帧的 x/y/z 紧密排列。
2. `track.target` 指向节点，`property` 选择 Position。
3. Clip 借用 Track，Mixer 注册 Action；这些数组和对象始终有效。
4. loop 传入真实经过的秒数，Mixer 自动插值；另一个旋转展示直接更新属性。

串口波特率 115200：`p` 暂停/继续，`r` 从头播放，`1` 重复、`2` 往返、`3` 单次。
单次模式只结束关键帧位移动作，直接更新的旋转仍会继续。

本例展示关键帧 API，真实角色效果见模型播放器。

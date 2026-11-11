# RGB565 贴图与镂空材质

默认绘制调度间隔为 16ms；实际帧率取决于渲染、显示提交和触摸读取。
输入配置与 UI 共用触摸见 [Interaction.md](../../docs/Interaction.md)。

本例独占整屏，使用 `presenter.presentFit(renderer.frame())` 保持比例，
每帧覆盖整个屏幕并把全部留边填黑；第二个参数可指定 RGB565 背景色。
需要保留外围内容时使用 `present(surface, viewport)`，不要使用整屏填边 API。

单指拖动绕两个材质样品的整体中心转动相机，双指捏合缩放，串口 `h` 复位。
贴图网格的自动旋转继续播放，相机保留手动视角；触摸在帧门限之前读取。
启动串口 `Orbit touch probe: ready/unavailable` 报告触摸探测状态。
默认配置为 P4 JC1060 GT911 / S31 Korvo GT1151，其他板卡需同时调整
`NiceGfxOrbitInput` 的控制器、I2C 地址及触摸原始分辨率。

这是材质 API 参考，说明产品 Logo、贴纸、镂空面板等如何使用颜色与 Alpha8。
查看真实角色动画请先运行 ModelFromFlash、ModelFromLittleFS、RobotExpressivePlayer。

打开 `TexturedMaterials.ino`。安装 NICE_GFX 与 NICE_GFX_3D，启用 PSRAM，
在 `Hardware.h` 选择实际显示预设。默认 P4 为 JC1060，S31 为 Korvo。

左侧网格使用 RGB565 棋盘贴图；右侧圆形镂空贴片附加 Alpha8 平面，
孔洞后方可以看到紫色底板。所有贴图仅在 setup 初始化。

- `Texture` 借用像素，不复制；数组和材质须活到最后一帧。
- `stride` 单位是像素，颜色与 Alpha8 的行步距一致。
- `alphaTest=0.5` 是二值覆盖，保留深度写入，适合叶片、标签和格栅。
- 渐变透明需 `transparent=true`、通常 `depthWrite=false`，并考虑网格排序限制。
- 颜色需要 `Color(0xRRGGBB)` 转换，直接整数参数是 RGB565。

当前库使用最近邻纹理采样，本例无需外部素材。

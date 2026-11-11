# SceneHierarchy：父子变换和几何共享

默认绘制调度间隔为 16ms；实际帧率取决于渲染、显示提交和触摸读取。
输入配置与 UI 共用触摸见 [Interaction.md](../../docs/Interaction.md)。

本例独占整屏，使用 `presenter.presentFit(renderer.frame())` 保持比例，
每帧覆盖整个屏幕并把全部留边填黑；第二个参数可指定 RGB565 背景色。
需要保留外围内容时使用 `present(surface, viewport)`，不要使用整屏填边 API。

单指拖动绕整个装配中心转动相机，双指捏合缩放，串口 `h` 恢复初始相机。
父子动画继续运行，手动视角会保留。触摸读取放在 16ms 帧门限之前。
启动日志 `Orbit touch probe: ready/unavailable` 显示控制器探测结果。
默认适配 P4 JC1060 的 GT911 / S31 Korvo 的 GT1151；更换板卡时也需调整
`NiceGfxOrbitInput` 的 I2C 地址、控制器类型和原始触摸尺寸。

完整 Arduino 示例，展示 `Scene`、两层 `Group`、`Mesh` 和共享几何。
画面中橙色方块是中心，青色小方块绕中心运动，绿色细方块随整个装配转动。

场景关系是：

```text
Scene
├─ assembly（绕 Y 轴转动）
│  ├─ hub（橙色）
│  ├─ marker（绿色）
│  └─ orbit（绕局部 Z 轴转动）
│     └─ satellite（青色，局部 X=1.35）
├─ AmbientLight
└─ DirectionalLight
```

三个网格借用同一个 `BoxGeometry`，各自具有独立材质、位置和缩放。
`satellite.position` 相对它的父对象 `orbit`；父对象的变换继续叠加到子对象上。
Renderer 会在绘制前更新世界矩阵。

## 运行

1. 安装同级的 `nice_gfx` 和 `nice_gfx_3d` Arduino 库，使用支持目标芯片的 core。
2. 选择实际 ESP32-P4 或 ESP32-S31 开发板，按板卡要求启用 PSRAM。
3. 核对本目录 `Hardware.h`：P4 默认对应 **JC1060P470C_I_W_Y**，S31 默认对应
   **ESP32-S31-Korvo-1**。其他板卡须替换 `display` 声明为真实的 nice_gfx
   显示预设或配置；本例没有提供通用屏幕引脚。
4. 打开 `SceneHierarchy.ino`。串口波特率为 115200，无需模型文件、SD 卡或转换步骤。

渲染尺寸固定为 320×240，保持比例放大到屏幕；屏幕多出的区域为黑边。
`maxVertices=64` 是单个网格的容量，`maxObjects=16` 包括分组、灯光等场景对象。

## 修改练习

- 调整 `satellite.position.x`，改变轨道半径。
- 把 `orbit.rotation.z` 改为 `orbit.rotation.y`，观察局部旋转轴的差异。
- 只改变一种材质的 `color`，其他网格不会跟着变色；改变共享几何则会影响三个网格。

对象和资源只建立一次，`loop()` 复用它们。父子关系和几何/材质指针均非拥有；
本例让父对象先声明、子对象后声明，退出时先销毁子对象。显示资源暂忙会跳过一次提交；
其他初始化或绘制错误打印状态并停止绘制。


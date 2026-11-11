# 最小 ESP-IDF 接入

本工程只使用内建 BoxGeometry，无外部资产，方便先确认 nice_gfx 显示预设、
PSRAM 和渲染提交路径。真实模型完整应用见相邻 `esp-idf-model-player/`。

保持工程在原目录中，CMake 会引用同级库；`main/main.cpp` 中 P4 默认 JC1060，
S31 默认 Korvo。其他板卡请替换为实际显示配置。

板载触摸支持以方块中心为目标的单指轨道旋转、双指缩放；串口 `h` 恢复初始相机。
默认绑定 S31 Korvo 的 GT1151、P4 JC1060 的 GT911。触摸初始化失败会记录状态并继续渲染，
更换板卡时须按真实硬件调整显示和输入配置。输入更新位于每次绘制之前，不重建相机或资源。

```sh
idf.py set-target esp32p4
idf.py menuconfig
idf.py build flash monitor
```

P4 使用至少 IDF 5.5.5，S31 使用支持它的至少 IDF 6.1，并按 SDK 选择相应目标。
在 menuconfig 中核对板卡 PSRAM、Flash、芯片修订与分区。
绘制采用 16ms 调度间隔；实际帧率取决于渲染和显示提交。
触摸配置与共享输入见 [Interaction.md](../../docs/Interaction.md)。

输出使用 `presenter.presentFit(renderer.frame())` 保持比例，每帧覆盖整个屏幕并把全部留边填黑。
第二个参数可指定 RGB565 背景色。需要保留外围内容时，改用 `present(surface, viewport)`。

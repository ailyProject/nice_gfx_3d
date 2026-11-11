# ESP-IDF：真实 Fox 骨骼动画播放器

这是完整原生 IDF 工程，无 Arduino 组件。预附 Fox 的 NG3D 字节头，
加载真正的网格、贴图、24 骨骼与 Survey / Walk / Run 动作，运行时实时渲染。

## 准备

1. 保持本目录在 `nice_gfx_3d/examples/` 下，旁边工作区有 `nice_gfx`。
2. `main/Hardware.h` 默认 P4 JC1060P470C_I_W_Y，S31 Korvo。
   其他板卡必须换成实际显示预设，不可照搬引脚。
3. 使用目标支持的 IDF：P4 至少 5.5.5，S31 至少 6.1。
   设置实际板卡的 Flash、PSRAM、分区与芯片修订版本。
4. 资源数据约 293 KiB；还需给固件代码留出足够的 app 分区。

```sh
idf.py set-target esp32p4
idf.py menuconfig
idf.py build flash monitor
```

S31 按所用 SDK 的预览目标配置方式选择 `esp32s31`。
触摸接入见 [Interaction.md](../../docs/Interaction.md)。

绘制采用 16ms 调度间隔；实际帧率取决于渲染和显示提交。
输出用 `presenter.presentFit(renderer.frame())` 保持比例，每帧覆盖整个屏幕并把全部留边填黑。
第二个参数可指定 RGB565 背景色。需要保留外围内容时，改用 `present(surface, viewport)`。

## 操作

串口命令：`n/b` 切换动作，`p` 暂停，`r` 重播，`l` 循环/单次，
`+/-` 调速，`a/d` 转动模型转台，`w/s` 缩放，`h` 恢复初始相机。终端可能需要回车后才提交输入。
控制台不能配置为非阻塞时，程序禁用串口控制并继续播放。

板载触摸支持单指绕模型中心旋转相机、双指缩放。模型已归一化到原点，
OrbitControls 以该中心为目标，触摸后不会被循环中的固定相机位置覆盖。
默认 S31 Korvo 使用 GT1151，P4 JC1060 使用 GT911；触摸失败记录状态后继续播放。

程序展示的实用步骤：先通过 ModelView 读取资源预算，按每个 primitive 的最大
顶点数配置 Renderer；setup 计算蒙皮后的包围盒、归一化取景；一次注册所有
动作；同一任务处理输入、动画、绘制和显示。每两秒记录当前设备上的实际
render/present 耗时与发布帧率。

`main/FoxModel.h` 为随例资源。替换模型时重新生成头文件并修改数组名；
容量与取景代码会根据新资源计算，不必手抄顶点数。

版权、改动与上游地址见 [ASSET_LICENSE.md](ASSET_LICENSE.md)。

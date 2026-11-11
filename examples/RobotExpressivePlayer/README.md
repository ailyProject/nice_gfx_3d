# RobotExpressive：真实机器人多动作播放器

默认绘制调度间隔为 16ms；实际帧率取决于渲染、显示提交和触摸读取。
输入配置与 UI 共用触摸见 [Interaction.md](../../docs/Interaction.md)。

本例独占整屏，使用 `presenter.presentFit(renderer.frame())` 保持比例，
每帧覆盖整个屏幕并把全部留边填黑；第二个参数可指定 RGB565 背景色。
需要保留外围内容时使用 `present(surface, viewport)`，不要使用整屏填边 API。

单指拖动绕整个机器人观察，双指捏合缩放；串口输入 `h` 并回车恢复初始相机，
`r` 仍重播动作。模型已在初始化时居中，控制目标为世界原点。
触摸会关闭自动转台，之后保持手动相机；输入 `o` 可再次启用模型转台。
触摸在帧调度门限之前读取，不影响暂停、动作切换或交叉淡化。
启动 `Orbit touch probe: ready/unavailable` 日志报告探测状态。默认适配
P4 JC1060 GT911 / S31 Korvo GT1151；其他板卡需调整 `NiceGfxOrbitInput` 的配置。

这套示例使用 three.js 官方示例目录里的 **RobotExpressive**，不是程序生成的圆柱或方块替代动画。机器人包含完整身体层级、两套手部蒙皮、行走、跑步、跳跃、跳舞、挥手等动作。程序在板上实时计算关键帧、层级和蒙皮，不播放 GIF 或预渲染视频。

硬件默认选项写在本目录独立的 `Hardware.h`：ESP32-P4 使用 JC1060P470C_I_W_Y；ESP32-S31 使用 Korvo-1。请按实际 MCU 选择 Arduino 开发板，启用 PSRAM，并安装本工作区的 NICE_GFX 与 NICE_GFX_3D 库。其他屏幕需要替换本地硬件适配，不能直接套用接线。此示例不依赖相邻示例的头文件。

打开 `RobotExpressivePlayer.ino` 即可看到完整程序；`RobotExpressiveData.h` 是已经生成并放在同一 sketch 目录中的原生模型数据。它不需要 SD 卡、联网或其他外部运行时资源。原始文件与转换产物仍保存在 `../assets/`，方便追溯或重新制作。

## 画面与操作

程序以 320×240 内部分辨率渲染，再保持比例放大到屏幕。启动后先播放 Walking，默认每五秒在 Walking → Running → Jump → Dance → Wave → Idle 中切换；切换使用 0.6 秒权重混合。

打开 115200 串口监视器，输入命令后按 Enter：

| 命令 | 行为 |
| --- | --- |
| `m` 或 `?` | 列出模型实际包含的全部动作及序号、秒数 |
| `0`–`13` | 选择对应动作，同时关闭自动播放列表 |
| `p` | 暂停/继续动作与混合过程 |
| `r` | 当前动作重新开始，重置混合权重 |
| `l` | 当前动作在循环和播放一次后保持末帧之间切换 |
| `c` | 后续切换在 0.6 秒交叉混合和立即切换之间切换 |
| `a` | 开关自动播放列表 |
| `o` | 开关展示用整体慢速旋转 |
| `+` / `-` | 调节动作速度，范围 0.25–2 倍 |
| `s` | 打印当前动作、权重及上一帧渲染统计 |

14 个动作按资源原有顺序保留：Dance、Death、Idle、Jump、No、Punch、Running、Sitting、Standing、ThumbsUp、Walking、WalkJump、Wave、Yes。Walking / Running / Idle / Dance 默认循环；其他动作默认一次并保持末帧。显示界面的菜单以实际资源为准。

同一时刻可以保留多个动作的权重，从当前混合状态渐变到目标动作；切换过程中再选另一个动作也从当前权重继续。当前 mixer 对同一属性累积动作权重，总权重不足 1 的部分由注册动作时的基准姿态补足，超过 1 时归一化；四元数先统一半球，再归一化加权结果。它不是物理过渡、IK 或完整的角色动画状态机。

源动作覆盖的节点/属性集合不完全相同。示例在每帧混合前恢复保存的 rest pose，再应用当前动作，避免已经退出的动作把新动作没有涉及的属性留在旧姿态。Model、节点、clip、轨道和 keyframe 存储都贯穿程序生命周期；所有 `clipAction()` 注册和数组分配在 setup 中完成。loop 复用模型、renderer、蒙皮 palette 和固定命令缓冲，不重载 GLB、不重建动作。

时间步最大为 0.1 秒，长时间阻塞后不会一次跃过很大动作区间；因此非常低帧率或串口阻塞时可能出现动作时间变慢。每帧结束仍执行 `delay(1)`，复杂画面超过 16 ms 时也为其他任务让出时间；临时 `Status::Busy` 留待下一帧重试。自动相机基于初始世界空间/蒙皮后的包围盒并增加跳跃余量，不是逐动作重新裁剪相机。

## 真实资源与适配边界

原始来源固定在 three.js commit `b924f0cad4058dc4dde71445c796980c3cd5b5ed`。模型作者是 Tomás Laulhé / Quaternius，Don McCurdy 做了 FBX 转换和表情等修改，原 README 标注 **CC0 1.0**。原始 GLB、原 README、来源/哈希锁和归属说明在 `../assets/upstream/RobotExpressive/`。单独复制本示例目录时，请一起保留 [ASSET_LICENSE.md](ASSET_LICENSE.md)。

原 GLB 的 SHA-256：

```text
047f5e5fb3bb6d378bd1df16ca6137f2a596c99b3a1b5690b4020c05aaf6f319
```

本库当前不处理 facial morph。准备脚本只删除 Head 的三个表情 morph 在三个 primitive 上的声明（共九项）、mesh/node 默认 morph weights，以及每个动作里一条 weights 通道（共十四条）。**全部十四个身体动作、所有原始 TRS 关键帧、74 个节点、两套各 43 关节的 skin 和 inverse bind matrices 均保留**。身体多个零件使用刚性节点层级运动，手部使用顶点骨骼蒙皮；不能把它描述成所有顶点都经过蒙皮。PBR metalness/roughness 使用简单光照近似，转换报告会记录该提示。

转换后是 7,214 个顶点、3,237 个三角形；NG3D 文件为 450,580 字节。最大单 primitive 为 1,344 个顶点，示例按真实节点/primitive 数配置 98 个对象容量，不沿用默认 128 或按全部顶点一次分配 raster scratch。详细 clip 时长、删除通道计数、输入输出 SHA-256 见 `../assets/RobotExpressive.metadata.json`。

## 重新制作

在 `nice_gfx_3d` 目录运行：

```powershell
node examples/tools/prepare-robotexpressive.mjs
```

这会读取已经保存的官方原文件，按固定 SHA-256 核对来源，再生成：

- `examples/assets/RobotExpressive.body.glb`：移除表情 morph 的 glTF 2.0/GLB，可用于 Studio 导入。
- `examples/assets/RobotExpressive.ng3d`：设备端原生资产。
- `examples/assets/RobotExpressive.metadata.json`：完整适配和来源报告。
- 本目录的 `RobotExpressiveData.h`：独立 Arduino sketch 使用的数据。

需要重新下载原文件时，加 `--download`。脚本固定官方 commit，不跟随 `dev` 分支悄悄更换素材。这个命令制作资产，不编译固件、不启动显示或执行渲染测试。

可用桌面 C++ 的 Model / AnimationMixer / Renderer 加载 NG3D，抽取 Dance 与 Jump 的多个时间点输出画面，生成命令见[桌面检查说明](../README.md#重现资源与桌面采样)。输出目录中的 `index.html` 和 `load-report.json` 用于检查抽样动作；这些本地生成产物不随源码保存，抽样也不能覆盖十四个动作的每个时间点。

资产元数据中的 `preparationStatus` 描述素材准备脚本的处理范围。

# 示例导航

建议先运行下面的真实模型播放器，再按需要查阅相机、材质和场景 API。
这些不是截图/GIF 播放程序：设备读取网格、纹理、骨骼和关键帧，逐帧实时渲染。
模型和适配后的资源已经附带，首次运行不要求联网或自行建模。

## 优先体验：真实动画模型

| 示例 | 实际内容 | 重点学习 |
| --- | --- | --- |
| [ModelFromFlash](ModelFromFlash/README.md) | Fox，576 三角形、24 关节，Survey / Walk / Run 三个动作 | Flash 资源加载、动作切换、自动播放、暂停和倍速 |
| [ModelFromLittleFS](ModelFromLittleFS/README.md) | CesiumMan，4672 三角形、19 关节的人形骨骼动作 | LittleFS、PSRAM 输入缓冲、短读与错误处理、释放加载输入 |
| [RobotExpressivePlayer](RobotExpressivePlayer/README.md) | RobotExpressive，3237 三角形、7214 顶点、19 primitives、两套 skin，14 个身体动作 | 真实角色播放列表、跳跃/舞蹈/行走、交叉混合、暂停、重启、动作菜单 |
| [esp-idf-model-player](esp-idf-model-player/README.md) | 同一真实 Fox 资产的完整原生 IDF 工程 | 按资源计算容量、自动取景、串口相机/动作控制、帧耗时日志 |
| [StudioModelWidget](StudioModelWidget/README.md) | 在已有 Studio 页面插入真实 RobotExpressive | GLB 导入、clip 选择、相机距离、内部分辨率和工程导出 |

Fox 与 CesiumMan 来自 Khronos glTF Sample Assets；RobotExpressive 来自
Three.js 官方示例。固定提交、原文件、SHA256、版权许可、材质/纹理改动均保存在
[assets](assets/README.md)，可复制的模型示例目录也保留了独立署名文件。

### 桌面加载预览（不是板端截图）

使用[重现资源与桌面采样](#重现资源与桌面采样)中的命令，可由本库的
**桌面原生 C++ Model / AnimationMixer / Renderer** 从附带的 NG3D 资源生成
320×240 动画采样帧。输出目录中的 `index.html` 展示 Fox、CesiumMan 与 Robot 的
不同动作时间点，`load-report.json` 记录加载/渲染结果、像素数量与资源哈希。
预览为本地生成产物，不随源码保存，也不代表设备采用图片序列播放。

## API 专题参考

| 示例 | 解决的问题 |
| --- | --- |
| [RotatingCube](RotatingCube/README.md) | 最小 setup → render → present，以及内部画面与屏幕分辨率的区别 |
| [SceneHierarchy](SceneHierarchy/README.md) | Group 局部/世界变换、装配层级、多个 Mesh 共享几何 |
| [CameraViews](CameraViews/README.md) | 透视和正交投影，串口切换产品展示/检视相机 |
| [MaterialsAndLights](MaterialsAndLights/README.md) | 不受光材质与 Lambert、环境光、方向光的区别 |
| [TexturedMaterials](TexturedMaterials/README.md) | RGB565 纹理、Alpha8 镂空、alphaTest 和纹理生命周期 |
| [KeyframeAnimation](KeyframeAnimation/README.md) | Track / Clip / Action / Mixer、重复/单次/往返播放 |
| [esp-idf](esp-idf/README.md) | 只有内建几何的最小 IDF 工程，排查模型以外的接入问题 |

基础几何只用于讲解 API 或排查显示接入，真实角色效果请看前面的播放器。

## 运行前的共同设置

1. 独立 Arduino 示例安装 NICE_GFX 与 NICE_GFX_3D 两个库，启用目标板 PSRAM。
2. 每个 Arduino 示例都有自己的 `Hardware.h`，可单独复制文件夹。
   默认 P4 是 **JC1060P470C_I_W_Y**，S31 是 **ESP32-S31-Korvo-1**。
   MCU 相同不表示屏幕引脚相同，其他板卡要替换显示预设。
3. 串口通常为 115200。Robot 支持两位数动作编号，需要发送换行；具体按键看各例 README。
4. LittleFS 示例需要额外上传其 `data/` 文件夹，程序不会为挂载失败自动格式化已有数据。
5. 默认内部尺寸 320×240，独占屏幕的例子通过 `presenter.presentFit(renderer.frame())`
   保持宽高比放大，每帧真实覆盖整屏并把全部留边填黑。可传第二个 RGB565 参数改变背景色。
   需要保留外围 UI 时使用 `present(surface, viewport)`，由 UI 统一合成的 Studio 不用整屏 API。
   尺寸、可见三角形、贴图和骨骼数量共同决定开销；日志中的实际设备结果比目标调度频率有意义。

触摸接线、轮询和 UI 共用输入见 [Interaction.md](../docs/Interaction.md)。

需要完整 UI 工程时可导入
[NOVA / MOTION LAB](../../nice_gfx_studio/projects/nova-motion-lab-3d/README.md)。

## 重现资源与桌面采样

已附带转换结果，普通使用不需要运行以下制作工具。

```sh
# 从固定上游版本重新准备 Fox / CesiumMan；只处理资产
node examples/assets/prepare-khronos-models.mjs
node examples/tools/prepare-robotexpressive.mjs

```

Windows PowerShell 下把桌面工具、两种 Zig 缓存和采样输出放在 D:/temp：

```powershell
$env:NICE_GFX_ARTIFACT_ROOT='D:/temp/nice_gfx_artifacts'
$env:ZIG_GLOBAL_CACHE_DIR="$env:NICE_GFX_ARTIFACT_ROOT/zig/global"
$env:ZIG_LOCAL_CACHE_DIR="$env:NICE_GFX_ARTIFACT_ROOT/zig/examples"
$desktop="$env:NICE_GFX_ARTIFACT_ROOT/workspace/nice_gfx_3d/examples"
New-Item -ItemType Directory -Force -Path "$desktop/.cache" | Out-Null
zig c++ -std=c++17 -O2 -I src examples/tools/render-model.cpp src/Model.cpp src/Renderer.cpp src/Memory.cpp -o "$desktop/.cache/render-model.exe"
node examples/tools/render-examples.mjs --renderer "$desktop/.cache/render-model.exe" --out "$desktop/previews"
```

可把 `zig c++` 换为普通 `c++`。`--out` 覆盖预览输出目录；未指定时使用
`NICE_GFX_ARTIFACT_ROOT` 下的 `workspace/nice_gfx_3d/examples/previews`，环境变量默认值为
`D:/temp/nice_gfx_artifacts`。输入资产和源码仍从库目录读取。制作 Khronos 的纹理需要
Windows System.Drawing 或 Python/Pillow，具体见资产制作脚本；这不是设备依赖。

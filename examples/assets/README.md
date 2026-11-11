# 真实动画资产

这些资源是官方上游的真实 glTF 模型，包含原始与适配版本。设备示例不会播放
预渲染图像。源文件、授权、改动与 SHA256 可追溯。

| 模型 | 原始来源 | 固定版本 | 运行资源 |
| --- | --- | --- | --- |
| Fox | [Khronos 官方资产](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Fox) | `8f492390ddc7a2ae221a1369ef560589cf8c70fb` | `Fox.ng3d`，299624 字节 |
| CesiumMan | [Khronos 官方资产](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/CesiumMan) | 同上 | `CesiumMan.ng3d`，435740 字节 |
| RobotExpressive | [Three.js 官方模型](https://github.com/mrdoob/three.js/tree/dev/examples/models/gltf/RobotExpressive) | `b924f0cad4058dc4dde71445c796980c3cd5b5ed` | `RobotExpressive.ng3d`，450580 字节 |

## 版权与改动

- Fox：PixelMannen 的网格为 CC0，rig/动画及 glTF 转换保留 tomkranis、
  AsoboStudio、scurest 的 CC BY 4.0 署名。准确原文见 `upstream/Fox/`。
- CesiumMan：Cesium，CC BY 4.0，原有商标说明保留在 `upstream/CesiumMan/`。
- RobotExpressive：Tomás Laulhé / Quaternius，Don McCurdy 修改，CC0 1.0。
  官方 README 与原 GLB 保留在 `upstream/RobotExpressive/`。

Fox / CesiumMan 将纹理最长边缩至 256 像素，并转换为支持的 PNG/RGB565。
所有原始骨骼动画通道和权重保留；缺失 NORMAL 时按平面法线拆分顶点。

RobotExpressive 保留全部 **14 个身体动作**：Dance、Death、Idle、Jump、No、
Punch、Running、Sitting、Standing、ThumbsUp、Walking、WalkJump、Wave、Yes。
当前库不支持 morph，因此去除头部表情的 morph target 和 14 条 weights 动画
通道；身体网格、skin、关节与 TRS 关键帧保留。不能把这个适配版描述为完整保留
上游所有脸部表情。逐项改动见 `RobotExpressive.metadata.json`。

## 文件用途

- `upstream/`：固定版本原件和许可资料。
- `*.metadata.json` / `Khronos.provenance.json`：哈希、数量、clip 与适配记录。
- `Fox.gltf` / `CesiumMan.gltf`：自包含 data URI，可直接在 Studio 导入。
- `RobotExpressive.body.glb`：保留身体动画的 Studio 可导入版本。
- `*.ng3d`：设备运行资源，可放 Flash、LittleFS 或自己的存储。
- Flash 示例使用的嵌入数组直接保存在 `../ModelFromFlash/FoxModel.h`；LittleFS 示例使用 `../ModelFromLittleFS/data/CesiumMan.ng3d`。
- `prepared/`：制作过程中的纹理文件。

上游原 GLB 中某些特性不属于本库首版子集。Studio 导入时应选上面的适配版本；
同时保留原 GLB 是为了来源和重现，不代表原件能未经适配直接导入。

可用实际 C++ Model / AnimationMixer / Renderer 加载三份 NG3D 并输出动画
采样图与加载报告，生成命令见[桌面检查说明](../README.md#重现资源与桌面采样)。这些本地生成产物不随源码保存，也不是 ESP32 板端测试。
触摸接入说明见 [Interaction.md](../../docs/Interaction.md)。

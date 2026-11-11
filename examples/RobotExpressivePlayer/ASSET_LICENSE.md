# RobotExpressive 模型归属与许可

本示例的 `RobotExpressiveData.h` 包含 **RobotExpressive** 模型的派生 NG3D 数据。即使只复制本代码文件夹，也请保留本说明，使模型来源和软件代码的许可可以分别追溯。

- 模型作者：**Tomás Laulhé / Quaternius**，<https://www.patreon.com/quaternius>。
- 上游修改：**Don McCurdy**，<https://donmccurdy.com/>。原 README 列出的修改包括添加三个面部表情 morph targets、使用 FBX2glTF 转换、合并重复材质与降低金属度。
- 上游模型许可：**CC0 1.0**，<https://creativecommons.org/publicdomain/zero/1.0/>。
- 官方来源：<https://github.com/mrdoob/three.js/tree/dev/examples/models/gltf/RobotExpressive>。
- 固定来源 commit：`b924f0cad4058dc4dde71445c796980c3cd5b5ed`。
- 原 README：<https://github.com/mrdoob/three.js/blob/b924f0cad4058dc4dde71445c796980c3cd5b5ed/examples/models/gltf/RobotExpressive/README.md>。
- 原 GLB：<https://github.com/mrdoob/three.js/blob/b924f0cad4058dc4dde71445c796980c3cd5b5ed/examples/models/gltf/RobotExpressive/RobotExpressive.glb>。

原 GLB SHA-256：

```text
047f5e5fb3bb6d378bd1df16ca6137f2a596c99b3a1b5690b4020c05aaf6f319
```

本示例适配删除了不受当前运行库支持的面部 morph 声明、默认 morph weights 和 14 条针对表情权重的动画通道。保留全部 14 个身体动作、原始平移/旋转/缩放关键帧、节点层级、skin 关节与 inverse bind matrices。PBR 金属度/粗糙度由简单实时光照近似。

模型原文件、原 README 和逐动作转换报告保存在库的 `examples/assets/`；派生资产不是未经修改的上游原文件。模型的 CC0 归属与 NICE_GFX_3D 软件库自身许可相互独立。

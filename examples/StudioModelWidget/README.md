# Studio 中插入真实 3D 动画

这是软件功能示例，不是新设计的 UI 项目。复用已有 ESP32-P4 / ESP32-S31 工程即可，不需要图片生成、截图或预渲染帧。

1. 在已有 Studio 工程中选择实际 P4 / S31 硬件。
2. 从组件面板插入“3D 模型 / 动画”，或把本目录 `widget.html` 复制到现有页面源码。
3. 选择组件，在右侧“3D 模型与动画”中导入 `../assets/RobotExpressive.body.glb`。这是真实 RobotExpressive 的兼容版本，保留十四个身体动作；原 GLB 仍在 `../assets/upstream/RobotExpressive/`，其表情 morph 尚不受当前导入器支持。
4. 导入器会绑定项目中实际生成的资产 ID。不要照抄其他项目的 `data-model3d` ID；片段特意没有引用不存在的资产。
5. 从动画片段下拉框选择 Walking、Running、Jump、Dance、Wave 等，进入播放模式查看。静态编辑状态显示初始姿态；播放模式实时计算节点/骨骼动画。
6. 根据屏幕布局调整组件显示尺寸。内部 320×240 与 CSS 显示尺寸独立；若动作超出自动取景范围，调整相机距离。
7. 保存项目或完整导出 Arduino / ESP-IDF 工程。现有 Studio 接入会随完整导出附带 3D 核心源码和生成的 `Model3DRuntime.h`，不生成 GIF。

资源来源、CC0 归属、表情通道删除记录和全部 clip 列表见 [RobotExpressivePlayer](../RobotExpressivePlayer/README.md)。若本地还没有兼容 GLB，在库根目录运行 `node examples/tools/prepare-robotexpressive.mjs` 生成，不能把原始含 morph 的 GLB 改扩展名假装已经兼容。

浏览器与 native 的过滤、光照和像素取整可以不同，浏览器播放不是板端性能测试。

含 3D 组件的 UI 应沿用 Studio 导出工程生成的任务栈配置。
相机控制及共享触摸输入的接法见 [Interaction.md](../../docs/Interaction.md)。

需要完整可导入的 UI 工程时见
[NOVA / MOTION LAB](../../../nice_gfx_studio/projects/nova-motion-lab-3d/README.md)。

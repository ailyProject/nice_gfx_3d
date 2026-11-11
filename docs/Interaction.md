# 触摸旋转与缩放

`OrbitControls` 管理相机，`NiceGfxOrbitInput` 负责读取触摸控制器。前者不依赖
Arduino、I2C 或 NICE_GFX，更新过程不分配内存。当前提供 Y 轴朝上的绕点旋转与缩放，
没有惯性或平移手势。

## 接入已有渲染工程

```cpp
#include <NICE_GFX_3D.h>
#include <NiceGfxOrbitInput.h>  // 触摸适配器需要单独包含
using namespace nice_gfx_3d;

PerspectiveCamera camera(45, 4.0f / 3.0f, 0.1f, 100);
OrbitControls controls(camera);
NiceGfxOrbitInput input;

void setupInteraction(uint16_t displayWidth, uint16_t displayHeight) {
  camera.position.set(0, 0, 5);
  controls.target.set(0, 0, 0);
  controls.minDistance = 1.2f;
  controls.maxDistance = 20;
  controls.setViewport(displayWidth, displayHeight);
  controls.update();
  controls.saveState();
  const bool touchReady = input.begin(displayWidth, displayHeight);
  Serial.printf("触摸=%s, 状态=%u\n",
                touchReady ? "就绪" : "不可用",
                unsigned(input.status()));
}
```

在屏幕初始化后调用 `setupInteraction(screen.width(), screen.height())`。`target` 是
**世界坐标**，应设为整个模型或场景的中心；模型节点的局部坐标不能直接代替它。
相机有父节点时也使用世界空间绕点旋转，父变换必须可逆。缩放范围要结合模型尺寸与
相机 `near/far` 设置，避免靠近时进入模型、远离时超过远裁剪面。

触摸坐标和视口使用**显示区域的像素尺寸**。例如屏幕为 800×480、内部渲染为
320×240，全屏触摸仍传 800×480。相机宽高比另按实际呈现区域配置。

轮询放在帧门限之前；下面沿用工程已有的 `scene`、`renderer`、`presenter`：

```cpp
void loop() {
  const uint32_t now = millis();
  input.update(controls, now);
  while (Serial.available())
    if (Serial.read() == 'h') controls.reset();

  static uint32_t previous = 0;
  if (uint32_t(now - previous) < 16) { delay(1); return; }
  previous = now;

  // 在这里推进动画；不要再用自动相机轨迹覆盖 controls 设置的位置。
  if (renderer.render(scene, camera) == Status::Ok)
    presenter.presentFit(renderer.frame());
  delay(1);
}
```

触摸轮询目标间隔为 5–8 毫秒，适配器默认 8 毫秒，并在 `update()` 内限频。每次主循环都可
调用它，不必等到下一次渲染。单线程正在执行较慢的渲染时不会额外轮询，实际触摸采样
间隔仍会受到帧耗时影响。16 毫秒门限控制渲染调度频率，实际帧率还取决于每帧耗时。

## 手势与相机接口

| 操作 | 行为 |
| --- | --- |
| 单指拖动 | 保持到 `target` 的距离，绕中心旋转；默认横向拖满视口宽度为一周 |
| 双指张开 / 收拢 | 透视相机改变距离；默认距离乘以前后指距之比，100→150 像素对应 2/3 |
| 正交双指缩放 | 改变 `left/right/top/bottom`，保持视框中心与相机位置，并更新投影矩阵 |
| `saveState()` / `reset()` | 保存 / 恢复相机位置、目标点及正交视框；`h` 复位由应用自行绑定 |
| `cancel()` | 清除当前手势基线，不恢复相机 |
| `update()` | 外部修改相机或目标点后，重新朝向中心并限制距离，返回是否成功 |

`rotateSpeed` 和 `zoomSpeed` 默认为 1，只影响触摸输入。程序控制使用弧度和直接比例：

```cpp
controls.rotate(0.2f, 0.0f);  // 水平转 0.2 弧度
controls.dolly(0.9f);        // 透视距离或正交视框缩小到 90%
controls.target.set(1, 0, 0);
if (controls.update()) controls.saveState();  // 把新的取景保存为复位视角
```

直接接入其他输入源时，每个完整采样只调用一次 `setContacts()`，最多传两个点：

```cpp
OrbitContact points[2] = {{7, 200, 180}, {12, 340, 180}};
controls.setContacts(points, 2);
controls.setContacts(nullptr, 0);  // 抬起也必须上报
```

两点以稳定的触点标识匹配，报告顺序交换不会反转缩放。手指数或触点标识改变时重新建立基线，
避免单指/双指切换跳变。同尺寸 `setViewport()` 保留手势；尺寸改变会取消旧基线。
非法坐标、重复触点标识、超过两点及非有限输入会取消当前手势；非法相机/距离参数会被拒绝。
业务代码仍应避免写入非数值（`NaN`），并检查 `update/rotate/dolly` 的返回值。

## 默认触摸配置与自定义总线

| 板卡 | 控制器 | SDA / SCL | I2C 地址 | 原始触摸尺寸 |
| --- | --- | --- | --- | --- |
| ESP32-S31 Korvo-1 | GT1151 | GPIO 0 / 1 | `0x14` | 800×480 |
| ESP32-P4 JC1060 | GT911 | GPIO 7 / 8 | `0x5d` | 1024×600 |

以上是当前代码的板卡默认值。其他面板需按实际接线与坐标范围配置。
默认总线频率为 400 千赫，`rotation` 为顺时针 0–3 个四分之一圈。

```cpp
OrbitTouchConfig makeTouchConfig() {
  OrbitTouchConfig cfg;
  cfg.controller = OrbitTouchController::GT1151;
  cfg.sda = 0; cfg.scl = 1;
  cfg.address = 0x14;
  cfg.rawWidth = 800; cfg.rawHeight = 480;
  cfg.pollIntervalMs = 5;
  cfg.rotation = 0;
  return cfg;
}
NiceGfxOrbitInput customInput(makeTouchConfig());  // ESP32 自带板级 I2C 实现
```

已有长期存活的 `nice_gfx::I2cBus` 实现 `existingBus` 时，可构造
`NiceGfxOrbitInput customInput(existingBus, makeTouchConfig());`。适配器借用该总线，
不接管其所有权。`I2cBus` 的接口是 `begin(address)` 和
`transfer(address, tx, txSize, rx, rxSize, stop)`；总线实现负责自己的引脚和时钟配置。
GT1151 当前只接受 `0x14`，GT911 接受 `0x14` 或 `0x5d`。

适配器读取并确认完整报告后，选取标识最小的两个触点传给 `OrbitControls`；更多手指不会
直接传入 `setContacts()`。GT911 没有新报告时保留当前手势，收到空报告立即释放；
GT1151 的空报告采用 `releaseGraceMs`（默认 30 毫秒）释放宽限。宽限内重新触摸会重建
位置基线，避免复用触点标识造成相机跳变。

`begin()` 探测失败可以继续显示和渲染。轮询失败会增加 `errors()`、取消手势，并把重试
间隔退让到 500 毫秒；它不会要求应用停止渲染。可通过 `ready()`、`status()`、`polls()`、
`errors()`、`contactFrames()`、`lastContactCount()` 查看输入状态。`status()` 返回的是
`nice_gfx::Status`，与渲染器使用的 `nice_gfx_3d::Status` 属于不同枚举。

## 和原生界面共用触摸

`NiceGfxOrbitInput` 默认占用全屏触摸报告。**同一控制器只能有一个读取并确认完整报告的
所有者**；总线对象共享不等于触摸报告已共享。不要让界面单点适配器和轨道控制多点适配器
各自轮询，否则一个读取者的确认操作可能抢走另一个需要的报告。

有按钮和三维舞台的页面，应读取一次完整采样，再按手势起点分发给界面或轨道控制，并保持
归属直到抬起。舞台输入先减去舞台左上角，视口使用舞台尺寸；界面则接收屏幕坐标。

可参考 NOVA 示例的项目私有
[`SharedTouchAdapter`](../../nice_gfx_studio/projects/nova-motion-lab-3d/firmware/SharedTouchAdapter.h)：
`nova::SharedTouchAdapter::begin()` 初始化，
`update(view, &controls, stageRect, now)` 统一读取并路由，
`cancel(view, &controls)` 同时取消界面/相机手势。它按项目硬件配置选择 S31/GT1151 或
P4/GT911，使用项目的引脚、触摸校准与屏幕方向；该项目私有接口可供自有工程参考。
普通 Studio 导出已统一路由界面与模型的触摸，见 [Studio 性能与生命周期](StudioPerformance.md)。
使用共用触摸桥接的页面不再同时轮询原 `TouchAdapter` 或 `NiceGfxOrbitInput`。

# 静态几何的重复计算复用

模型的顶点属性保持固定时，可在初始化阶段显式建立复用表。骨骼、节点、相机、灯光和
材质仍可逐帧变化。每个网格都会先计算本帧首次出现的位置与光照，再给相同输入的后续
顶点复用结果。各顶点原有的纹理坐标、硬边法线和三角形索引继续保留，不简化模型。

大型模型应先创建渲染器、预留工作区，再尝试可选缓存。以下代码放在加载成功且
`RendererOptions` 容量已经按实际图元/场景对象配置的初始化流程中：

```cpp
using namespace nice_gfx_3d;
Status status = model.load(bytes, length);
if (status != Status::Ok) return status;
options.colorMemory = MemoryPreference::ExternalFirst;
options.depthMemory = MemoryPreference::ExternalFirst;
options.scratchMemory = MemoryPreference::InternalFirst;
status = renderer.begin(options);
if (status != Status::Ok) return status;
const Status staticCache = model.optimizeStaticGeometry();
const Status textureCache = model.optimizeTextures();
// 分开记录这两个结果。NoMemory/CapacityExceeded 可继续采用普通渲染路径；
// InvalidData 等输入错误仍应处理。模型是否可显示由加载和渲染的结果决定。
```

`optimizeStaticGeometry()` 返回 `nice_gfx_3d::Status`。初始化工作按每个图元
分别建立位置表与光照表，比较浮点位模式、关节索引和权重。位置表匹配位置与蒙皮输入，
光照表匹配法线、顶点颜色与蒙皮输入。节点动画改变变换后，本帧结果仍会重新计算。

启用后，顶点的位置（`position`）、法线（`normal`）、颜色（`color`）、纹理坐标（`uv`），
以及蒙皮关节索引（`joints`）和权重（`weights`）数组应保持不变。
需要编辑这些数组时，先关闭复用，再在编辑完成后重新建立表。

```cpp
model.clearStaticGeometryOptimization();
// 在渲染调用之外编辑顶点或蒙皮数组。
// 编辑完成并保证数组后续保持固定后，可再次调用 optimizeStaticGeometry()。
```

`Model::load()` 成功后已把输入资源复制为模型拥有的数据，可以释放输入文件缓冲；缓存借用的是
模型拥有的几何数据，不是原文件。`ModelView` 自身仍是借用读取器，源字节须在读取
及使用该视图加载期间有效。

普通缓存的关闭操作会释放其分配；使用 `ModelCacheWorkspace` 时则归还借用，缓冲仍由工作区保留。
成功的 `load()`、`clear()` 和模型析构也会释放或归还相关存储。替换几何对象的
`vertices` 指针、`skinVertices` 指针或 `vertexCount` 时，渲染器自动采用动态计算。
原指针上的内容编辑需要显式关闭复用。复制的 `BufferGeometry` 作为普通借用视图工作，
不继承私有优化或闭合表面元数据，也不延长原数组寿命。

若把一个普通副本赋回原模型的几何对象，私有优化绑定同样会清空。保持原顶点、蒙皮
指针和顶点数量不变时，重新调用 `model.optimizeStaticGeometry()` 成功后会恢复该模型
拥有的优化绑定。
这不把缓存所有权转给外部副本，也不允许在原数组内容已改变后跳过清理步骤。
使用工作区时应继续传同一个工作区重绑；无参接口不负责匹配或借用工作区槽。

每次调用应安排在没有正在执行 `render()` 时，且不能与渲染或模型销毁并发。调用失败时保留原有复用表，
因此重新建立表时须遵守原有不可变约定。普通 `Model::load()` 和手工创建的几何对象
使用动态路径，只有显式调用该接口才会启用复用。

设所有图元顶点总数为 `N`，当前持久基础块为 `16 × N` 字节：两张 32 位无符号整数
复用表占 `8N`，原纹理坐标缓存占 `8N`，另加每个图元的描述符。基础块优先内部内存，
空间不足可回退 PSRAM；初始化哈希表在 ESP32 上使用 PSRAM，完成后释放。
分配失败返回 `NoMemory` 并保留原缓存。已有内部原始属性缓存时，替换基础块要求内部
分配成功，避免一次重建把原有热数据挤到 PSRAM。

内部内存充足且单个图元顶点数不超过 65,535 时，还可选缓存原始代表属性：
`12 × P + 16 × L + 2 × Np` 字节，其中 `P/L` 分别是位置、法线/颜色代表数量，
`Np` 为该图元的顶点数。三个块按从大到小顺序分配且必须全部位于内部内存；
失败只放弃该图元的属性缓存，基础复用表仍可用。原色材质（`MaterialType::Basic`）的保守快捷路径可利用
其中的常量顶点色与刚性关节信息，不改变同一材质模式的输出。
`renderer.stats().basicConstantMeshes` 可确认本帧有多少网格使用了该原色材质快捷路径。

| 接口 | 含义 |
| --- | --- |
| `staticGeometryOptimizationBytes()` | 基础块、复用描述符及已启用原始属性块的合计。 |
| `staticVertexAttributeCacheBytes()` | 上项已包含的原始属性字节，不能重复相加。 |
| `staticVertexAttributeCacheMeshes()` | 成功获得原始属性缓存的图元数量。 |
| `staticVertexAttributeInternalBytes()` | ESP32 上确认位于内部内存的原始属性字节；主机实现返回 0。 |

这些计数不包含整个模型、渲染器工作区、所有权容器或初始化临时哈希表的峰值。
清理静态优化后相关缓存计数归零。渲染继续使用渲染器已有的变换顶点工作区。
内部内存也包含可作为堆使用的实时时钟（RTC）/低功耗（LP）内存；`InternalOnly` 或内部字节计数
不保证块位于主高性能域内存（HP DRAM），更不能单凭该计数推断帧率。

## 跨重载保留缓存

`ModelCacheWorkspace` 是显式的缓存缓冲拥有者，适合反复加载同一资产或匹配缓存布局的资产。
它不保存场景、模型原始数组或动画对象，也不改变 NG3D、顶点、纹理格式或材质。
不传工作区的 `optimizeStaticGeometry()` / `optimizeTextures()` 创建由模型拥有的缓存，
不会自动建立跨模型的全局池。
重载仍会构造模型原数据与描述符，带工作区的优化也会重新写入缓存、建立临时表；
复用载荷地址不代表整个加载/初始化过程零分配或零成本。

最少改动的接入方式是先按原流程成功建立缓存，再接管已有缓冲。将工作区作为应用成员
或持久变量；下面的 `model` 和 `renderer` 是应用已有对象：

```cpp
using namespace nice_gfx_3d;
ModelCacheWorkspace cache; // 保留到多次模型重载之后

// 已检查 model.load(...) 和 renderer.begin(...) 成功；此时没有渲染在运行。
const Status geometry = model.optimizeStaticGeometry();
const Status texture = model.optimizeTextures();
if (geometry == Status::Ok && texture == Status::Ok) {
  const Status retained = model.retainOptimizations(cache);
  // retained != Ok 时原缓存仍有效；单独记录结果，不隐藏已加载的模型。
}
```

`retainOptimizations()` 不重新分配、复制或移动缓存载荷，因此可以保留首次成功分配的地址。
它仍需创建工作区管理元数据，可能返回 `NoMemory`。目标工作区必须为空，缓存不能已经
属于任何工作区，否则返回 `Busy`；没有可接管的缓存返回 `InvalidArgument`。
已有缓存需要的类型块超过工作区 32 槽上限时返回 `CapacityExceeded`，原缓存保持有效。
若缓存对应的顶点/蒙皮指针、顶点数量，或纹理颜色指针、宽高、行跨度（`stride`）已被修改，接管会
返回 `InvalidData`，应先恢复原引用或清理后重建。原数组内容仍须遵守不可变合同，接管
不重新验证每个像素或顶点。

下一次重载先销毁旧动画混合器及动作对象，并等待渲染结束，然后使用带工作区的重载：

```cpp
model.clear(); // 归还租约；cache.reservedBytes() 仍保留，leasedBytes() 变为 0
Status loaded = model.load(bytes, length);
if (loaded == Status::Ok) {
  const Status geometry = model.optimizeStaticGeometry(cache);
  const Status texture = model.optimizeTextures(cache);
  // 也可从第一次建立缓存就使用这两个重载；检查各自结果。
}

// 停止使用时，确认旧动画混合器及动作对象已销毁且渲染空闲：
model.clear();
Status released = cache.end(); // 所有借用已归还时为 Ok；仍被模型使用时为 Busy
```

每个工作区最多保留 **32 个带类型的块**，静态和纹理共用此上限；按用途、图元/纹理索引、字节数、对齐和
内存偏好匹配；它不是按模型名称或文件摘要匹配。静态缓存组和纹理缓存组各自在首次成功
建立或接管后冻结，不能自动扩展为任意新布局。不同布局应使用另一个工作区，或先归还全部
借用、`end()` 清空后重建。同一工作区同时只能被一个模型借用，操作须由所有者串行执行。
已有槽要求精确匹配，不会因为旧块更大就接受较小请求；同尺寸但不同内容的贴图，也可能
因为紧凑载荷字节数变化而不匹配原布局。

必需的基础映射表或纹理载荷不匹配时可返回 `CapacityExceeded`，分配失败可返回 `NoMemory`；
已有缓存保留。原始属性块是可选的：槽不足或不匹配时，该图元可以退回普通属性读取，
整个优化仍返回 `Ok`。紧凑纹理元数据也可能退回原始分块，但必需载荷仍须匹配。
因此 `Ok` 不保证全部可选优化生效，应同时查看缓存字节、网格和本帧使用计数。
布局冻结后，先前缺失的可选槽也不会仅因后来空闲增加而自动补齐。
`Busy` 表示仍有冲突借用或需要先清理，不应当作普通内存不足静默忽略。

同一个模型已使用该工作区且源指针、数量或纹理尺寸仍匹配时，重复调用对应优化接口
会复用已有缓存，不重建载荷。此检查不比较数组内容，不能用它代替编辑前的清理。
模型重载后则需要重新建立当前数据的缓存内容。

`reservedBytes()/reservedBlocks()` 是工作区保留的载荷容量，
`leasedBytes()/leasedBlocks()` 是当前被模型借用的部分。它们不包含管理元数据或堆对齐开销；
模型的缓存字节计数已经包含其在用载荷，不要把两者相加当作两份内存。
只清理静态或纹理缓存会归还对应组；`Model::clear()` 归还全部租约，`end()` 在还有租约时
返回 `Busy`，不会强制释放。工作区句柄意外先析构时，存活的模型持有共享状态，缓存不会悬挂；
最后一个拥有者释放后才回收。为持续复用，仍应让工作区覆盖整个重载周期。
这些生命周期保证不允许与渲染、数组编辑或模型销毁并发。

`Renderer::stats().reusedPositions` 和 `reusedLighting` 统计当前帧实际复用次数，可用于
确认应用启用了该路径。是否提高设备帧率取决于几何、访存、光栅化和界面合成的实际耗时。

## 闭合表面的离线提示

`tools/closed-surface-hints.mjs` 为拓扑闭合、绕序一致的组件生成可选提示。组件必须未蒙皮，
或全部顶点刚性绑定到同一关节；每个图元最多生成 128 个组件。不合格的三角形
保留普通路径，模型顶点、索引、贴图与动画不会被删减。这些提示独立于 NG3D 文件和
静态顶点缓存，需要在模型加载成功后显式挂接。

离线脚本可以从将部署的 NG3D 生成头文件；下面的导入路径相对于仓库根目录：

```js
import { readFile, writeFile } from 'node:fs/promises';
import { decodeNg3d } from './tools/compile-model.mjs';
import { closedSurfaceHints, cppClosedSurfaceHints } from './tools/closed-surface-hints.mjs';

const model = decodeNg3d(await readFile('robot.ng3d'));
const hints = closedSurfaceHints(model, { opaqueOnly: true });
await writeFile('robot-hints.h', cppClosedSurfaceHints(hints, {
  namespace: 'robot_hints', include: false,
}));
```

```cpp
#include <NICE_GFX_3D.h>
#include "robot-hints.h"

// model.load(...) 已成功，且没有渲染在运行。
const Status hintsStatus = robot_hints::attach(model);
// 检查 hintsStatus；挂接失败时仍可使用普通渲染路径。
```

生成头文件的 `attach()` 调用 `Model::setClosedSurfaceHints(primitive, &hints)`。该接口
检查数量与引用等结构，不重新验证闭合拓扑；提示必须对应当前模型的精确顶点、索引和
蒙皮数据。提示对象、组件数组、三角形映射及几何数组在使用期间均须存活且保持不变。
编辑几何前应等待渲染结束并调用 `model.clearClosedSurfaceHints()`；它与
`clearStaticGeometryOptimization()` 分开管理。模型清空、成功重载或析构会解除提示绑定。

运行时仅对不透明、无透明度测试、没有贴图透明度通道的 `Side::Double` 材质尝试使用
提示。相机必须在组件包围球外，组件包围球须完整位于近裁剪面前；变换或蒙皮不满足
条件时逐组件退回普通双面渲染。`closedSurfaceComponentCount()` 统计已挂接组件数，
`renderer.stats().closedSurfaceComponents` 和 `closedSurfaceFallbacks` 分别统计本帧通过
运行时检查和回退的组件数。

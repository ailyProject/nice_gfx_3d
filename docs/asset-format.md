# NG3D 资源格式

`tools/compile-model.mjs` 将 glTF 2.0（`.gltf` 或 `.glb`）转换为小端序的
NG3D 版本 1 格式。设备端不解析 JSON，也不解码图像。
`ModelView` 借用完整的输入字节范围；使用视图期间，输入必须始终有效。
`Model::load` 校验资源并将其复制为自己拥有的场景数据，因此加载成功后可以释放输入。

所有偏移均为从文件起始位置计算的绝对字节偏移。所有区段和可变长度载荷均从
四字节对齐的位置开始。计数和偏移均为无符号 32 位整数；缺省的有符号索引为 `-1`，
缺省的蒙皮顶点范围为 `0xffffffff`。浮点数采用 IEEE-754 binary32 格式。
运行时读取器不会将文件记录直接转换为 C++ 结构体，也支持未对齐的输入地址。

## 文件头与目录

32 字节文件头的布局如下：字节 0 为 `NG3D`，字节 4 为 u16 版本号 `1`，
字节 6 为 u16 文件头大小 `32`，字节 8 为 u32 文件总字节数，字节 12 为 u32
目录项数量，字节 16 为 u32 目录偏移，字节 20、24、28 为三个必须置零的 u32 保留字段。

每个目录项占 16 字节，依次包含四字符 ASCII 标签、u32 记录数量、u32 记录步长和
u32 记录偏移。下表中的每个标签均恰好出现一次，空区段也必须保留目录项。
读取器拒绝未知区段、版本和标志位。所有载荷范围必须位于文件声明的长度以内。

| 标签 | 记录步长（字节） | 字段，按存储顺序排列 |
| --- | ---: | --- |
| VERT | 36 | 位置 f32[3]、法线 f32[3]、UV f32[2]、RGB565 u16、保留字段 u16=0 |
| VSKN | 24 | 局部关节索引 u16[4]、归一化权重 f32[4] |
| INDX | 4 | 相对于所属图元首顶点的 u32 索引 |
| MESH | 8 | 首图元索引、图元数量 |
| PRIM | 32 | 首顶点索引、顶点数量、首索引位置、索引数量、材质索引 i32、网格索引、首蒙皮顶点索引或 0xffffffff、保留字段=0 |
| NODE | 60 | 父节点索引 i32、网格索引 i32、蒙皮索引 i32、名称偏移、名称字节数、位置 f32[3]、四元数 xyzw f32[4]、缩放 f32[3] |
| MATL | 28 | 基础颜色 RGBA f32[4]、纹理索引 i32、标志位、透明度裁剪阈值 f32 |
| TEXR | 24 | 宽度、高度、RGB565 像素偏移、可选 alpha8 偏移（不透明时为 0）、采样器标志位、保留字段=0 |
| SKIN | 24 | 关节节点索引数组偏移、关节数量、逆绑定矩阵数组偏移、骨架根节点索引 i32、名称偏移、名称字节数 |
| ANIM | 16 | 首通道索引、通道数量、名称偏移、名称字节数 |
| CHAN | 24 | 节点索引、目标（0=平移、1=旋转、2=缩放）、插值（0=线性、1=阶跃）、关键帧数量、关键帧时间数组偏移、关键帧数值数组偏移 |
| ROOT | 4 | 活动 glTF 场景中的节点索引 |

名称采用 UTF-8 编码，声明的字节长度之后附加一个 NUL 字节，名称内部禁止出现 NUL。
矩阵数组采用列主序 f32[16]。旋转通道的数值包含四个分量，平移和缩放通道包含三个分量。
关键帧时间必须为有限、非负且严格递增的数值。每个通道至少包含一个关键帧；
动画时长取所有通道最后一个关键帧时间中的最大值。

蒙皮中的关节索引指向 `NODE`；顶点中的关节索引指向对应蒙皮的关节数组。
权重必须有限、非负且总和为一，每个顶点最多使用四个关节。
逆绑定矩阵必须为可逆仿射矩阵。活动蒙皮网格引用的所有关节必须属于活动场景，
以便渲染器在动画更新后更新全部骨骼的世界矩阵；不满足此限制的资源会被拒绝导入。

材质标志位：第 0 位表示双面，第 1 位表示透明度裁剪，第 2 位表示透明度混合。
裁剪和混合互斥。采样器标志位：第 0..1 位表示 S 方向环绕模式，第 2..3 位表示
T 方向环绕模式（`0` 为边缘夹取、`1` 为重复、`2` 为镜像重复）。其他位必须为零；
当前仅实现最近邻过滤。纹理按从上到下的顺序紧密排列各行，RGB565 数值采用小端序。
alpha8 通道单独存储，完全不透明时省略。

## 编译器接口与支持范围

```js
import { readFile } from 'node:fs/promises';
import { compileGltf, decodeNg3d } from './tools/compile-model.mjs';
const input = new URL('./model.gltf', import.meta.url);
const { binary, model, clips, warnings } = await compileGltf(new Uint8Array(await readFile(input)), {
  resolveUri: async (uri) => new Uint8Array(await readFile(new URL(uri, input))),
});
const previewModel = decodeNg3d(binary);
```

输入可以是 GLB 字节、glTF JSON 文本，或已经解析的 glTF 对象。
外部缓冲和 PNG 图像需要通过 `resolveUri` 读取；数据 URI 和 GLB BIN 数据块直接处理。
命令行工具相对于输入文件所在目录解析外部文件，不获取网络资源。

支持三角形网格；位置、法线、UV0、顶点 RGB 颜色；缺少法线时生成平面法线；
基础颜色因子和 PNG 基础颜色纹理；不透明、透明度裁剪和透明度混合材质声明；
节点平移/旋转/缩放（TRS）或不含剪切的仿射矩阵；四权重蒙皮和逆绑定矩阵；节点平移、旋转和缩放的
线性或阶跃动画；所选场景的根节点。
glTF 相机声明会被跳过并产生警告，需要自行配置运行时相机。

缺少 `NORMAL` 时，编译器会为每个三角形复制顶点以保留平面法线，并将 UV、
顶点颜色及全部四个蒙皮关节索引和权重重新映射到新顶点。
此时会报告 `missing normals flattened; may increase vertices`，表示缺失法线已按平面补齐，
顶点数量可能增加。这会增加资源顶点数及渲染器所需的 `maxVertices` 容量。
提供法线的图元保留原有的共享顶点和索引。

PNG 解码支持非交错、八位的灰度、RGB、索引色、灰度加透明度和 RGBA 图像，
包括 PNG 行过滤和调色板透明度。纹理被编译为 RGB565 及可选的 alpha8 通道。
桌面材质的金属度和粗糙度系数会产生材质近似提示：渲染器使用基础颜色和简单光照。
金属度/粗糙度、法线、遮蔽和自发光**纹理贴图**以及非零自发光因子会被拒绝。
不存储多级渐远纹理；对于线性或多级渐远采样声明，会明确提示其近似为基础层级的最近邻过滤。

glTF 扩展、稀疏访问器、变形目标/权重动画、三次样条（CUBICSPLINE）插值、每顶点超过四个关节的影响、
额外 UV 集、透明度小于一的顶点颜色、压缩几何/图像和非三角形图元均会被拒绝，
并返回具体错误。导入前需要重新导出或烘焙这些特性。
动画混合和渲染质量属于运行时能力，与资源容器格式相互独立。

```sh
node tools/compile-model.mjs model.glb -o model.ng3d
node tools/compile-model.mjs model.gltf -o model.ng3d --header model.h --symbol robot_model
```

可选的头文件声明一个 `alignas(4) inline constexpr uint8_t[]` 及字节长度常量，
运行时不依赖文件系统。资源应来自可信的制作输入；运行时读取器仍会在构造场景前
校验范围、计数、引用、有限数值、拓扑和关键帧数据，不会解引用文件提供的指针。

## 无损源数据存储

`tools/model-storage.mjs` 将编译后的 NG3D 字节打包用于存储。其描述信息独立于 NG3D
文件，包含 `bytes`、`codec`、`rawBytes`、`packedBytes`，以及原始 NG3D 字节的
32 字节 SHA-256 摘要 `digest`。描述信息必须与打包载荷一起保存；载荷本身不是
自描述的 NG3D 文件。

| 编解码方式 | 值 | 存储的载荷 |
| --- | ---: | --- |
| `Raw` | 0 | 原始 NG3D 字节 |
| `Zlib` | 1 | 经 zlib 压缩的 NG3D 字节 |
| `ChannelDeltaBytePlanesZlib` | 2 | 动画通道无损预测、字节平面重排，再进行 zlib 压缩 |

`packModelBytes()` 比较所有支持的候选方案，仅在结果更小时采用；压缩无收益时保留原始数据。
通道预测直接处理动画时间和数值的精确 32 位字，恢复后可逐字节还原原始数据，
不会减少动画关键帧或降低几何、纹理质量。源数据存储限制为 8 MiB 原始 NG3D 数据，
该限制独立于 `Model::load()` 使用的可配置 `ModelLimits`。

```js
import { createHash } from 'node:crypto';
import { deflateSync } from 'node:zlib';
import { packModelBytes } from './tools/model-storage.mjs';

// binary 是 compileGltf() 返回的 Uint8Array。
const packed = packModelBytes(binary, {
  compress: (bytes) => new Uint8Array(deflateSync(bytes)),
  digest: new Uint8Array(createHash('sha256').update(binary).digest()),
});
```

压缩函数必须同步执行并返回 `Uint8Array`，SHA-256 摘要由调用方提供。
浏览器集成需要注入自己的 zlib 实现和摘要。该模块不依赖文件系统或 Studio。

设备端需要在 `ModelAssetDescriptor` 中填写 `packed`、`packedBytes`、`rawBytes`、
`codec` 和 `digest`，然后调用 `ModelAssetBuffer::decode()`。所有编解码方式均在
提供字节数据前校验恢复后的 SHA-256。打包长度和原始长度分别最多为 8 MiB；
可选参数 `maximumBytes` 可进一步降低限制。解码失败时保留先前的数据。

```cpp
#include <NICE_GFX_3D.h>
using namespace nice_gfx_3d;

Status loadPackedModel(const ModelAssetDescriptor& asset, Model& model) {
  ModelAssetBuffer decoded;
  const Status status = decoded.decode(asset);
  if (status != Status::Ok) return status;
  // load() 复制 NG3D 数据后，decoded 会在函数结束时释放。
  return model.load(decoded.data(), decoded.size());
}
```

原始外部输入采用借用方式，必须在所有读取器使用期间保持有效。
在 ESP32 上，压缩输入恢复到由缓冲对象拥有的 PSRAM 中。编解码方式 1 需要分配
一块原始数据大小的临时内存；编解码方式 2 在还原字节平面重排时，短暂需要两块
原始数据大小的内存。预算还需计入先前保留的解码缓冲，以及 `Model::load()` 构建的
自有场景。加载结束且所有借用的 `ModelView` 读取器使用完毕后，可清理解码缓冲。
桌面端原始数据解码使用可移植的 SHA-256 实现；压缩数据解码需要链接 `miniz.h`
对应的实现，否则返回 `Status::Unsupported`。

## 运行时所有权与缓存

校验或场景构建失败时，`Model::load` 保留已有场景。源资源必须在整个调用期间保持有效，
从其他对象借用的源资源也遵循这一规则。加载成功后，自有场景、几何、纹理像素和
动画数据不再借用 NG3D 输入，因此可以释放文件缓冲。`ModelView` 本身始终是借用式
读取器；调用其任意访问器或后续的 `Model::load(view)` 时，输入都必须有效。

动画关键帧时间和数值在 ESP32 上显式优先使用 PSRAM，分配失败返回 `Status::NoMemory`。
几何和场景容器使用 C++ 分配器，实际内存位置取决于开发板的 ESP-IDF 堆配置。
应按应用的资源预算设置 `ModelLimits`。启用 C++ 异常时，分配失败会被捕获并返回
`Status::NoMemory`。使用 ESP-IDF 常见的 `-fno-exceptions` 配置时，STL 分配失败
遵循工具链的终止策略；`Status::NoMemory` 覆盖显式的不抛出异常的对象分配，
但无法从 STL 分配失败中恢复。以事务方式替换已加载模型时，需要同时为新旧场景预留内存。

运行时缓存布局不改变 NG3D 文件格式。调用可选的 `Model::optimizeStaticGeometry()`
和 `Model::optimizeTextures()` 前，先通过 `Renderer::begin()` 预留渲染临时工作区。
静态复用映射和缓存纹理坐标优先使用内部 RAM，建立缓存时的哈希表在 ESP32 上使用 PSRAM。
可选的原始属性缓存块仅使用内部 RAM。纹理分块使用 PSRAM 存储 RGB565 载荷，
可选元数据优先使用内部 RAM；纯色块和调色板块保留精确的源颜色。
这两类缓存均不会丢弃原始模型数据。

缓存启用期间，静态几何输入属性和纹理像素数据必须保持不变；节点/骨骼动画、相机和
灯光仍可动态变化。修改输入数组前应禁用对应缓存；清理、重建缓存或重新加载模型前，
应等待渲染结束。缓存建立失败时保留已加载场景和已有缓存；可选缓存返回 `NoMemory`
不必停止渲染，其状态应与加载、渲染状态分别保存。字节计数及复制、生命周期约束见
[静态几何](StaticGeometry.md) 和 [内存预算](design.md)。

`ModelCacheWorkspace` 可选地在运行时持有可复用的缓存缓冲，不改变 NG3D 格式，也不改变
Model 拥有的原始几何、纹理和动画数据，只保留派生优化缓冲的所有权。
未使用工作区时，派生缓存缓冲由 `Model` 拥有并释放。
接受工作区的重载在建立缓存时租用带类型的缓冲，并在缓存清理、模型成功重载或销毁时归还。
重载后仍需显式调用优化方法，将缓存绑定到新拥有的场景。

空工作区也可通过 `Model::retainOptimizations(workspace)` 接管已有的普通缓存，
无需移动或重建载荷，但仍需为管理信息分配内存。工作区槽位按精确的类型/索引、大小、
请求的内存偏好和对齐要求匹配；静态组与纹理组的布局在各自成功后分别冻结，合计最多 32 个槽位。
必需的映射或纹理载荷不匹配时可返回 `CapacityExceeded`；可选属性槽位可回退为
使用原始输入，并仍返回 `Ok`。同一工作区同一时间仅允许一个 Model 持有租约。
仍有租约时，`end()` 返回 `Busy`。工作区句柄和 Model 共享生命周期状态，
提前销毁句柄不会释放仍在租用的缓冲。这仅保证生命周期，不允许并发渲染和修改缓存。
清理及状态处理方式见 [重载示例](StaticGeometry.md#跨重载保留缓存)。

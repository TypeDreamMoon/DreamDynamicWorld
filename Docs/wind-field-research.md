# 动态风场系统 — 调研笔记

日期：2026-08-17
引擎：`F:\UnrealEngine\UE_Moon`（UE 5.8 定制分支）
目标：一个**世界级动态、性能可控**的风场系统，供植被 / 布料 / 头发 / 粒子 / 物理 / Gameplay 统一消费。

---

## 0. 先给结论

不要一上来就写 3D 流体解算。工业界能扛住开放世界的方案基本是**分层**的，成本从下往上递增，
而且**绝大部分视觉收益来自最便宜的那一层**：

| 层 | 内容 | 成本 | 谁消费 |
|---|---|---|---|
| L0 解析全局场 | 方向 + 强度 + 阵风(时间 FBM) + 低频空间噪声 | ~0（纯 ALU，CPU/GPU 同式） | 所有人 |
| L1 静态调制 | 地形加速/背风区/室内遮蔽，烘焙成 2D 图 | 一次烘焙 + 1 次采样 | 材质、Niagara |
| L2 动态体积 | 摄像机跟随的 clipmap 3D 纹理，注入 + 平流 + 衰减 | 0.1–0.3 ms GPU | 材质、Niagara、VFX |
| L3 交互注入 | 角色/技能/爆炸/载具写入 L2 | 与事件数成正比 | 写入方 |

关键设计原则（三条，违反哪条都会返工）：

1. **L0/L1 必须是纯解析函数**，同一份公式在 C++ 和 HLSL 各写一遍。
   这样 CPU 侧（布料、骨骼链、Gameplay、浮空物）**不需要任何 GPU 回读**就能拿到风，
   而且天然多人同步（位置+时间+种子的纯函数）。
2. **只有"扩散/平流后的残留"才允许住在 GPU 上**。爆炸/技能这类冲击源在 CPU 侧保留一份
   解析基元列表（球/锥 + 衰减曲线），CPU 需要时解析求值，GPU 侧同时注入体积做涡流细节。
   ⇒ 全系统零回读、零一帧延迟。
3. **风的成本不在顶点动画，在阴影失效**。见 §3。

Ghost of Tsushima 全游戏用的就是 L0（滚动 2D Perlin，CPU/GPU 共用采样），
God of War 才上了全 3D GPU 流体（L2）。先做 L0+L1，把 L2 当作可开关的增强项。

---

## 1. UE 5.8 现状盘点（本机引擎源码实测）

### 1.1 引擎自带的"风"是什么

`Engine/Source/Runtime/Engine/Public/SceneProxies/WindSourceSceneProxy.h`

```cpp
class FWindData { float Speed; float MinGustAmt; float MaxGustAmt; FVector Direction; };
```

就这 4 个标量/向量。`FWindSourceSceneProxy` 有两种形态：

- **方向源**：全场一致，`Weight = Strength`。
- **点源**：`Radius` 内按 `max(1 - |(p-P)/R|², 0)` 衰减，方向 = 从源心指向采样点，`Weight = (R-d)/R * Strength`。

`FScene::GetWindParameters()`（`Renderer/Private/RendererScene.cpp:3988`）遍历所有风源做**加权平均**后归一化。
所以引擎的"风场"= 一组点/方向源的加权插值，**没有网格、没有体积、没有平流、没有涡流、没有障碍物**。

### 1.2 谁在消费它（全引擎调用点就这几处）

| 消费者 | 位置 | 备注 |
|---|---|---|
| SpeedTree 顶点动画 | `RendererScene.cpp:4144 FScene::UpdateSpeedTreeWind` | **只调 `GetDirectionalWindParameters`** ⇒ 点风源对 SpeedTree 完全无效；且每个 StaticMesh 一份 uniform，全场同一个风值，**没有空间变化** |
| AnimDynamics（骨骼链物理） | `AnimGraphRuntime/.../AnimNode_AnimDynamics.cpp:1424` | 逐 body 走 `GetWindParameters_GameThread` |
| 骨骼网格布料 | `Engine/Private/SkeletalMeshComponentPhysics.cpp:3258` | 同上，game thread 点查询 |
| Niagara `WindForce` 模块 | `Plugins/FX/Niagara/Content/Modules/Update/Forces/WindForce.uasset` | 引擎里最完整的风消费者，带 `ENiagaraWind{Turbulence,Combing,GroundMask,Friction,Collision,Offset}Mode` 等枚举 |
| Groom 头发 | Niagara Hair Solver 里挂 Wind Force 模块 | 走 Niagara 那条路 |

### 1.3 材质端是断的

`Engine/Content/Functions/Engine_MaterialFunctions01/WorldPositionOffset/SimpleGrassWind.uasset`
和 `Engine_MaterialFunctions02/WorldPositionOffset/Wind.uasset` **完全不读场景风源**，
只吃时间和自己的参数。所以社区通用做法是：BP 每 tick 把 WindDirectionalSource 的
方向/强度写进 Material Parameter Collection，材质读 MPC。

⇒ **引擎材质端的"全局风"本来就要自己搭**，这正是我们要做的事，不存在"绕过引擎"的负担。

### 1.4 结论

grep 全引擎 `GlobalWind|WindGrid|WindVolume|FWindField|WindTexture` → 零命中。
**UE 5.8 没有任何体积风场基础设施**。我们要做的东西是净新增，不是改写既有系统，
接口面反而干净：只要最终把值喂回 `FWindData` 这条老路，SpeedTree/布料/AnimDynamics 自动受益。

---

## 2. 业界方案光谱

### A. 纯解析 / 滚动噪声（最便宜，性价比之王）

**Ghost of Tsushima**：风是贯穿全局的低分辨率向量场，本质是**滚动的 2D Perlin 噪声**，
CPU 和 GPU 用同一个采样函数，开销可忽略。草叶（数百万）、竹子、树、布料（数千片）、
粒子（数十万）全部读同一个场，所以整个世界"往同一个方向呼吸"——这是"引导之风"玩法的基础。

- GDC 2021《Blowing from the West: Simulating Wind in *Ghost of Tsushima*》
  — 幻灯片 PDF：<https://media.gdcvault.com/GDC+2021/Rockenbeck-Bill-Blowing+from+the+West.pdf>
  — 视频：<https://www.youtube.com/watch?v=d61_o4CGQd8>
  — GDC Vault：<https://gdcvault.com/play/1027124/Blowing-from-the-West-Simulating>
- GDC 2021《Procedural Grass in *Ghost of Tsushima*》（草的 compute 生成 + 风采样）
  <https://gdcvault.com/play/1027033/Advanced-Graphics-Summit-Procedural-Grass>
- SIGGRAPH 2021 Advances《Real-Time Samurai Cinema》（同作，渲染整体）
  <https://advances.realtimerendering.com/s2021/jpatry_advances2021/index.html>
- 第三方拆解笔记：<https://tigerabrodi.blog/grass-in-ghost-of-tsushima> ·
  <https://haoranliang.com/grass-rendering>

**Curl noise** 是这一层的"高配"：对噪声取旋度，结果**天然无散度**（看起来像真流体的涡），
还能被改造成尊重固体边界。

- Bridson et al., *Curl-Noise for Procedural Fluid Flow*, SIGGRAPH 2007
  <https://www.cs.ubc.ca/~rbridson/docs/bridson-siggraph2007-curlnoise.pdf> ·
  ACM: <https://dl.acm.org/doi/10.1145/1275808.1276435>
- 边界感知的改进版（对"绕过建筑/山体"很有用）：
  <https://cs.uwaterloo.ca/~c2batty/papers/Ding2023/Differentiable_Curl_Noise.pdf>

### B. 分层程序化顶点动画（风怎么"变成动作"）

风场只给你一个向量，**把向量变成好看的摆动是另一半工作**，而且这半边定成本、可复用。

- GPU Gems 3 ch.16 — *Vegetation Procedural Animation and Shading in Crysis*（Tiago Sousa）：
  拆成 **main bending**（整株沿风向弯）+ **detail bending**（叶片抖动），
  逐实例在世界空间累加多个风区（有向/无向，像点光一样带衰减）。同一套也用在布料和头发上。
  <https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-16-vegetation-procedural-animation-and-shading-crysis>
- GPU Gems 3 ch.6 — *GPU-Generated Procedural Wind Animations for Trees*（Renaldas Zioma）：
  树干/树枝/叶片的分层刚度模型，全在 vertex shader，配合 instancing。
  <https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-6-gpu-generated-procedural-wind-animations-trees>
- 实操向博客（顶点色编码刚度/相位，62 条指令的 VS）：
  <https://mtnphil.wordpress.com/2011/10/18/wind-animations-for-vegetation/>
- SpeedTree 的官方风模型（Global / Branch / Leaf / Frond 四段，UE 走的就是这套）：
  <https://docs.speedtree.com/doku.php?id=advancewind> ·
  <https://docs8.speedtree.com/modeler/doku.php?id=windvfx> ·
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/using-speedtree-in-unreal-engine>

### C. 2D 风图 / 流图（地形调制层）

世界尺度的一张俯视纹理，存"这个 XY 位置的风被地形怎么改造了"：山脊加速、背风面减速、
峡谷导流、城墙后的静风区。烘焙一次，运行时一次采样。
Horizon Zero Dawn 的植被系统就用了全局风力场配合大规模植被。

- GDC 2018《Between Tech and Art: The Vegetation of *Horizon Zero Dawn*》（Gilbert Sanders）
  — PDF：<https://media.gdcvault.com/gdc2018/presentations/gilbert_sanders_between_tech_and.pdf>
  — 视频：<https://www.youtube.com/watch?v=wavnKZNSYqU>
  — Vault：<https://www.gdcvault.com/play/1025530/Between-Tech-and-Art-The>

UE 里的载体候选：Runtime Virtual Texture（跟地形对齐、自动 mip/流送），或一张普通的
world-size `R11G11B10` / `RG16F` 纹理。

### D. 真 3D 流体解算（最贵，但天花板最高）

**God of War (2018)** 上的是**完整的 3D GPU 流体模拟**驱动全局风，
讲的是"为什么值得"以及提速的取舍。

- GDC 2019《Wind Simulation in *God of War*》（Rupert Renard, Santa Monica Studio）
  — Vault：<https://gdcvault.com/play/1026404/Wind-Simulation-in-God-of>
  — 视频：<https://www.youtube.com/watch?v=dDgyBKkSf7A>
  — 场次说明：<https://schedule2019.gdconf.com/session/wind-simulation-in-god-of-war/860474>
  — 媒体综述：<https://www.gamedeveloper.com/programming/video-simulating-the-wind-of-i-god-of-war-i->
- 求解器的经典起点（add source → diffuse → advect → project）：
  Jos Stam, *Real-Time Fluid Dynamics for Games*, GDC 2003
  <http://graphics.cs.cmu.edu/nsp/course/15-464/Fall09/papers/StamFluidforGames.pdf> ·
  作者页：<https://www.dgp.toronto.edu/public_user/stam/reality/Research/pub.html>

**成本提醒**：projection（压力泊松）是最贵的一步，也是"看起来像流体"的来源。
折中做法是**只做平流+衰减，用 curl noise 补涡**，把 projection 砍掉或降到 2~4 次 Jacobi。

### E. 局部交互注入

角色踩过/奔跑/技能/爆炸/载具尾流 → 写进风场（L2）或独立的 bend map。
UE 社区成熟做法是 Scene Capture / 粒子写 Render Target，再用 RVT 分发。

- <https://80.lv/articles/tutorial-interactive-grass-in-unreal>
- <https://forums.unrealengine.com/t/foliage-bend-on-collision-interactive-foliage/391104>
- Godot 上的完整开源实现（着色器思路可直接迁移）：<https://github.com/2Retr0/GodotGrass>
- Unity 复刻 GoT 草的开源实现：<https://github.com/harlan0103/Grass-Rendering-in-Modern-Game-Engine>

---

## 3. 性能真相：风的开销主要不在"算风"

这一节是这份调研里最省钱的部分。

### 3.1 WPO 会让 VSM 缓存整片失效 ← 头号成本

Nanite 植被 + WPO 的性能崩塌，绝大多数来自 **Virtual Shadow Map 页缓存每帧失效**，
不是顶点算得慢。一棵一直在动的树 = 它覆盖的所有 VSM 页每帧重画。

- <https://forums.unrealengine.com/t/vsm-with-nanite-foliage-wpo/776406>
- <https://medium.com/@shinsoj/notes-on-foliage-in-unreal-5-3522b6eb159f>

**本项目已有前车之鉴**：`moontoon-toon-wpo-vsm-artifact` 记录的"阴影盘"就是逐帧 WPO 失效导致的。
所以风场上线前必须先定阴影策略（见 §5 预算）。

### 3.2 必须设 WPO Disable Distance

`FFoliageType::WorldPositionOffsetDisableDistance`（`Runtime/Foliage/Public/FoliageType.h:367`）
默认是 0 = 永不禁用。这是 Nanite 植被单项收益最大的开关：远处直接停掉顶点动画。
另有 `bEvaluateWorldPositionOffset` / `bEvaluateWorldPositionOffsetInRayTracing`
（`InstancedFoliage.cpp:1727-1739`）——注意光追那份是**分开的**，别只关了栅格那份。

- Nanite Foliage 官方文档：<https://dev.epicgames.com/documentation/unreal-engine/nanite-foliage?lang=en-US>
- 实践指南：<https://www.strayspark.studio/blog/nanite-foliage-ue5-complete-guide>

### 3.3 其它已知坑

- Nanite + WPO 会强制**保守的 cluster bounds**，剔除效率下降。
- 给资产开 RVT 写出，在某些配置下会被当成"启用了 WPO"，连带吃到上面所有代价。
- CPU 侧 `GetWindParameters_GameThread` 是**逐 body / 逐组件**的点查询（布料、AnimDynamics），
  角色一多就是 O(角色 × 骨骼体) 次 game-thread 调用。解析层能直接把这个变成几条 ALU。
- **CPU 与 GPU 的风必须同式**。头发用 GPU 解算、裙子用 CPU 布料，两边风值不一致 = 穿模/分家。

### 3.4 本项目特有约束

- **CustomPrimitiveData 已满**（9 个 float4 用光，slot 28–35 已被透视矫正和 toon actor id 占掉，
  见 `moontoon-cpd-slot-budget`）⇒ **不能靠 per-instance CPD 下发风参数**，
  风必须在着色器里"按世界坐标采样"，或者搭 `ToonActorTexture` 那条车。
- Toon 二值化会放大运动的**时域走样**：风让法线/阴影抖动一点点，卡通阈值就会把它放大成闪烁。
  风的高频分量在 toon 材质上要比在 PBR 上更保守。

---

## 4. 建议架构（映射到 UE 具体实现）

```
UWindFieldSubsystem (UWorldSubsystem)
├─ L0 解析层   FWindAnalytic { BaseDir, BaseSpeed, GustFBM(t), LowFreqNoise(p,t) }
│              └── 双实现：WindField.ush  ⟷  WindFieldAnalytic.h/.cpp（同一套常量）
├─ L1 地形调制 UTexture2D / RVT，世界俯视，RG16F = 风向偏转 + 强度缩放
├─ L2 动态体积 FRDGTexture 3D，摄像机跟随 clipmap（如 L0 32m / L1 128m / L2 512m）
│              64×64×32 RGBA16F ≈ 0.5 MB/级，环形寻址（toroidal）⇒ 相机移动零拷贝
│              每帧：注入 → 平流（半拉格朗日）→ 衰减 → (可选 2~4 次 Jacobi projection)
└─ L3 注入源   FWindImpulse { Center, Radius, Dir, Strength, Falloff, TTL }
               CPU 保留权威列表（可解析求值）+ 同时注入 GPU 体积做涡流残留
```

**采样接口收敛成三个**，其余全部走它们：

| 消费方 | 接口 |
|---|---|
| 材质 | 一个 `MF_SampleWind(WorldPos)` 材质函数：解析层 ALU + 体积一次采样 |
| Niagara | 一个 Data Interface（或先用 MPC + 现成 WindForce 模块过渡） |
| C++/BP/物理 | `UWindFieldSubsystem::SampleWind(FVector)`，只走 L0/L1 + CPU 冲击列表，**零回读** |

同时把最终风值写回 `FWindData` 那条老路，SpeedTree / Chaos Cloth / AnimDynamics / Groom
**不用改一行**就能吃到新风场。

相关引擎设施文档：

- Niagara Data Channels（游戏代码 ⟷ Niagara 的数据通路，做风事件广播很合适）
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/niagara-data-channels-overview>
- Sparse Volume Textures（如果 L2 想走"烘焙+流送"而不是实时解算）
  <https://dev.epicgames.com/documentation/unreal-engine/sparse-volume-textures-in-unreal-engine?lang=en-US> ·
  <https://dev.epicgames.com/documentation/unreal-engine/heterogeneous-volumes-in-unreal-engine?lang=en-US>
- Hair Physics 总览（Groom 走 Niagara + XPBD，风从 Wind Force 模块进）
  <https://dev.epicgames.com/documentation/unreal-engine/hair-physics-in-unreal-engine---overview>
- Niagara Simulation Stages / Grid2D-3D（自己写 L2 解算时的宿主）
  <https://www.strayspark.studio/blog/niagara-vfx-advanced-simulation-stages>

---

## 5. 预算与验收指标

上线前先把这几个数钉死，否则"性能良好"没法证伪：

| 指标 | 目标 | 测法 |
|---|---|---|
| L2 体积更新 GPU 耗时 | < 0.3 ms @ 1080p 档 | `stat GPU` / Unreal Insights GPU track |
| VSM 页失效数（有风 vs 无风） | 增幅 < 2× | `r.Shadow.Virtual.ShowStats 1` |
| 材质 VS 增量指令 | < 30 条（含一次体积采样） | 材质统计面板 |
| CPU 风查询 | < 0.05 ms / 帧（全场角色） | Insights game thread |
| 显存 | < 4 MB（3 级 clipmap） | `stat RHI` |

**方法论提醒**（吃过亏的）：先量分布再改判据；单次墙钟波动大，信操作计数（失效页数、
采样次数）不信一次跑分。

---

## 6. 现成可拆的实现参考

| 项目 | 链接 | 值得看什么 |
|---|---|---|
| JkWindSystem（UE5 开源全局风） | <https://github.com/josephkirk/JkWindSystem> | 直接照 Jos Stam 解算做的网格风场；README 里有**网格尺寸 × 更新频率的性能取舍实测**，正好是我们要避的坑 |
| UE_GlobalEnvironmentalSystem | <https://github.com/delebash/UE_GlobalEnvironmentalSystem> | 天气/风/水/植被联动的整合思路 |
| GodotGrass | <https://github.com/2Retr0/GodotGrass> | GoT 式草 + 风的完整可读实现 |
| Grass-Rendering-in-Modern-Game-Engine | <https://github.com/harlan0103/Grass-Rendering-in-Modern-Game-Engine> | Unity 复刻 GoT compute 草 |
| Magic Wind（Fab，付费） | <https://www.fab.com/listings/7f30b22c-3c81-4dee-b759-e99c9cf89206> | 商用植被风方案的功能面基准 |
| Groom Wind System（Fab，付费） | <https://www.fab.com/listings/0b3e5a57-3e76-4b41-baef-602772ca9fb8> | 头发风的参数面 |
| UE5 风搭建全流程教程 | <https://yelzkizi.org/wind-in-unreal-engine-5-winddirectionalsource-foliage-wind-niagara-forces-cloth-and-groom-hair-setup/> | 把 WindDirectionalSource → 植被/Niagara/布料/Groom 四条线一次串起来 |
| Global Wind Controller（Epic 社区教程） | <https://dev.epicgames.com/community/learning/tutorials/vJPb/unreal-engine-5-global-wind-controller> | MPC 同步风的标准做法 |

---

## 7. 建议的下一步

1. **Spike 1（半天）**：只做 L0 解析层 + `MF_SampleWind` + 写回 `FWindData`。
   用现有草/树材质换掉 `SimpleGrassWind`，确认"整个世界一起呼吸"的观感。
2. **Spike 2**：加 §5 的度量，先拿到无风/有风的 VSM 失效基线 —— 这个数决定 L2 值不值得做。
3. **Spike 3**：L2 clipmap 体积 + 一个爆炸注入源，验证 0.3 ms 预算。
4. L1 地形烘焙放最后（它是内容管线活，不是渲染活）。

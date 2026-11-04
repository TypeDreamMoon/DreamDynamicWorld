# DreamDynamicWorld

Systems that make a world feel alive, for Unreal Engine 5.8 (MoonEngine). They share one set of
conventions: **one analytic model evaluated identically on the CPU and the GPU** (a single header
compiled as both HLSL and C++, so gameplay, cloth, materials and Niagara never disagree and nothing
reads back from the GPU), plus small camera-following volumes for the things that must be simulated
locally.

| Subsystem | State | Content | Modules |
| :-- | :-- | :-- | :-- |
| **Core: world clock, sun and moon** | shipping | `/DreamDynamicWorld/MPC_DreamDynamicWorld` | `DreamDynamicWorld`, `DreamWorldEditor` |
| **Sky** (stylized) | shipping | `/DreamDynamicWorld/Sky/**` | `DreamSky` |
| **Weather** (state, zones, map; precipitation and surfaces next) | shipping | `/DreamDynamicWorld/Weather/**` | `DreamWeather` |
| **Wind** | shipping | `/DreamDynamicWorld/Wind/**` | `DreamWind`, `DreamWindNiagara`, `DreamWindEditor` |
| Water | planned | `/DreamDynamicWorld/Water/**` | — |

Each subsystem keeps its own modules, content folder, settings section and console prefix. They share the
core module: the world clock, and **one Material Parameter Collection** (`MPC_DreamDynamicWorld`) that every
subsystem writes — a material may reference only two collections, so a material that reads the wind and the
sky still leaves one for the project.

> The wind's own `MPC_DreamWind` became that shared collection (same parameter names). The plugin's
> `Config/DefaultEngine.ini` redirects the old path; copy its `[CoreRedirects]` into your project's
> `Config/DefaultEngine.ini` like the one below.

## Setup

1. Enable the plugin (it depends on Niagara). Build.
2. `UnrealEditor-Cmd.exe <project> -run=DreamDynamicWorldSetup` creates (or repairs) the shared collection
   with every subsystem's parameters, the wind's render targets and presets, the default sky style, the
   weather map and presets, and the horizon cloud sprites (imported from `SourceArt/Sky`, again when the PNG
   is newer). `-TestMap` also writes the sky test level; `-ForceStyle` / `-ForcePresets` write the class
   defaults over the existing style / presets; `-ForceSprites` imports the sprites again. The assets are
   committed with the plugin already.
3. Drop an **`ADreamSky`** in the level (remove the level's own directional lights, sky light and height
   fog), and for wind optionally an **`ADreamWindGlobal`**.

## Clock and sky

**World clock** (`UDreamWorldSubsystem`). World time is a double count of seconds in the world's own
calendar; real time times the time scale moves it (*Day Length Minutes*, default 24 = one real minute per
game hour). Sun and moon are a pure function of it: *Artistic* mode (sunrise and sunset hours, the sun's
height at midday, the moon kept opposite the sun if you like — the same every day) or *Realistic* mode
(latitude, longitude, UTC offset, the date: NOAA sun, low-precision moon with its real phase; about a
degree). Setting the time, a snapshot or scrubbing puts the whole sky exactly where it would have been.

```cpp
UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(this);
Clock->SetTimeOfDay(18.5f);                       // jump (events skipped, OnTimeJumped fires)
Clock->TransitionToTimeOfDay(6.f, 5.f);           // run forward to 06:00 over 5 real seconds
FDreamWorldSnapshot Save = Clock->CaptureSnapshot();   // into a save game; ApplySnapshot on load
Clock->OnSunset.AddDynamic(this, &AMyActor::LightTheLamps);
```

Events (Blueprint-assignable): `OnHourChanged`, `OnDateChanged`, `OnSunrise`, `OnSunset`, `OnTimeJumped`.
Other subsystems update inside the clock's tick, in order, through `OnWorldTick`, and spread occasional heavy
work through its scheduler (`GetScheduler()`, a per-frame millisecond budget).

**Sky** (`ADreamSky` + `UDreamSkySubsystem`). The actor owns the lights the sky drives — the sun (the
atmosphere sun, index 0) and the moon, a sky light that captures the dome, a height fog and an unbound post
process for exposure — and a dome drawn by `M_DreamSky` (`DShader/Sky/M_DreamSky.dss`, drawing in
`Shaders/Private/DreamSkyStylized.ush`): gradient, sun and halo, moon with its phase, stars, a layer of toon
clouds overhead and clouds standing on the horizon. A `UDreamSkyStyle` (default `DS_DreamSkyStylized`) says how everything looks: colour and
intensity curves keyed by the **sun's elevation** (so one style fits any day length or latitude), cloud
shapes and bands, and manual exposure from an EV100 curve (no eye-adaptation lag when time jumps).

The lighting eases while the clock runs — a toon ramp reading the main light never flickers across its
threshold — and snaps when the clock jumps. The directional lights turn only after the sun moved
`LightRotationThreshold` degrees (moving a light invalidates cached shadows); the sun is set black, not
hidden, at night, so it costs nothing and anything that asks "where is the sun" still finds it. After dark the
moon light takes over; when the moon is down a stand-in opposite the sun keeps a main light (stylized
default, `bNightLightAlwaysUp`).

The clouds are drawn the way anime backgrounds and stylised games paint them. Overhead, heaps of round lobes:
discs blended into soft heaps with small lobes along their rims (`CloudLobeDetail`), lit like pillows — the
side toward the light bright, the far side in a pale blue-grey shade, in a few clean bands — gathering where a
slow field says, swelling into a closed deck under full cover, thinning toward the zenith
(`CloudZenithThinning`) and breathing a little over the hours. On the horizon, painted cut-outs from
`T_DreamCloudSprites` (`SourceArt/Sky`, packed by `Tools/Art/pack_cloud_sprites.py`: light, rim, core and
coverage per sprite): fair-weather cumulus floating low (`CloudTowers`), and weather towers read from the
weather map along each direction (`CloudTowerDistance`) — a storm cell kilometres away stands up as a
cumulonimbus with an anvil, a front of thick cloud as towering cumulus — up to `CloudTowerElevation`. The
overhead layer only follows the weather map out to `CloudLayerWeatherReach`; further weather is the towers' job.
All clouds take the colour of the sky around them (`CloudSkyTint`: warm low in a sunset, deep blue overhead)
and catch the sun's glow beside it; a cloud with the sun behind it turns darker with a bright edge. The sun's
halo spreads along the horizon while the sun is low. When the sky light captures
the dome it sees no stars and only `SunDiskInSkyLight` of the sun disk, so the captured light has no hot spot.

**Indoors.** The clock probes the view (`GetViewOcclusion`): rings of rays around the camera, a few a frame, a
full round every 0.35 s, tell how enclosed it is (*Interior*), whether a roof is overhead (*Overhead*) and which
sides are closed. The sky lifts the exposure indoors (`InteriorExposureLift`) and can scale the sky light and the
sun and moon there; `DDW_Interior` carries it to materials. Settings (channel, reach, round length, smoothing)
in *Project Settings ▸ Dream Dynamic World ▸ View Probe*.

**For any material or character shader** the collection carries the lighting: `DDW_MainLight` (direction
toward the main light, illuminance), `DDW_MainLightColor` (rgb, 0 sun / 1 moon), `DDW_AmbientSky /
Equator / Ground`, `DDW_DayPhase` (day, twilight, night weights and the sun's elevation), plus `DDW_SunDir`,
`DDW_MoonDir` (phase in w) and `DDW_Time`. In C++/Blueprint: `UDreamSkySubsystem::GetLighting()`.

**Editor.** The level toolbar has a time-of-day slider (scrubbing also sets the level's start hour,
undoably); the actor's *Time Of Day* does the same. Console: `DreamWorld.Time <hours>`, `DreamWorld.Date
<yyyy-mm-dd>`, `DreamWorld.Pause`, `DreamWorld.DayLength <minutes>`, `DreamWorld.Transition <hours>
[seconds]`, and `DreamWorld.ShotTour Hours=0,6,12,18 Prefix=Day Quit` (screenshots each hour into
`Saved/Screenshots/DreamWorld`, for scripted checks of a day).

**Test level.** `/DreamDynamicWorld/Sky/Maps/Lvl_DreamSkyTest`: grey floor, primitives, the sky, random
weather, a thunderstorm zone four kilometres away, and a camera looking south-west so the sunset and the storm
are in frame.

## Weather

Drop an **`ADreamWeather`** in the level to turn the weather on (without one the sky keeps to its style).

**Channels.** The weather is a set of named numbers (`Dream.Weather.Channel.*` gameplay tags): cloud coverage
and darkness, precipitation and snow ratio, fog, dust, wind, lightning, temperature offset, wetness, snow cover.
A project adds its own (ash, spores, miasma) in *Project Settings ▸ Dream Weather ▸ Custom Channels*; every
query, preset, zone and modifier handles them like the built-in ones. Each channel has its own transition time
and **lead**: clouds and wind start changing before the weather turns, rain and lightning follow, the ground
lags — a change of weather reads as coming.

**Schedule.** The actor's *Selector* chooses the weather over time — `Fixed`, `Sequence` (a scripted cycle),
`Random` (weighted, with persistence) or `Seasonal` (weights per season, either hemisphere). Every selector is a
pure function of the world time and seed: the same moment has the same weather on every machine, after every
load, in every replay, and `GetForecast` is just the function evaluated ahead. Presets (`UDreamWeatherPreset`)
hold channel values; the plugin ships `WP_Clear / Cloudy / Overcast / Rain / Storm / Snow / Fog`.

**Bending it.** `PushModifier` (a scripted storm, a spell that brings rain: channel values or a preset,
Override / Add / Multiply / Max / Min, a weight, a priority, real-time fades) and `ForceWeather(Preset)`.
**Zones** (`ADreamWeatherZone`, or `UDreamWeatherZoneComponent` on anything) give a region weather of its own: a
box or circle footprint, a blend distance, a priority (equal priorities apply in a fixed order, never by
registration order). Anything else can bend the weather where it is by implementing `IDreamWeatherInfluence`.

**Queries and events.** `SampleWeather(Position)` — every channel there, rain and snow (precipitation turns to
snow near freezing, so a mountain top snows in the valley's rain), the temperature (climate, season, hour,
height, the weather's offset) and `Dream.Weather.State.*` tags for gameplay conditions. Events:
`OnWeatherChanged`, `OnPrecipitationStarted / Stopped` (with hysteresis), `OnWeatherTagsChanged`. The modifiers
are saved in the world snapshot; the schedule needs nothing saved.

**What it drives.** The sky (cloud cover, a greyer and dimmer sky under cloud, more fog, an exposure lift), the
wind's speed and gusts, and the shared collection (`DDW_Weather0/1` at the view). The zones are also drawn by a
compute pass into a **weather map** around the camera (`RT_DreamWeatherMap`); the sky material looks the clouds
up in it where each direction meets the cloud layer, so a storm zone's dark clouds show from afar. The zone
falloff is one function (`Shaders/Shared/DreamWeatherShared.h`) compiled for the compute pass and for the CPU
queries: what the sky shows over a valley is what gameplay reads there.

**Shelter.** Every query carries an *Exposure* (0 under a roof .. 1 in the open) and `RainHere / SnowHere`,
the rain and snow that actually land on the point. Shelters are boxes (`ADreamWeatherShelter`, or
`UDreamWeatherShelterComponent` inside a house, under a bridge, in a cave mouth) or anything implementing
`IDreamWeatherShelterProvider`; with a baked wind terrain map (`ADreamWindGlobal`) the level's own roofs count too, and
rain blown by the wind reaches in under the windward eaves. `IsSheltered(Position)` for gameplay.

**Lightning.** Strikes come out of the Lightning channel — around the view, preferring wherever the lightning is
strongest, so a far storm flashes on the horizon — at `StrikesPerMinute` real-time strikes at full strength
(real, so a storm looks the same at any day length), from the world seed (the same frames give the same storm).
`OnLightningStrike` gives the place, intensity, distance and the thunder's delay (`ThunderSecondsPerKm`, the
speed of sound by default). A `UDreamLightningRodComponent` (a rod, a sword held high, a tall tree) draws
strikes landing within its reach and reports `OnStruck`. Each strike flashes for about a second — a few bright
strokes, then a fading afterglow — in the clouds over it, the ambient light and the sky's flash light (a
directional light from the strike, shadows optional). The flash is brightened with the exposure
(`LightningExposureCompensation`), so it still reads at noon.

**Rain and snow** (module `DreamWeatherFX`). Around the view, a buffer of *fall paths* (1024 by default) is
traced a few hundred a second: each starts high above, falls down and downwind and ends where it hits something
— ground, roof, canopy — or passes under a shelter (`FDreamWeatherFallPaths`). The effects (`NS_DreamRain`, from
`DFX/Effects/NS_DreamRain.dfs`) draw thousands of drops by picking paths at random (`DreamFallPath.dfm`): a drop
falls along its path and dies where it lands, and splashes open where landed paths end. No drop queries
collision, no rain falls indoors, and the splashes land on what the rain lands on. Amounts, fall speeds, wind
drift and the paths are in *Project Settings ▸ Dream Weather Effects*; an effect only lives while its
precipitation falls. The weather hands every frame to listeners through `UDreamWeatherSubsystem::OnWeatherFrame`.

Console: `DreamWeather.Set <preset> [seconds]`, `DreamWeather.Clear`, `DreamWeather.Forecast [hours] [step]`,
`DreamWeather.Dump`.

The rest of this document is **Wind**.

> Renamed from `DreamWind` on 2026-08-18. Content moved from `/DreamWind/<X>` to
> `/DreamDynamicWorld/Wind/<X>`; the plugin's `Config/DefaultEngine.ini` carries the `[CoreRedirects]`
> entry that keeps old references loading — **copy it into your project's `Config/DefaultEngine.ini`**
> (package redirects are read before plugin configs are merged, so a plugin-side copy alone does not
> apply). Module names, class names, `DreamWind.*` console variables and MPC parameter names are
> unchanged.

## Wind

A dynamic, world-scale wind field, built to be **cheap where it counts and consistent everywhere**:
the analytic model above, plus a small camera-following volume for local air.

| Layer | What | Cost | Who reads it |
| :-- | :-- | :-- | :-- |
| **L0 analytic** | direction · speed · travelling gust fronts · curl turbulence, a pure function of (position, time) | ALU only, 3 (cheap) or 6 (full) noise evaluations; 0.05 µs / 0.09 µs on the CPU | everyone |
| **L1 terrain** | a baked 2D map over the level: ridges blow harder, hollows are sheltered, wind steers toward valleys, and level geometry casts a **wind shadow** (walls, rocks, hill crests; calm inside solids) | 1 bilinear fetch + 25 for the shadow march, ~1 MB per 256² map (2 MB with its max-height chain) | everyone |
| **L2 volume** | two 64×64×32 RGBA16F windows that follow the camera (near: 1 m voxels / 64 m, far: 4 m voxels / 256 m); residual wind, impulses, wakes; advected and decayed each frame | two compute passes, 2 MB | materials (on by default), Niagara GPU |
| **L3 sources** | velocity fields with a footprint: directional / radial / vortex spheres (directional and vortex can be **stretched** into a jet along their axis) and **shelter** boxes (calm interiors, wind zones); transient impulses or components that follow an actor | per source, ≤ 64 uploaded | CPU sample, volume, Niagara |

Consumers, all fed from the same `UDreamWindSubsystem`:

* **Materials** — the shared `MPC_DreamDynamicWorld` + `MF_DreamWindSample` (DreamShaderLang, `DShader/`)
* **Niagara** — `UNiagaraDataInterfaceDreamWind` (`SampleWind`, `GetGlobalWind`) on CPU VM and GPU, and the `DreamWindForce` module (DreamFXLang, `DFX/`)
* **C++ / Blueprint** — `UDreamWindSubsystem::SampleWind`, no GPU read-back
* **Everything that reads the engine's `WindDirectionalSource`** — SpeedTree, Chaos Cloth, AnimDynamics, Groom, Niagara *Wind Force* — through one driven engine wind component

## Wind setup

1. Enable the plugin (it depends on Niagara). Build.
2. **Tools ▸ DreamWind: Setup Assets** (or `UnrealEditor-Cmd.exe <project> -run=DreamWindSetup`, or the
   plugin-wide `-run=DreamDynamicWorldSetup`). Creates the shared collection, the volume / terrain render
   targets at the paths in *Project Settings ▸ DreamPlugin ▸ Dream Wind*, and the wind presets
   `/DreamDynamicWorld/Wind/Presets/DWP_Calm | DWP_Breeze | DWP_Storm`. All are committed with the plugin already.
3. Optionally drop an **`ADreamWindGlobal`** actor in the level to author the global wind (direction,
   speed, gusts, turbulence). Without it the project default applies.
   Its **Bake Terrain Map** button traces the level's collision into a `UDreamWindTerrainMap`
   (`<Level>_WindTerrain` next to the map), assigns and applies it. Bake bounds default to the level's
   static geometry; the bake settings (resolution, blur radius = the size of a "ridge", exposure gain,
   steer gain, max steer, texel sweeps) live on the actor. Each texel column is swept with a
   texel-sized box, so walls and posts thinner than a texel still register — pick a resolution that
   gives ~0.5–1 m texels where you want wind shadows (512 over 200 m = 39 cm). Actors that carry a
   **`UDreamWindPassThroughComponent`** (or the actor tag `DreamWind.PassThrough`) are left out:
   fences, foliage, thin props, anything whose collision should not block the wind. Re-bake after
   changing them.
4. Remove hand-placed `WindDirectionalSource` actors — DreamWind drives one for you.

## Using it

**Materials.** Call `MF_DreamWindSample(WorldPosition)` → `Velocity` (cm/s), `Gust`, `Speed`,
`Direction`. Bend geometry with `Velocity * (Response * TipMask)` in World Position Offset. Two static
switches, exposed on every material instance:

* **Wind Volume** (on) — adds the dynamic volume inside its windows: up to two texture fetches. This
  is what makes grass answer impulses and wakes; turn it off on the cheapest foliage.
* **Wind High Quality** (off) — 2 gust octaves + 2 curl octaves + vertical turbulence instead of the
  3-evaluation cheap wind. Hero foliage, cloth-like sheets.

The flags reach the HLSL as literal constants, so the shader compiler folds the branch away. See
`DShader/Materials/M_DreamWindGrass.dsm` and `M_DreamWindDebug.dsm` (paints direction as hue, speed as
brightness — put it on a big plane).

**Niagara.** Add a `Dream Wind` data interface (User parameter) and use `DreamWindForce(Wind, Drag,
Strength, Lift)` in Particle Update before *Solve Forces and Velocity*; or call
`Wind.SampleWind(Particles.Position, Velocity, Gust)` in your own module. `NS_DreamWindLeaves` (GPU)
and `NS_DreamWindDebug` (CPU arrow grid) are the references.

**Gameplay.**
```cpp
UDreamWindSubsystem* Wind = UDreamWindSubsystem::Get(this);
FDreamWindSampleResult W = Wind->SampleWind(Location);          // analytic + live sources, CPU only
Wind->AddRadialImpulse(Explosion, 800.f, 3000.f, 0.3f);          // shockwave
Wind->SetGlobalWindDirectionAndSpeed(FVector(0,1,0), 900.f, 2.f); // blend over 2 s
```
`UDreamWindSourceComponent` on an actor gives a persistent, moving source (fan, wake, vortex).

**How far a fan blows.** Sources *blend* the wind toward their own velocity by a footprint weight —
they never add — so `Strength` is the speed *inside* the footprint and only `Radius` and `Stretch`
decide the reach. Turning Strength up makes the edge harder, not farther. For a jet set **Stretch**:
the sphere becomes an egg that reaches `Radius × Stretch` ahead of the centre along +Direction (and
one radius behind it); `Stretch 5` on a 7 m radius is a 35 m fan lane. Vortices stretch into columns
the same way; radial sources ignore it.

**Weather states.** `UDreamWindPreset` is a data asset holding one global wind; the plugin ships
`DWP_Calm` (150 cm/s), `DWP_Breeze` (600) and `DWP_Storm` (2200). `Wind->ApplyPreset(Preset, 3.f)`
blends to it, `ApplyPresetKeepDirection` changes strength without turning the wind, and
`ADreamWindGlobal` can be pinned to a preset (*Preset*, which then replaces its inline *Wind*).

**Interiors and wind zones.** The terrain bake looks straight down, so it cannot know that a house is
hollow. Place a source of type **Shelter** instead: an oriented box (`BoxExtent` in the component's
frame, rotation and scale from the transform) inside which the wind becomes `Direction * Strength` —
`Strength 0` is calm air (rooms, caves, the lee of a wall), `Strength > 0` is a wind zone blowing along
+X (corridor draft, wind tunnel). Shelters are seen by every consumer like any other source: the CPU
sample and the Niagara DI apply them directly, materials get them through the dynamic volume (a
`InjectRate` of 60 settles in a few frames). Shelters are kept ahead of everything else when the source
cap is reached. `ADreamWindSourceActor` draws the box.

**Walls block the wind.** With a baked terrain map, every sample also marches upwind through the
baked heights (12 taps over `OcclusionRange`, dense near the sample, each reading the max-height level
whose texel is at least as wide as the step, at the tap and at the mid-step — so a one-texel wall is
never stepped over and the shadow stays smooth). Anything that rises above the sample shades it: right
behind fully, fading to nothing at `rise / OcclusionSlope` downwind (0.2 = five heights). Inside a
solid the wind is calm. `OcclusionStrength` (0.9: a little air still moves in the lee), `OcclusionRange`
(30 m) and `OcclusionSlope` are project settings; the shadow follows the *current* wind direction, so
turning the wind moves every shadow — the CPU sample, materials, Niagara and the volume all march the
same way. It is 2.5D: overhangs, bridges and roofed rooms need a `Shelter` box for the space
underneath. Things that must not shade — see the pass-through component above.

**Console.** `DreamWind.Enable 0|1|-1`, `DreamWind.Volume 0|1|-1` (override the settings for A/B
tests), `DreamWind.TimeScale`, and the debug view:

* `DreamWind.DebugDraw 1` — volume windows, sources (spheres / shelter boxes), the global arrow;
  `2` — also the **arrow field**; `3` — the arrow field only.
* The arrow field is a **3D lattice fixed in world space** (anchored at the world origin in X, Y and Z),
  and only the cells inside `DreamWind.DebugDraw.Range` (cm, default 2000) of the **player camera**
  (the viewport camera in the editor) are drawn — walk through it and the arrows stay put. Each cell is
  one CPU sample (analytic + terrain + sources, what cloth and gameplay see): the arrow points along the
  wind, its length is `Speed × DreamWind.DebugDraw.Scale` (default 0.25 cm per cm/s), and its colour is
  `DreamWind.DebugDraw.Color` — `0` speed ramp blue → green → yellow → red up to
  `DreamWind.DebugDraw.MaxSpeed` (1500), `1` direction hue (the same wheel as `M_DreamWindDebug`),
  `2` gust factor (blue lull → white → red gust). Calm cells (shelters, the eye of a vortex) draw as dots.
* `DreamWind.DebugDraw.Spacing` (default 250 cm) is widened automatically if the range would need more
  than 4096 cells; `DreamWind.DebugDraw.Field3D 0` gives flat sheet(s) instead — `Layers` planes from
  world Z `Height` upward.

## Test map

Open **`/DreamDynamicWorld/Wind/Maps/Lvl_DreamWindTest`** (Content Browser ▸ Settings ▸ *Show Plugin Content* ▸
DreamWind Content ▸ Maps; make sure the viewport is **Realtime**). It has everything wired up:

* a 9 000-blade `ADreamWindGrassField` (engine cone + `M_DreamWindGrass`, bounds-based tip mask), the
  green tips lean with the global wind and ripple with the gusts;
* a `WindDebugPlane` (`M_DreamWindDebug`: hue = direction, brightness = speed) with the CPU
  `NS_WindArrows` grid on top, and the GPU `NS_Leaves` drifting over the field;
* four "hills" (cubes) and a baked `Lvl_DreamWindTest_WindTerrain` on `DreamWindGlobal`, so wind speeds
  up on the tops and steers around them;
* an `ADreamWindSourceActor` **fan lane** (a directional source with `Stretch 5`: a 35 m jet, arrow
  shows the push), a **vortex** source, and an `ADreamWindImpulseEmitter` that fires a radial burst
  every 4 s — all live in the editor viewport, no PIE needed;
* a roofless **hut** with a `Shelter` source (the grass inside stands still, the debug ground goes
  dark) and a two-wall **corridor** whose shelter is a 900 cm/s wind zone along it;
* a solid **wall** and, beside it, a **fence** tagged `DreamWind.PassThrough`: the wall drags a wind
  shadow across the grass behind it, the fence does not; the tall "hills" trail long shadows downwind
  (the terrain map is baked at 512, 39 cm texels).

Try: select `DreamWindGlobal` and drag *Wind ▸ Direction / Speed / Gust Strength*; type
`DreamWind.DebugDraw 3` for the 3D arrow field (`DreamWind.DebugDraw.Range 3000` to see more of it,
`.Color 1` for direction hues); toggle *Wind Volume* off on a grass instance to see what the analytic
layer alone gives; `DreamWind.Volume 0` to A/B the whole volume. The PlayerStart sits between the fan
lane and the hut, so PIE drops you in the middle of it.

The three demo actors (`DreamWindDemoActors.h`, `DreamWindImpulseEmitter.h`) are plain C++ and stay
usable in your own levels; none of the runtime depends on them.

## Architecture notes

* `Shaders/Shared/DreamWindShared.h` is the single source of the analytic model. It is written in the
  HLSL/C++ common subset and included by the compute shader, the Niagara DI template, the DreamShader
  `Function` blocks (via `#include` hoisting) **and** by C++ through `DreamWindHlslShim.h`. Value noise
  with analytic derivatives on an integer-hashed lattice; curl of the noise gives divergence-free
  turbulence; gust fronts are stretched across the wind and advected downwind.
* Sources are never *added*: consumers blend toward a source's velocity by its footprint weight, so a
  source can never accumulate beyond its own strength and the result is frame-rate independent. On the
  GPU the volume blends the *total* wind toward the source (residual toward `source − analytic`).
* The volume is re-based every frame by the update pass (semi-Lagrangian back-trace through the
  previous window), so following the camera costs no copy; cells that were outside the old window start
  from zero. Two levels share one shader: the far level runs the same pass with 4× voxels; consumers
  take the near level inside its window and the far level outside it. Only one world drives the shared
  render targets at a time (game worlds beat the editor world).
* The wind's part of the shared collection is ten float4 (`DreamWind_P0..P2`, `V0`/`V1` near window,
  `V2`/`V3` far window, `T0..T2` terrain map) in exactly the layout `DW_UnpackParams` / `DW_UnpackVolume` /
  `DW_UnpackTerrain` expect; the DI and the compute pass use the same packing. Parameter ids are derived
  from the names, so a regenerated collection keeps them.
* Every function of the shared header carries `DW_FN` — `inline` in C++ — because every C++ file that
  includes the header compiles the whole model; built one file at a time (a project's adaptive non-unity
  build, the test host) plain functions would be defined in each.
* Terrain: the map is a data asset of plain floats (`UDreamWindTerrainMap::Texels`), so the CPU sample
  reads the very numbers the GPU does; the subsystem uploads the active map into `RT_DreamWindTerrain`
  (a fixed 2D render target materials reference) whenever it changes. Height relative to a box-blurred
  copy = exposure → speed multiplier; the blurred slope, clamped, → steering toward lower ground. Texels
  with nothing under them count as the lowest ground seen, not as a pit.

## Performance guidance

* The wind is cheap; **World Position Offset is not** — it invalidates Virtual Shadow Map pages every
  frame. Set *World Position Offset Disable Distance* on foliage types / meshes, and note the ray-tracing
  flag is separate (`bEvaluateWorldPositionOffsetInRayTracing`).
* **Shadow strategy for wind foliage that casts shadows** (the engine's own knobs, all per component;
  `ADreamWindGrassField` exposes them under *Field ▸ Shadows*):
  * *World Position Offset Disable Distance* — beyond it WPO is not evaluated at all: far blades stop
    moving and stop invalidating VSM pages. Pick the distance at which the sway is no longer visible
    (a few thousand cm for grass, more for trees).
  * *Shadow Cache Invalidation Behavior* — `Auto` re-renders the pages under every moving blade each
    frame (correct, costs shadow-depth time); `Rigid` keeps the cached pages (shadows stop swaying,
    nothing is re-rendered — fine for grass whose shadow is a haze anyway); `Static` also ignores
    transform changes.
  * Ray-traced shadows: `bEvaluateWorldPositionOffsetInRayTracing` is off by default, so RT shadows
    show the un-bent silhouette and no BLAS is refitted; turn it on only for hero foliage.
  * Lumen: keep instanced blades out of the surface cache (see below).
* **Material cost, measured** (`MaterialEditingLibrary.get_statistics`, SM6, editor): a plain
  `BasicShapeMaterial` is VS 148 / PS 192 instructions. `M_DreamWindGrass` with the default switches
  (*Wind Volume* on, *Wind High Quality* off) is VS **427** / PS 247 — the whole material, i.e. cheap
  analytic sample + terrain fetch + tip mask + bend + flutter. Switch deltas: *Wind Volume* off → VS 381
  (−46: the two volume fetches and their masks); *Wind High Quality* on → VS 563 (+136: 2 gust + 2 curl
  octaves and vertical turbulence). `M_DreamWindDebug` (cheap sample + volume + terrain in the pixel
  shader, ~10 instructions of its own) is PS 318, so the wind function itself is ≈ 110–130 ALU with the
  volume, ≈ 70–80 without. Vertex ALU of that order is not what limits foliage; shadows and Lumen are.
* Prefer the cheap sample (3 noise evaluations, no texture) in vertex-heavy materials; reserve the
  full sample + volume for particles, cloth-like effects and hero foliage.
* Volume: 64×64×32 at 100 cm voxels covers 64 m × 64 m × 32 m for 1 MB and one pass; the far level
  (4 m voxels, 256 m) doubles both. Raise resolution or voxel size in the settings; the render targets are
  re-initialised in memory. Turn the far level off (`bVolumeFarLevel`) for interiors.
* Measured (i9-14900KF, editor viewport, `ProfileGPU`): near volume update 0.018 ms + far 0.022 ms +
  two 0.004 ms history copies ≈ **0.05 ms GPU** for both levels; `NS_DreamWindLeaves` GPU sim 0.015 ms.
  CPU (`DreamWind.Perf.CpuSample`): cheap sample **0.05 µs**, full **0.09 µs**; sources add a few ns each.
* **Lumen and instanced wind foliage.** Lumen's surface cache builds a card set per ISM instance and
  re-captures the ones near the camera as it moves; with thousands of small non-Nanite blades that is
  the full capture budget every frame (measured on the test map: 227–300 card captures, 33–70 ms GPU
  in `LumenSceneUpdate`, render thread stalled behind it, 18 fps while flying along the ground).
  `ADreamWindGrassField` therefore ships with *Affect Dynamic Indirect Lighting* and *Affect Distance
  Field Lighting* off on its instanced component (2 captures, 0.07 ms, 120 fps at the same spot).
  Do the same on your own grass/foliage components — the wind is not the cost, the surface cache is.

## Tests

`Tools/TestHost/Invoke-DreamDynamicWorldTests.ps1` builds the test host and runs everything headlessly
(see `Tools/TestHost/README.md`). `DreamWorld.*` covers the clock (run, events, jumps, transitions,
snapshots), the sun and moon (artistic path; Tokyo's solstice noon, the equinox, Sydney; the 2026 lunar and
solar eclipses for the moon's phase), the scheduler, the parameter ids and the view probe (a closed room, a
porch, the open; rays spread over the round, easing, teleports). `DreamSky.*` checks that a style's
day is continuous (the main light never jumps, sun and moon never light at once, exposure eases), drives a
sky actor in a game world (snap on jumps, easing while the clock runs, the night hand-over) and the indoor and
flash lighting. `DreamWeather.*`
covers the selectors (determinism, weights, persistence, seasons and hemispheres), the schedule (every channel
eases; clouds gather before a change and rain follows it), modifiers (fades, priorities, forced weather), zones
(falloff, priorities, the fixed order of equal priorities, and the weather map's records run through the shared
falloff on the CPU agreeing with the point query), events with hysteresis, the wind and the environment the sky
reads, temperature with height and snow above the freezing line, snapshots of modifiers, shelters, lightning
(strike counts, determinism, rods, the multi-stroke flash), the fall paths (a roof, the open, a shelter without
geometry, wind, the forward bias, spreading) and `DreamWeather.FX.*` the rain effects driven by real weather.
`Automation RunTests DreamWind.` — `DreamWind.Shared.*` are pure C++ over the shared model
(determinism, continuity across lattice cells and in time, calm wind, source blending incl. jets and
shelters, packing, terrain, occlusion: max-height atlas, tap levels, wall shadow both ways, holes/ripple),
`DreamWind.Perf.CpuSample` times the CPU sample, `DreamWind.Subsystem.TickAndImpulse` drives a real
subsystem tick with an impulse in whichever editor/game world is up.

## Verification (2026-08-18)

* Editor: subsystem ticks in editor and game worlds; MPC updates every frame; volume window follows
  the viewport camera; impulses expire; direction changes propagate within a tick.
* Materials and Niagara: `MF_DreamWindSample`, `M_DreamWindGrass`, `M_DreamWindDebug` compile;
  `NS_DreamWindDebug` (CPU) and `NS_DreamWindLeaves` (GPU) run — see `Saved/Screenshots` for the
  arrow grid following gusts and impulses.
* Shared header: compiles as C++ (UBT), HLSL SM5 (fxc) and SM6 (dxc).
* 2026-08-18 (later): far volume level active (`V3.w = 1`), terrain bake over the showcase map lands in
  the MPC (`T0/T1`), CPU sample on a baked ridge reads 1.4× the flat speed, the debug plane and the CPU
  arrow grid agree on the steered directions; 8/8 `DreamWind.*` tests green.
* 2026-08-18 (evening): shelters, jets and occlusion. CPU sample inside the hut 0 cm/s, just outside
  1337; corridor 900 along its axis; hill_1 (12 m cube) lee 119 vs 1250 in the open, inside 110, calm
  plume visible on the debug ground and in the grass; the solid wall shades the grass behind it, the
  pass-through fence does not; jet 20 m ahead of the fan reads the fan, 40 m ahead ambient. Generated
  MF, the DI template and the volume shader all pass fxc/dxc; 9/9 tests green.

## Layout

```
DreamDynamicWorld/
  Shaders/Shared/DreamWindShared.h        the wind model (HLSL + C++)
  Shaders/Private/DreamWindVolumeUpdate.usf
  Shaders/Private/NiagaraDataInterfaceDreamWindTemplate.ush
  Shaders/Private/DreamSkyStylized.ush    the stylized sky dome
  Shaders/Private/DreamSkyCloudSprites.ush  the cloud sprites' rectangles (generated by Tools/Art)
  SourceArt/Sky/            the horizon cloud sprites (Clouds/*.png) and their packed atlas
  Tools/Art/pack_cloud_sprites.py         cleans painted clouds into sprites, packs the atlas
  DreamDynamicWorld.uplugin
  Config/DefaultEngine.ini  CoreRedirects for the old /DreamWind mount point and MPC_DreamWind
  Source/DreamDynamicWorld  core: world clock, sun and moon, shared parameters, scheduler, snapshots, console
  Source/DreamSky           sky actor, style, lighting director
  Source/DreamWeather       weather channels, presets, selectors, modifiers, zones, queries, shelters, lightning,
                            fall paths, weather map pass
  Source/DreamWeatherFX     rain and snow around the view (Niagara; loads after DreamWeather)
  Shaders/Shared/DreamWeatherShared.h     zone falloff (HLSL + C++)
  Shaders/Private/DreamWeatherMap.usf     the weather map compute pass
  Source/DreamWorldEditor   time-of-day toolbar slider, DreamDynamicWorldSetup commandlet
  Tools/TestHost/           the test host template, its generator and the test runner
  Source/DreamWind          wind runtime: subsystem, settings, source component, global actor, volume renderer,
                            terrain map (+ occlusion), pass-through component, demo actors (source actor incl.
                            shelter box, grass field, impulse emitter)
  Source/DreamWindNiagara   UNiagaraDataInterfaceDreamWind
  Source/DreamWindEditor    Tools menu, asset setup, DreamWindSetup commandlet
  DShader/                  MF_DreamWindSample.dsf, M_DreamWindGrass.dsm, M_DreamWindDebug.dsm, Sky/M_DreamSky.dss,
                            Weather/M_DreamRainDrop.dss, Weather/M_DreamRainSplash.dss
  DFX/                      DreamWindForce.dfm, NS_DreamWindLeaves.dfs, NS_DreamWindDebug.dfs,
                            Modules/DreamFallPath.dfm, Effects/NS_DreamRain.dfs
  Content/MPC_DreamDynamicWorld  the shared parameter collection
  Content/Sky/                   M_DreamSky, DS_DreamSkyStylized, T_DreamCloudSprites, Maps/Lvl_DreamSkyTest
  Content/Wind/                  generated assets (RT, materials, Niagara) — regenerable
  Content/Wind/Presets/          DWP_Calm / DWP_Breeze / DWP_Storm (UDreamWindPreset)
  Content/Wind/Maps/             Lvl_DreamWindTest + its baked wind terrain
```

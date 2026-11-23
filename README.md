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
   weather map and presets, and the clouds' 3D noise. `-TestMap` also writes the sky test level;
   `-ForceStyle` / `-ForcePresets` write the class defaults over the existing style / presets;
   `-ForceCloudNoise` generates the noise again. The assets are
   committed with the plugin already.
3. Drop an **`ADreamSky`** in the level (remove the level's own directional lights, sky light and height
   fog), and for wind optionally an **`ADreamWindGlobal`**.

## Clock and sky

**World clock** (`UDreamWorldSubsystem`). World time is a double count of seconds in the world's own
calendar; real time times the time scale moves it (*Day Length Minutes*, default 24 = one real minute per
game hour). Sun and moon are a pure function of it: *Artistic* mode (sunrise and sunset hours, the sun's
height at midday, the moon kept opposite the sun if you like — the same every day; the stars turn with the
local sidereal time of that date and hour, so the constellations rise when they really do) or *Realistic* mode
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

**Shared render targets** (`DreamWorldSharedTargets.h`). The weather map, the five surface maps, the wind's
volumes and terrain map and the clouds' panorama are assets, one each for the whole engine, while every world
(the editor's, a play-in-editor one) keeps its own clock, weather, view and material parameter collection. One
world owns them at a time: a game world, else the first play-in-editor world (Simulate too), else the editor
world; a test's or tool's own world takes part by its kind once it asks. Every writer asks before it draws
(`DreamWorld::SharedTargets::FWriter`) and draws everything afresh when they come back to it (play in editor
ended). Without this the play session's ground flickered between its own maps and the editor world's.
`DreamWorld.SharedTargets 0` lets every world write them again, to compare.

**Sky** (`ADreamSky` + `UDreamSkySubsystem`). The actor owns the lights the sky drives — the sun (the
atmosphere sun, index 0) and the moon, a sky light that captures the dome, a height fog and an unbound post
process for exposure — and a dome drawn by `M_DreamSky` (`DShader/Sky/M_DreamSky.dss`, drawing in
`Shaders/Private/DreamSkyStylized.ush`): gradient, sun and halo, moon with its phase (its lit side shaded as the
moon's dust shades it, evenly bright to the limb at the full; the terminator soft over `MoonTerminatorSoftness`; the
dark side in the earthshine, `MoonEarthshine` of the lit side near the new moon and fading to none at the full,
`DDW_MoonLook`), stars, high cloud and rain shafts; the cumulus are drawn by the sky cloud renderer (below). A `UDreamSkyStyle` (default `DS_DreamSkyStylized`)
says how everything looks: colour and intensity curves keyed by the **sun's elevation** (so one style fits any day
length or latitude), the clouds, and the exposure from an EV100 curve — fixed (no eye-adaptation lag when time
jumps) or with eye adaptation held around it. An overcast dims the moon (`OvercastLightDimming`), and the sun too
where the clouds' own shadow does not reach: the sun light is left whole while the sky cloud renderer's shadow takes it
off the ground, so the same clouds do not darken it twice, but the shared parameters' main light (`DDW_MainLight`,
which materials and toon characters read without the light function) is dimmed as before. Translucent materials
lit by the engine's sun light see no light function either (forward-shaded translucency does not sample it), so
under an overcast they get the whole sun: a project's translucent material that should darken with the weather can
use an unlit or custom-lit model and light itself from `DDW_MainLight` (direction in rgb, illuminance in a, already
dimmed for the overcast) and `DDW_MainLightColor` instead.

The lighting eases while the clock runs — a toon ramp reading the main light never flickers across its
threshold — and snaps when the clock jumps. The directional lights turn only after the sun moved
`LightRotationThreshold` degrees (moving a light invalidates cached shadows); the sun is set black, not
hidden, at night, so it costs nothing and anything that asks "where is the sun" still finds it. After dark the
moon light takes over; when the moon is down a stand-in opposite the sun keeps a main light (stylized
default, `bNightLightAlwaysUp`).

The clouds are drawn by the plugin's own cloud renderer (`Source/DreamSky/Private/DreamSkyCloudRenderer.*`,
`Shaders/Private/DreamSkyCloudRender.usf`): per view, right after the base pass, a compute pass marches the field at
reduced resolution (`DreamSky.CloudRender.Downsample`, 2) with the shared cloud march (`DreamCloudMarch.ush`) —
skipping clear air by the cumulonimbus' distance bound over the layer, walking no longer than a billow near a
surface, each ray's steps offset per pixel and frame — a second pass blends the frames before, reprojected by the
clouds' depth (`DreamSky.CloudRender.History`, 0.9), and the clouds are laid over the sky pixels behind the scene,
under the sky's height fog as the engine fogs its own clouds; they take the dome's weather haze and lightning. Three
looks (`ADreamSky.CloudLook`, or the console's `DreamSky.CloudRender.Look`): **toon** (the surface, lit once by its
normal and its shadow toward the light and painted in the style's few tones), **realistic** (the density lit by
scattering: soft) and **painted** (the density painted in the style's lit, half and shade tones by the light through
it: hard-edged, opaque, with a silver lining near the sun, an anime background's clouds). The light reaches each
sample of the realistic look through the air as the atmosphere's transmittance table has it (DreamAtmosphere, the
same air the sky is made of) once it is built, an analytic air of the same coefficients before
(`DreamSky.CloudRender.Atmosphere 0` keeps to that). The air between the clouds is the dome's own sky
(`DSky_SkyBackdrop`: the physical sky through the style's grade, its gradient as far as `GradientBlend` says). The toon
and painted clouds' lit and shade colours come from the physical sky's light too: the sun's and the moon's light
outside the air through it to the clouds' base for the lit tone, the sky's ambient light (as the sky works out
`DDW_AmbientSky` / `Equator`) for the shade -- by default kept as bright as the style's `CloudLitColor` /
`CloudShadowColor` at every moment, their colour the light's (`bCloudMatchStyleBrightness`), so a low sun reddens the
clouds by itself -- their hue steered toward the style's own colours as far as `CloudLitToStyle` / `CloudShadeToStyle`
(`PaintedCloudLitToStyle` / `PaintedCloudShadeToStyle` for the painted look) say over the sun's elevation, their
brightness kept: none at noon; as the sun goes down the shade back to the style's blue-lavender (the complement of the
warm light) and some of the sunset's saturation off the lit side (more for the painted look), still warming with the
light; at night the style's moonlit grey-blue, whatever hue the night's light has -- then times the style's
`CloudLitScale` / `CloudShadeScale` curves (white; `PaintedCloudLitScale` / `PaintedCloudShadeScale`); `bCloudColorsAbsolute` goes back to the style's
curves as they are. The realistic and painted
looks take their settings from the style (`RealisticClouds`, `PaintedClouds`, and `CloudShadowReach`, how far their
shading toward the light reaches), the level's cloud overrides over them; on the actor, `CloudSunScattering` (2.5)
brightens the sun's and the moon's scattering in them (their multiple scattering loses light: at 1 a sunlit cumulus
comes out light grey) and `CloudShadowTracingDistance` (6 km) is how far each sample looks toward the sun. A map of
the optical depth toward the sun round the view shades them and casts their shadows on the ground (a light function
the sky puts on its sun, `M_DreamCloudShadow`): they take all of the sun off the ground under every look; the style's
`StylizedCloudShadow` lowers that between broken clouds under the toon and painted ones (say 0.55: a painted
background's cloud shadows lie light and warm), rising to all of it as the cover closes (an overcast casts no sun
shadows); the `GroundShadow` override sets it for any look. A panorama of them every fourth frame (`RT_DreamCloudPanorama`) is what the dome shows
in the sky light capture, so the sky light and reflections see them. Seen from above the layer's base the clouds
between the view and the ground are drawn over the scene too, just before the post processing: there they cover
whatever translucency stands in front of them (rain, particles, glass). It does not draw clouds in front of nearer
scenery seen from under the layer, nor the view flying into them (cloud volumes are for clouds close by). The dome
stands 1000 km out (`DomeRadius`), beyond any terrain. `DreamSky.CloudRender.Debug 1` shows where the clouds go into
the sky pixels, `2` the rays that found no cloud, `3` how each ray took the frames before (green its history, yellow
none this frame, red its history off screen, blue this frame alone); `DreamSky.CloudRender.GroundShadowReference <steps>`
works the ground shadow out by an even march per texel, a reference for the sun map's ground maps. The sun maps
(the optical depth toward the sun round the view, a near and a far cascade) are redrawn a slice a frame, the
cascades taking turns (`DreamSky.CloudRender.SunMapSlices`, 6: each redrawn whole every twelfth frame; 1 a whole
cascade a frame), and whole when the view moves a texel or the light turns a quarter of a degree (on the cascade's
turn), at once on a jump of the clock, and every frame in the cinematic mode.

The renderer needs Shader Model 5 (the desktop renderer, deferred or forward). Under the mobile renderer, and in the
editor's mobile preview, the sky is drawn without its cumulus — the sky itself, the high cloud and the rain shafts as
ever — and the log and the editor say so once. `DreamSky.CloudRender.Enable 0` leaves them out the same way: set it in
a device profile to have no clouds on a platform.

**Sky light.** The sky light takes its light from the dome captured in real time (`SkyLightSource`, by default),
or captured again only when the sky has changed enough (`CaptureWhenChanged`, as `bRealTimeSkyCapture` off), or --
for low end and mobile, with no capture at all -- from a cubemap: `Cubemap` a fixed one (`SkyLightCubemap`, the
engine's daylight cubemap if none) at the style's sky light intensity, or `TintedCubemap` one tinted by the sky's
ambient light and as bright as it (a plain white cube Setup makes, `T_DreamSkyWhiteCube`, if none: a neutral cubemap
takes the tint best), the ground below it the sky's ambient ground; `SkyLightCubemapScale` scales either.
`DreamSky.SkyLightSource 0..3` overrides it (a device profile or a quality setting).

**Flat clouds.** Where the renderer does not draw, the dome draws flat clouds instead (`DreamSky.Clouds.Flat`, -1): a
cheap sheet at the cloud base from the weather map's cover and darkness -- a few clouds, then many, then an overcast,
dark under a storm, a storm zone showing from afar -- shaped by three octaves of the cloud noise drifting with the
wind, lit through at the thin edges and shaded in the middles in the style's cloud colours, the side toward the sun
brighter, veiled by the air far off; and the sun takes a light function (`M_DreamFlatCloudShadow`) that casts the same
sheet on the ground. Nothing is marched: a few texture reads a sky pixel. `DreamSky.Clouds.Flat 1` draws them instead
of the renderer (for a low quality setting or a device profile), `0` never (no clouds where the renderer does not
draw, as before). The style's `FlatClouds` sets their `Opacity`, `Tones` (0 smooth; 2 to 4 flat tones, as the toon
look's), `Scale` and `ShadowStrength`, or turns them off; `ADreamSky::DrawsFlatClouds` says whether they are drawn.

The field (`Shaders/Private/DreamSkyCloudShape.ush`, from the 3D noise `T_DreamCloudNoise` that Setup generates) is
built the way UDS's volumetric clouds are. The weather map's cover picks the cloud type at each point: small
fair-weather cumulus, towering cumulus, stratocumulus, an overcast deck. A low-frequency formation of Worley cells,
cut at a threshold that falls as the cover rises, says where cloud may stand — whole clouds of every size, crowded in
places and sparse in others — and the type's vertical profile tapers it with height: small cumulus keep to the
lowest few hundred metres above the base (`CloudLayerHeight`, 0.6 km), towering cumulus climb the 4 km layer
narrowing and leaning downwind, an overcast fills it, its base lifted here and there; a breaking overcast cracks
along ragged, wandering lines. Billows erode it (`CloudErosion`; erosion only, it never adds cloud) — Worley swellings
gathered into clumps of every size, not equal balls — most on small clouds, hardly on an overcast's underside, and a
finer octave frays the edges near the view (`CloudDetail`). The clouds grow and fall apart as they drift
(`CloudFormationSpeed`: at 1 a cloud builds and dissolves over some 20 game minutes, 0 holds every shape), following
the world clock, so a given moment always shows the same clouds. Fair-weather cumulus follow the hour
(`CloudDiurnal`): fewer and flat in the morning, building into the tall cumulus of the mid-afternoon, sinking and
fraying toward evening. Under storm darkness the strongest cells climb out of the deck into cumulonimbus, up to 10 km, and
spread into anvils there, while the storm's base hangs low in broken rolls with ragged scud beneath; rain hangs
under a distant storm in grey curtains. The air between veils far clouds in
the sky's own colour behind them, thicker low down (`CloudVisibility`, `CloudHazeHeight`): a tower's base sinks into
the horizon while its top stays clear. Above it all lie thin cirrus streaks along the wind (`CloudHighCover`,
`CloudHighAltitude`). The toon clouds take the colour of the sky around them (`CloudSkyTint`: warm low in a sunset,
deep blue overhead) and catch the sun's glow beside it. The sun's halo spreads along the horizon while the sun is
low. When the sky light captures the dome it sees no stars and only `SunDiskInSkyLight` of the sun disk, so the
captured light has no hot spot.

**Cloud tones.** The toon and painted looks' tones are the style's to set, none fixed in the shaders. The toon look
(`ToonClouds`): the steps from the shade through the half tone to the lit tone by the light on a billow -- `Bands`, as
many as wanted, each where it changes, how softly and to which tone, or a free `ToneCurve` in their place -- the half
tone (`HalfToneMix`, `HalfToneTint`), an underside's own steps and deepest shade (`UndersideBands`,
`UndersideDeepTint`), how the light on a billow is reckoned (`KeyFacing`, `KeyUp`, `KeyHeight`, `KeyHeightPivot`,
`KeyLow`, `KeyRange`), the silver lining (`RimPower`, `RimStrength`, `RimFocus`) and how dark a storm leaves the lit side; its `Advanced` group has the rest -- how deep a cloud toward the light casts
its shade, the shade's own light (base, facing up, thin, high), the low sun's colour, an underside's glow, a deck's
sunlit top, where the silver lining comes on, how much of a storm's darkening the tops keep, how far clouds sink into
the sky's colour, and the size and tearing of the cracks in a thinning deck.
The painted look (`PaintedClouds`) adds its half tone, the light's falloff into a cloud, its rim, its shade at the base
and the least a base keeps, and in its advanced settings the low sun's colour, the sky's in the shade, the key at a
cloud's base, a deck's soft gradient, a storm's darkening and the base floor's reach; a sky modifier's *Painted
Clouds* blends to another set of all of these, as *Toon Clouds* does for the toon look. The defaults are the looks as they were. A sky modifier's *Toon Clouds* brings another set
of toon tones and blends to it as it fades in (the ramps sampled and mixed). The level's style overrides go over all of
it as over the rest of the style. The renderer takes them with its own parameters; no shared parameter, no texture.

**Aurora.** The dome draws the aurora as curtains of soft light over the auroral oval toward the magnetic pole
(north, or south below the equator), from a sharp, brighter lower edge some 95 km up fading out by 320 km, green
low down going to violet at the top. Where the curtains stand is drawn as thin ribbons that wander, fold and slowly
change shape (two copies of one smooth noise field drifting apart, lit where they nearly agree), stretched along the
oval and strongest where a broad field of activity is up; each view ray is walked up through the shell at fourteen
heights, so a ribbon becomes a curtain standing over it, striated with rays that reach higher than the rest. It keeps
this soft light under the toon and painted clouds too; `Aurora.bStylizedTones` (off by default) bands it into three
clean tones there instead, at `ToneStep1..3` with `ToneSoftness` and `ToneLevel1..2` (`DDW_AuroraTones`). Its
*Shape* settings (`DDW_AuroraShape`, `2`, `3`) set the ribbons' length, width and sharpness, the rays' spacing and
contrast, how fast a curtain fades upward, its lower edge and top, the edge band's brightness and the oval's width. The clouds
stand in front of it and it lights the sky light a little. It shows once the sky is dark (from
the end of the civil twilight), as present as the world's latitude makes it in the realistic celestial mode
(`Aurora.StartLatitude` to `FullLatitude`, 50 to 66 degrees by default) or as the style's
`Aurora.ArtisticPresence` says in the artistic one. The style's *Aurora* also sets its brightness, colours, speed,
and how far toward the pole the arc stands and how wide it is. A sky modifier's *Aurora* brings it anywhere (or
holds it off); `DreamSky.Modifier.Add Aurora` tries it. It costs some 0.16 ms of the base pass at 1080p while it
shows, nothing while it does not.

**Night glow and light pollution.** From sunset on the night sky has a faint glow of its own (the style's
`NightGlow.Airglow`, a little stronger low down), and a city's light climbs from the horizon
(`NightGlow.LightPollution`, 0 = the wilderness; its colour, and `Climb`, how high it reaches). Weather haze spreads
it further up and brighter, and the sky cloud renderer lights the clouds' bases with it (`CloudUnderLight`): an
overcast over a city glows warm from below. A sky modifier's *Light Pollution* sets it for a place (a modifier held
by a volume over a town, say) and its *Airglow* scales the glow; `DreamSky.Modifier.Add City` tries it.

**Eclipses.** In the realistic celestial mode the sky has its eclipses where the sun and the moon line up
(`Eclipse.bFromSunAndMoon`; the maths in `DreamSkyEclipse.h`). In a solar eclipse the dome draws the moon's disk over
the sun's at their true places (scaled to the drawn disks) and the sun's light goes with the covered share of its
disk. The sky, its light and the exposure hold, as the eye makes up for the failing light, until most of the disk is
gone (`Eclipse.DarkensFrom`, 80 %); toward totality they go to a deep blue twilight, the style's own with the sun
`Eclipse.TotalityTwilight` under the horizon, while the horizon all round glows orange (`Eclipse.TotalityRing`: the
far air outside the moon's shadow, still in the sun), only the brightest stars come out (`Eclipse.TotalityStars`) and
the corona shows, streamers of uneven length reaching two or three sun radii (`Eclipse.Corona`). In a lunar eclipse the earth's umbra crosses the moon at its
true place, turning it a dark copper, and the moon's light fails to `Eclipse.BloodMoonLight` of itself. A sky
modifier's *Solar Eclipse* or *Lunar Eclipse* sets one by its coverage in any mode (`DreamSky.Modifier.Add Eclipse`,
`BloodMoon`). Nothing is drawn, and nothing costs, without one.

**Light shafts.** Under broken clouds the sky cloud renderer draws the sunlight the air below them scatters toward the
view where their shadow lets it through: shafts falling through the gaps, seen whether the sun is in view or behind
it. Each ray of the view is marched through the air under the clouds' base, reading at every step the clouds' own
shadow on the ground (the texture the sun's light function reads) up the sun's direction, so the shafts stand where
the shadows on the ground are. The style's `LightShafts` sets how bright they are drawn (`Intensity`), the haze they
show in (`Density` per km at the ground, `ScaleHeight`, how much it scatters toward the sun, `Anisotropy`), how far
along the view (`Distance`) and how much the weather's haze thickens it (`WeatherHaze`); they come in as the cover
breaks (`CoverStart` to `CoverFull`: a clear sky casts none) and go with the sun under the horizon. A sky modifier's
*Light Shafts* scales them. The march takes the same samples every frame (`DreamSky.LightShafts.Samples`, 24),
nothing builds up over frames: a cinematic frame or a cut shows them as they are. `DreamSky.LightShafts` multiplies
them (0 leaves them out); `DreamSky.LightShafts.Debug 1` shows in grey how much of the sun the air along each ray
gets. They cost some 0.04 ms at 1080p, nothing under a clear sky or with the sun down.

**Lens flare.** With the sun in view the sky draws its lens flare over the scene after the depth of field, in one of
the style's `LensFlare.Preset`s: *Natural* (a soft glow, a faint ring and ghosts of a few colours along the line from
the sun through the middle of the view), *Anamorphic* (a long blue streak across the sun) or *Stylized* (flat hexagons
and a crisp ring, for the toon and painted looks); *None*, the default, draws none -- a lens flare is the project's
choice to turn on. It is as bright as the sun shows on screen over
`LensFlare.Threshold` (1 = white), times `Intensity`, in the sun's own colour times `Tint`: the brightness is measured
from the frame itself over the sun's disk, so clouds drifting over the sun, a wall in front of it or an eclipse weaken
it or take it away, and it fades as the sun leaves the view. Nothing builds up over frames. A sky modifier's *Lens
Flare* scales it; `DreamSky.LensFlare` multiplies it (0 = none) and `DreamSky.LensFlare.Preset` forces a preset. It
costs some 0.03 ms at 1080p, whatever draws the clouds.

**Rain on the lens.** With the style's `ScreenDroplets.bEnabled` (off by default: a lens effect is the project's
choice) rain drops gather on the camera's lens over the finished picture, after the upscale: beads in two sizes, each
bending the picture behind it like a small lens, living a while and drying off, and drops running down once the screen
is wet through. The lens gets wet while the view is out in the rain (not the snow) facing into it -- looking up catches
more than looking down, wind more still -- and dries off under a roof, indoors (the view probe) or once the rain stops,
over `WetSeconds` and `DrySeconds`; a cinematic frame or a jump of the clock shows it as it would be by then. How wet it
is, `UDreamSkySubsystem::GetScreenWetness`, is kept whether or not the drops are drawn, for a game's own use. The style
sets how many (`Intensity`), how big (`DropSize`) and how far they bend the picture (`Refraction`);
`DreamSky.ScreenDroplets <intensity>` draws them whatever the style says (-1 = the style's). They cost some 0.05 ms at
1080p while the lens is wet, nothing while it is dry.

**Frost on the lens.** With the style's `ScreenFrost.bEnabled` (off by default, as the drops) frost grows on the lens
in the same pass: in from the rim, the corners first, its front feathered with crystals and a frosted pane behind it
that blurs and whitens the picture, its facets' edges and grains catching the light. It grows while the view is out in
the snow in the cold -- from `StartsBelow` (0 C) to as much as it gets at `FullBelow` (-8 C), more in the wind -- over
`FreezeSeconds`; out in the cold once the snow is over it stays, fading over `LingerSeconds`; indoors (the view probe)
or in the warm it melts over `MeltSeconds`, into drops on the lens when those are drawn. A cinematic frame or a jump
of the clock shows it as it would be by then. How frosted it is, `UDreamSkySubsystem::GetScreenFrost`, is kept whether
or not it is drawn. The style sets how thick (`Intensity`), the crystals' size (`CrystalSize`), how much it blurs
(`Blur`) and bends (`Refraction`) the picture, and its `Tint`; `DreamSky.ScreenFrost <intensity>` draws it whatever the
style says (-1 = the style's). It costs some 0.2 ms at 1080p while thick, nothing while the lens is clear.

**Heat haze.** On a hot day what lies far off near the horizon wavers, rising -- not on the lens but in the air, so the
style's `HeatHaze` is on by default. It is drawn in the same pass from the scene's depth: within `BandDegrees` (2.5)
of the horizon, from `NearDistance` (50 m) out to full at `FullDistance` (400 m), and locked to the world's directions,
so it stands where it is as the view turns. How strong, `UDreamSkySubsystem::GetHeatHaze`, follows the air's
temperature from `StartsAbove` (24 C) to full at `FullAbove` (36 C), the sun's height, a clear sky and dry air; the
wind stirs it away. `Intensity` sets how far it shifts the picture (1 = some 2.5 pixels at 1080p), `Speed` how fast
it shimmers; `DreamSky.HeatHaze <intensity>` draws it whatever the style says (-1 = the style's).

**Stars.** The style's `StarField` picks the dome's stars. *Catalogue*, the default, is the real sky's: the some 9100
stars of the Yale Bright Star Catalogue, 5th revised edition (Hoffleit & Warren, 1991), down to magnitude 6.5, each where
it stands for the clock's latitude and local sidereal time (the shared parameters' `DDW_StarPole`, the north celestial
pole in the world, and `DDW_StarEquinox`, the March equinox), each as bright as its magnitude -- `StarContrast` sets how
much brighter each magnitude is drawn (1 = 2.512 times, as in the sky; 0.8 by default keeps the faint ones in sight) --
and in its own colour from its B-V index; the style's star intensity curve and `StarTwinkle` apply as before (`StarTwinkleSpeed`, `StarSize` and, for the
stylized scatter, `StarDimmest` too, `DDW_StarLook`), and an
eclipse's totality still leaves only the brightest. *Stylized* is the scatter of stars of `StarDensity` the dome drew
before (`DreamSky.StarField 0|1` picks either whatever the style says). The stars are read from a small map, `T_DreamStarMap` (512 x 512 cells on an octahedral map of the sphere, the
brightest star nearest each), which Setup builds from the catalogue's file: `-StarCatalogue=<path>`, by default
`<project>/Saved/StarData/bsc5.dat.gz` (the gzipped file as the catalogue is distributed, e.g. by the Harvard-Smithsonian
Center for Astrophysics' Telescope Data Center or the CDS as V/50); with no file there Setup leaves the map as it is, and
a file that does not read as the catalogue (far from its 9110 records, or places and magnitudes out of their ranges) is
an error and leaves it as it is too. The catalogue's file itself is not part of the plugin; the map is.

**Milky Way.** Behind the stars the dome draws the Milky Way on the same celestial sphere, so it turns with them and
stands where it does for the latitude, the season and the hour (low in the south on a northern summer night with its
bright centre in Sagittarius, the fainter Perseus and Auriga stretch on a winter one): a band widening and brightening
toward the centre with the bulge round it, clumped into star clouds, split by dust lanes and the Great Rift, warm white
toward the centre and cooler along the arms, and the Magellanic Clouds in the south. It is made from noise, no
photograph: Setup bakes it into `T_DreamMilkyWay` (2048 x 1024 in galactic longitude and latitude, the dust's depth in
its alpha). It shows only on a dark night -- from the sun at `ShowsBelow` (-10 degrees) to all of it by `FullBelow`
(-18) -- and the moon's light (`MoonWash`: a full moon high up leaves some 8% of it), a city's light (`CityWash`: the
dome's light pollution that leaves a third), haze, fog and cloud wash it out; `UDreamSkySubsystem::GetMilkyWayVisibility`
says how much shows. It looks the same under every cloud look. The style's `MilkyWay` sets how bright it is
(`Intensity`, on the stars' night intensity curve), its `Contrast`, how dark its `Dust` lanes are, its `Saturation` and
`Tint`; a sky modifier's *Milky Way* scales it. It is read in the dome with the stars, one texture sample, nothing
while it does not show.

**Rainbow.** In the same pass, the rain beyond shows a rainbow round the point straight away from the main light: the
first bow violet inside at 40.6 degrees to red outside at 42.3, a glow inside it, the fainter second bow (`SecondBow`,
0.43) at 50.4 to 53.5 with its colours turned about, and the darker sky between the two. It shows while there is rain
in the air -- as it falls, and for `LingerSeconds` (90) after -- with the light lower than 42 degrees and not behind
the clouds (a full overcast hides it), in the rain from `NearDistance` (20 m) to fully at `FullDistance` (250 m): so
mostly as a shower clears in the low sun of the morning or the evening, and as a faint moonbow by a bright moon. It
is the air's own light, so the style's `Rainbow` is on by default; `Intensity` sets how bright, `Width` how far its
colours spread, `Patchiness` how broken it is. How strongly it shows is `UDreamSkySubsystem::GetRainbow`;
`DreamSky.Rainbow <intensity>` draws it whatever the style says (-1 = the style's).

**The sun and the moon larger near the horizon.** The disks are drawn larger as they near the horizon -- the moon
illusion, drawn: the style's `SunDiskScale` and `MoonDiskScale` are curves over each one's elevation, times
`SunDiskSize` / `MoonDiskSize` (by default half again on the horizon, as they are from some 20 degrees up), the same at
any latitude and season. Only the disk as seen grows: its glow with it (its exponent over the scale squared), an
eclipse's corona and a lunar eclipse's shadow on the moon, the moon's corona and the lens flare; the lights' source
angle, and so the shadows' softness, stays. A sky modifier's *Disk Scale* turns both toward its own.

**Clouds clearing round the sun and the moon.** With the sun low the clouds round it thin out, its disk and the glow
about it showing through, and the moon's likewise: each ray's cloud is eaten back from its edges and its volume
thinned as near the light as `CloudClearing.Radius` (14 degrees) and `Exponent` (2, a soft bell) say, so the clouds
shrink and fade toward the light rather than a hole opening -- for the toon, realistic and painted clouds, the high
cloud and the flat clouds alike (the light's own shading and the shadows are left as they are). How strongly is a
curve over the light's elevation, `SunStrength` and `MoonStrength` (by default plain with the light low, almost none
overhead); a sky modifier's *Cloud Clearing* turns all four toward its own. `DreamSky.CloudClearing <scale>` scales it
whatever the style says (0 to compare; -1 = the style's).

**Halos and the corona.** Under a sheet of high ice cloud the dome draws the 22 degree halo round the main light (the
sun, or the moon by night), red on its sharp inner edge, running out to a whitish fade (`Radius`, `Width`), and the
sun dogs either side of it at the light's height, red toward it with a white tail outward, moving out as the light
climbs (22 degrees on the horizon, 29 at 30 degrees up) and gone past some 55 degrees; the sheet milks the sky a little
round them (`Milkiness`). In thinner cloud the moon has a corona, its bluish-white glow ringed in brown-red and blue,
the droplets' diffraction (`CoronaRadius`, the first red ring, 2.5 degrees, or two and a half times the drawn moon's
radius if that is larger). They are in the light the high cloud is lit
by, brightest in its veils, the same under every look. The style's `Halos` sets when they show by the high cloud's cover
(`HighCoverFrom` 0.45 to `HighCoverFull` 0.8 for the halos, `CoronaCoverFrom` 0.4 to `CoronaCoverFull` 0.7 for the
corona: the style's own thin cirrus, 0.35, shows neither) and how bright (`Intensity`, `SunDogs`, `Corona`); a sky
modifier's *High Cloud Cover* brings a sheet over (the showcase's clear morning has one from 8 to 9). A closing
overcast hides them.

**The earth's shadow.** After sunset the low sky away from the sun lies in the earth's own shadow: a blue-grey band
along that horizon, rising to some twice the sun's depression as it sinks, and over it the pink Belt of Venus, air still
in the reddened sun; gone by some 9 degrees under. The physical sky has them of itself (the sky table's walk leaves the
air in the planet's shadow unlit), so they show whenever the grade's `GradientBlend` is 0. Over the style's own
gradient the style's `EarthShadow` draws them as far as the gradient shows: `ShadowTint` and `BeltTint` multiply the
gradient, `Height` is the shadow's top in sun depressions (2, as the physical sky's), `Strength` how strongly.

**Noctilucent clouds.** In the deep twilight of high-latitude summer nights thin silver-blue clouds some 82 km up shine
low over where the sun went, lit long after it has set on the air below: the dome draws them on a shell that high,
where the sun's ray to them still clears the lower air, in bands (`BandSpacing`, 50 km) broken and wavering, with
billows across them (`BillowSpacing`, 9 km), drifting (`Drift`, 0.05 km/s), brightest toward the sun and denser toward
the horizon. In the realistic mode they come from `StartLatitude` (45) to full at `FullLatitude` (52), fading out by
`EndLatitude` (72), north or south, within `SeasonFullDays` (35) of the hemisphere's midsummer and gone by
`SeasonEndDays` (65); in the artistic mode as `ArtisticPresence` says. They show with the sun from 4 to 17 degrees
under, in full from 7 to 13 (`UDreamSkyStyle::NoctilucentTwilight`), as bright as the style's `Noctilucent.Brightness`
in the dome's measure, the same under every look; a sky modifier's *Noctilucent* brings them anywhere (the showcase's
evening has them from 17:30 to 20).

**Meteors and satellites.** On a dark night the dome draws meteors: short streaks along great circles, brightest at
the head with the trail fading behind, flaring and dying out within a second -- sporadic ones from anywhere
(`SporadicPerHour`, 8), and the yearly showers by the world's date streaking away from their radiant among the stars
(the Quadrantids, Lyrids, Eta Aquariids, Perseids, Orionids, Leonids and Geminids, each at its peak date and radiant,
falling off over `WidthDays`; the rate their zenithal hourly rate times how high the radiant stands, none with it under
the horizon). `RateScale` (3) multiplies every rate -- real ones are sparse for a game -- and a sky modifier's *Meteors*
scales them again (the showcase's clear night has a storm, ten times over). Satellites the sun still lights after
dusk cross on great circles some 550 km up in about five minutes (`SatellitesPerHour`, 12), fading into the earth's
shadow partway across, as a real pass does. `Brightness`, `Color`, `TrailLength` and `SatelliteBrightness` set how they
look; they are drawn in real seconds, behind the clouds and the moon, not in the sky light.
`UDreamSkySubsystem::GetMeteorShower` says which shower is active on the date and how strong, `GetMeteorRate` how many
meteors an hour are drawn now; the style's `Meteors.Showers` is the list. `DreamSky.Meteors <scale>` scales the rates
whatever the style says (100 fills the sky to look at; -1 = the style's).

**Per level.** The actor's panel shows only what is worth setting: the style, the clock, the clouds, and this
level's **overrides**, in four groups — Clouds (cover, scale, base altitude, drift, how fast they form, the hour's
effect, erosion, detail, cirrus, storm darkening, visibility, and the realistic and painted looks' extinction, glow,
base glow, painted tones, edge softness and ground shadow), Sun
and Moon (intensity, source angle, shadow distance and cascades, contact shadows, volumetric scattering, the
disks), Sky Light and Fog (intensity, fog density, falloff, opacity, start, volumetric fog) and
Exposure (mode, compensation, the eye's range and speed). Tick a setting to use it; the style stays the preset
other levels share. The components the sky drives are still in the component tree, though it rewrites most of
what they hold every frame.

**Levels saved with the older clouds.** The sky cloud renderer now draws every sky's clouds: the dome's own toon
clouds and the engine's volumetric cloud layer are gone, and with them `ADreamSky.Clouds`, `DreamSky.Clouds`,
`VolumetricCloudMaterial`, `DreamSky.CloudMaterial`, `VolumetricLayerThickness`, `VolumetricTracingDistance`,
`VolumetricSampleCountScale`, `VolumetricBaseSkyOcclusion`, `VolumetricShadowResolution`, `VolumetricShadowSamples`,
`bVolumetricHalfResTracing`, the `VolumetricClouds` and `CloudsBelow` components, `M_DreamVolumetricCloud`,
`M_DreamVolumetricCloud_Painted` and `M_DreamSkyBelow`. A level saved before is read once as it loads, and the log says
so: toon clouds become the toon look; volumetric clouds the realistic or the painted look, after the material they had.
A material of the level's own becomes the look its name suggests (painted if "Paint" is in it), with a warning that its
values were not carried over: set them in the style's `RealisticClouds` or `PaintedClouds` (or the level's cloud
overrides). The painted material's `EngineLight` (how much of the engine's own lighting showed under its tones) has no
counterpart: the renderer lights the painted clouds by its own march alone. `VolumetricSunScattering` and
`VolumetricShadowTracingDistance` are now `CloudSunScattering` and `CloudShadowTracingDistance`, and a saved value is
read into them; the materials' `ShadowReach` is the style's `CloudShadowReach` (60 km). The sun override
`CloudShadowStrength` and the style's `CloudShadowExposureLift` went with the engine's clouds: a level that adds an
engine cloud layer of its own sets its shadow on the sun light itself. Save the level once to keep it in the new form. Console lines or device profiles that set `DreamSky.Clouds` or `DreamSky.CloudMaterial` do nothing
any more.

**Indoors.** The clock probes the view (`GetViewOcclusion`): rings of rays around the camera, a few a frame, a
full round every 0.35 s, tell how enclosed it is (*Interior*), whether a roof is overhead (*Overhead*) and which
sides are closed. Indoors the sky lifts the exposure (`InteriorExposureLift`, 2.5 EV: a room lit only through its
door and windows, with little bounced light, is still dark at 1) and raises the sky light (`InteriorSkyLightScale`,
2.5): with Lumen the sky light is occluded, so what the doors and windows let in brightens and the corners stay dark.
Without Lumen nothing bounces in; `bInteriorFill` (off by default) then adds a weak fill that ignores occlusion
(`InteriorFill` of the outdoor sky light, and as much on the shared ambient gradient). Neither lights a room with
small windows by itself: place lights in it, as a level would anyway. The sun and moon can be scaled indoors too;
`DDW_Interior` carries how far the view is indoors to materials. Settings (channel, reach, round length, smoothing)
in *Project Settings ▸ Dream Dynamic World ▸ View Probe*.

**Sky modifiers** (`UDreamSkyModifier`, a data asset). A change to the sky that comes and goes while the game
runs: a cave's gloom, a cursed red sky, a dream. Tick what it changes: the sun's and moon's intensity and tint,
their disks' brightness, the sky light, the ambient tint, the exposure (stops), the fog's density and colour, the
haze, the dome's tint and saturation, the stars, and the clouds' cover, darkness and tones. Intensities scale,
tints multiply, the exposure is offset, the cover, darkness, haze and fog colour are approached; part way (an
alpha below 1) each goes that share of the way. The cover and darkness reach the sky cloud renderer's clouds
wherever the weather map gives them as well (`DDW_SkyModClouds`). A modifier can also carry a post process,
blended in with it.
In C++ or Blueprint, on `UDreamSkySubsystem`: `AddModifier(Modifier, FadeInSeconds)` and
`RemoveModifier(Modifier, FadeOutSeconds)` fade it in and out (a negative time takes the asset's own, with its
easing); `SetModifierAlpha(Modifier, Alpha, Seconds)` holds it at a strength something else drives (a player's
distance to a place); `ClearModifiers`, `GetModifierAlpha`, `GetModifiers`. Several stack: the higher `Priority`
goes on later. They go on over the eased lighting, before a lightning flash. The stack is kept in world
snapshots (`FDreamWorldSnapshot`), so a save restores it mid-fade. Console: `DreamSky.Modifier.Add <asset path>
[seconds]`, `.Remove`, `.Alpha <asset> <alpha> [seconds]`, `.Clear`, `.List`; instead of a path, `Gloom`,
`Crimson` or `Clear` take a ready-made one to try. A weather preset can carry a modifier too (its
*Sky Modifier*): the sky then holds it at that weather's weight at the view, apart from the scripted stack and
not saved with it (the weather gives it again).

**By condition.** The sky actor's *Post Processes* list blends post processes in by a condition (a cold grade on
snowy nights, a warm one at dusk, a soft bloom in fog), and its *Conditional Modifiers* list holds sky modifiers
the same way. A condition (`FDreamSkyCondition`) is any of: an hour window (it may run past midnight, faded at
each end), weather values at the view within a range with soft edges (cloud cover, darkness, rain, snow,
precipitation, fog, dust, wind, lightning, temperature), and indoors or outdoors (the view probe); the parts
multiply. Each entry's weight follows its condition over its *Blend Seconds*, with the clock stopped too, and
cuts on a jump of the clock (and every frame in cinematic updates). `GetPostProcessWeight(Index)` reads one; a
conditional modifier is not kept in snapshots (its condition gives it again). `DreamSky.Conditions.Demo [Off]`
gives the level's sky a rain grade and a blue night to try them.

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
weather, a thunderstorm zone 35 kilometres away, and a camera looking south-west so the sunset and the storm
are in frame.

## Weather

Drop an **`ADreamWeather`** in the level to turn the weather on (without one the sky keeps to its style).

**Channels.** The weather is a set of named numbers (`Dream.Weather.Channel.*` gameplay tags): cloud coverage
and darkness, precipitation and snow ratio, fog, dust, wind, lightning, temperature offset, wetness, snow cover,
freezing rain. **Freezing rain** (`Dream.Weather.Channel.FreezingRain`) is the share of the precipitation's rain that
is supercooled, 0..1: what falls near freezing turns to snow whatever the weather says, all but that share, so a
freezing rain stays rain below freezing (the rain's particles and sounds as any rain); what it does to the ground is
the surfaces' (it glazes what is frozen). The snow ratio channel itself still wins: a weather that says snow snows.
A project adds its own (ash, spores, miasma) in *Project Settings ▸ Dream Weather ▸ Custom Channels*; every
query, preset, zone and modifier handles them like the built-in ones. Each channel has its own transition time
and **lead**: clouds and wind start changing before the weather turns, rain and lightning follow, the ground
lags — a change of weather reads as coming.

**Schedule.** The actor's *Selector* chooses the weather over time — `Fixed`, `Sequence` (a scripted cycle),
`Random` (weighted, with persistence) or `Seasonal` (weights per season, either hemisphere). Every selector is a
pure function of the world time and seed: the same moment has the same weather on every machine, after every
load, in every replay, and `GetForecast` is just the function evaluated ahead. Presets (`UDreamWeatherPreset`)
hold channel values; the plugin ships `WP_Clear / Cloudy / Overcast / Rain / Storm / Snow / Fog / Dust / Frost /
FreezingRain / FreezingFog`. A preset
can name a *Sky Modifier* (see *Sky modifiers* above): the sky takes it on as far as that weather is in effect
where the view is, through the schedule's transitions, forced or pushed presets and zones alike, weighed by the
rules the channels blend by (an Override pulls what is under it back by its weight; the other blends add theirs).
An influence of your own brings a preset the same way by overriding `GetWeatherInfluencePreset`.

**Bending it.** `PushModifier` (a scripted storm, a spell that brings rain: channel values or a preset,
Override / Add / Multiply / Max / Min, a weight, a priority, real-time fades) and `ForceWeather(Preset)` (a second
forcing blends in over the first, which holds until it has arrived: the schedule never shows through between).
**Zones** (`ADreamWeatherZone`, or `UDreamWeatherZoneComponent` on anything) give a region weather of its own: a
box, circle or polygon footprint, a blend distance, a priority (equal priorities apply in a fixed order, never by
registration order). A **polygon** is any outline seen from above, up to 64 points in the zone's frame (`Points`,
`SetPolygonPoints`), or a closed spline: `ADreamWeatherOutlineZone` takes its footprint from its `Outline` spline, a
point every `OutlineSpacing` (call `UpdateFootprint` after moving the spline at run time; a spline tagged as an
outline is never taken for a storm's path). The blend runs out from the outline's nearest edge, so a snowfield with a
bay cut into it stays clear in the bay. **The footprint is where the zone is at full strength**: the blend lies
outside it, fading to nothing `BlendDistance` beyond the outline (half strength at half of it). The weather map, the
precipitation, the sounds, the surfaces and `DreamWeather.MarkEdge` all follow it, so a snow zone's snow reaches past
its outline: given time to settle, snow lies nearly as deep in the blend as inside. To keep the weather close to the
outline, shorten `BlendDistance`. A zone's channels are the weather's, so its **`Temperature` is an offset** on
the climate's temperature for the date and hour, not a temperature: a snowfield on a midsummer afternoon (some 25 °C)
needs about −30 to freeze, and `DreamWeather.MarkEdge Air` (below) rings where it does. Anything else can bend the
weather where it is by implementing `IDreamWeatherInfluence`.

**Queries and events.** `SampleWeather(Position)` — every channel there, rain and snow (precipitation turns to
snow near freezing, so a mountain top snows in the valley's rain; a freezing rain's share stays rain), the temperature (climate, season, hour,
height, the weather's offset) and `Dream.Weather.State.*` tags for gameplay conditions. Events:
`OnWeatherChanged`, `OnPrecipitationStarted / Stopped` (with hysteresis), `OnWeatherTagsChanged`. The modifiers
are saved in the world snapshot; the schedule needs nothing saved.

**What it drives.** The sky (cloud cover, a greyer and dimmer sky under cloud, more fog, an exposure lift), the
wind's speed and gusts, and the shared collection (`DDW_Weather0/1` at the view). The zones are also drawn by a
compute pass into a **weather map** around the camera (`RT_DreamWeatherMap`); the sky material looks the clouds
up in it where each direction meets the cloud layer, so a storm zone's dark clouds show from afar. The zone
falloff is one function (`Shaders/Shared/DreamWeatherShared.h`) compiled for the weather map, the surface maps and
the CPU queries: what the sky shows over a valley is where the ground gets wet and what gameplay reads there. A
polygon's points follow the zone records in the maps' zone buffers. `DreamWeather.MarkEdge [channel] [radius m] [step
m] [level] [post height m]` puts red posts on the ground where the point queries cross a level of a channel around
the view (`DreamWeather.MarkEdge off` takes them away), to see a zone's edge as gameplay has it beside the clouds and
the ground. The channel `Air` is the air's temperature at the base height rather than the `Temperature` channel (an
offset on the climate's hour). In a warm season a cold zone's snow lies where the air stays below freezing
(`DreamWeather.MarkEdge Air 5000 40 0.5 150` rings the showcase's snowfield); where it freezes all round, snow lies
wherever any falls (`DreamWeather.MarkEdge Precipitation 40 0.5 0.03` in the surface test level). Rain, snow and
lightning are held to what
the clouds allow, after every preset, modifier and zone (*Project Settings ▸ Dream Weather ▸ Clouds*): no
precipitation below 75% cover, in full from 90% (`PrecipitationCloudCover`), and lightning only from a dark, closed
cover (`LightningCloudDarkness` 0.45–0.75, `LightningCloudCover` 0.85–0.97). A change of weather therefore stops
raining before its clouds break up, and starts only once they have closed in.

**Shelter.** Every query carries an *Exposure* (0 under a roof .. 1 in the open) and `RainHere / SnowHere`,
the rain and snow that actually land on the point. Shelters are boxes (`ADreamWeatherShelter`, or
`UDreamWeatherShelterComponent` inside a house, under a bridge, in a cave mouth) or anything implementing
`IDreamWeatherShelterProvider`; the level's own roofs count too, from the wind's runtime terrain map (or a baked one,
`ADreamWindGlobal`, where the runtime map has not looked yet), and rain blown by the wind reaches in under the
windward eaves. `IsSheltered(Position)` for gameplay.

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
falls along its path, from that path's own start to its end at the speed it was traced with (a gust after the trace
never slants it through a roof), and dies where it lands, and splashes open where landed paths end. No drop queries
collision, no rain falls indoors, and the splashes land on what the rain lands on. Amounts, fall speeds, wind
drift and the paths are in *Project Settings ▸ Dream Weather Effects*; an effect only lives while its
precipitation falls. The weather hands every frame to listeners through `UDreamWeatherSubsystem::OnWeatherFrame`.

Console: `DreamWeather.Set <preset> [seconds]`, `DreamWeather.Clear`, `DreamWeather.Forecast [hours] [step]`,
`DreamWeather.Dump`.

### Sound

Module `DreamWeatherAudio` (needs the engine's MetaSound plugin). `UDreamWeatherAudioSubsystem` plays the weather at
the listener — the view the world is looked at from — every frame the weather runs:

* **Voices.** Wind (the wind field's speed at the listener), whistling gusts (a strong, gusty wind), light and heavy
  rain (crossfading with the amount), rain on a roof, rain on leaves, falling snow, blowing dust. What the rain
  lands on comes from the rain's fall paths ending near the listener (`FDreamWeatherFallPaths::GetSurfaceShares`):
  a hit on something tagged `DreamWeather.Leaves` (or painted foliage) is leaves, one tagged `DreamWeather.Roof`, a
  shelter box, or any surface a metre above the view is a roof, the rest is ground.
* **Thunder.** Every `OnLightningStrike` is heard after its `ThunderDelay`: a clap (`ThunderNear`) closer than
  `ThunderNearDistance` (3 km), a rumble (`ThunderFar`, or the clap low-passed) out to `ThunderMaxDistance` (15 km),
  quieter and duller with distance, from the side it struck on. A near clap ducks the wind and the rain for a
  moment (`ThunderDuckDb`, −4 dB, back over `ThunderDuckSeconds`, 1.5 s; `bThunderDucking`), so thunder hits
  whatever the recordings; far thunder ducks nothing.
* **Walls and roofs.** The view probe (`GetViewOcclusion`: how enclosed, a roof overhead, how closed each side is)
  muffles outdoor sound (a low-pass from 20 kHz down to `MinCutoffHz` in a closed room), quietens it (to
  `InteriorMinVolume`) and moves it toward the open sides: inside the hut the rain is dull, in its doorway it comes
  from the door. The roof over the listener is not outside — it drums at full loudness — and a canopy is not a roof.
  **Your own outdoor sounds** (traffic, a river) get the same: give them the sound class `SC_DreamOutdoor` (or a
  child of it); the subsystem pushes a sound mix that sets the class's volume and low-pass. Everything the weather
  plays is in `SC_DreamWeather`; parent it under your SFX class.
* **Ambience.** A `UDreamAmbiencePreset` points at a MetaSound that makes a place out of the hour, the weather, the
  wind and the season; the subsystem gives it, all 0..1, `TimeOfDay`, `Day` / `Twilight` / `Night`, `Spring` /
  `Summer` / `Autumn` / `Winter` (blending across the turn of a season; the southern hemisphere from the clock's
  latitude), `Year`, `Cloud`, `Rain`, `Snow`, `Fog`, `Dust`, `Lightning`, `Wind`, `Gust`, `Interior`, and the
  occlusion `Left` / `Right` / `Muffle`. `SetAmbience(Preset, FadeSeconds)` crossfades; *Default Ambience* plays
  otherwise. The plugin ships `MS_DreamAmbienceForest` / `DAP_DreamForest`: a random bird every couple of seconds
  by day, insects at night, wind in the trees, all quieter in the rain.

**Sounds are slots.** *Project Settings ▸ Dream Weather Audio ▸ Sounds* has one slot per voice (`Wind`, `WindGust`,
`RainLight`, `RainHeavy`, `RainOnRoof`, `RainOnLeaves`, `Snow`, `Dust`, `ThunderNear`, `ThunderFar`) and a map of the
ambiences' named inputs (`Birds`, `Insects`, `TreeWindLight`, `TreeWindHeavy`); a `UDreamWeatherSoundSet` asset
(*Sound Set*, or `SetSoundSet` at run time) overrides the slots it fills. **The plugin ships no recordings**: every
slot is empty until the project fills it. A slot takes several sounds (a loop keeps the one the world seed picks,
thunder draws one per strike) and a volume. A sound wave is played through `MS_DreamWeatherVoice` (loops it, eases
the gains, low-passes it); a MetaSound source of your own is played as it is and receives, all 0..1, `Left`,
`Right` (gains after walls and placement), `Muffle`, `Intensity` (before walls), `Gust`, `Distance` (thunder) and
`MinCutoff` (Hz); any other sound gets the component's volume and low-pass.

The numbers are worked out without an audio device too (a server, `-nosound`): `GetVoiceMix`, `GetOutdoorOcclusion`,
`GetPendingThunder` and `GetAmbienceParams` for gameplay and tests. `SetMuted` fades everything out and back.
`DreamWorldPlaySetup` (below) builds the graphs, the preset and the two sound classes.

### Quality levels

Dream Dynamic World follows the engine's scalability: each group of its features takes the level of the engine group it
belongs with -- the sky (`sg.EffectsQuality`), the surface maps (`sg.TextureQuality`), the weather's particles
(`sg.EffectsQuality`), its volumetric fog (`sg.ShadowQuality`) and the wind (`sg.EffectsQuality`) -- or Dream Dynamic
World's own, `r.DreamWorld.Quality` (0 low .. 4 cinematic, -1 the engine's; `UDreamWorldQuality::SetQualityOverride`
from a settings menu). When a level changes the world clock sets the console variables its group maps to, at the
scalability's priority, so anything set by hand (the console, an ini, a device profile) still wins; switching while the
game runs swaps them in place -- the clouds keep drawing, their history starting afresh, nothing goes blank.

| Level | Clouds | Light shafts | Sky extras (`DreamSky.SkyDetail`) | Particles | Volumetric fog |
|---|---|---|---|---|---|
| Low | the flat clouds (`DreamSky.Clouds.Flat 1`) | off | none (no aurora, halos, meteors, noctilucent clouds) | 35% | off |
| Medium | a third resolution, 96 samples, sun maps in 8 slices | off, 12 samples | aurora and halos; no meteors, satellites or noctilucent clouds | 60% | off |
| High | half resolution, 128 samples, 6 slices | on, 16 samples | all | 85% | the sky's |
| Epic | half resolution, 160 samples, 6 slices (as tuned) | on, 24 samples | all | 100% | the sky's |
| Cinematic | full resolution, 256 samples, the sun maps whole | on, 48 samples | all | 100% | the sky's |

The wind takes its level from `r.DreamWind.Quality` (-1 = as above). The surface maps take theirs from
`r.DreamSurface.Quality` (-1 = as above) as the world starts -- their state maps at half their size at low and medium,
three quarters at high, as authored above -- and keep that size while the game runs: a level changed then takes effect
at the next load (resizing would drop the wetness and snow they have gathered, a visible jump); `UDreamWorldQuality::GetLevel` gives any group's, and `QualityTable` the console variables
each group's levels set.

### Calendar and events

The realistic sky can stand at a built-in city (`FDreamCelestialSettings::City`: Tokyo, Beijing, London, New York,
Sydney and some thirty more, `UDreamWorldCalendar::GetCityPresets`) -- its latitude, longitude, standard time and
daylight saving written into the clock's settings as the config is applied -- or at the place typed in. Daylight
saving (`DaylightSaving`: Europe, North America, south-eastern Australia, New Zealand) puts the clock an hour forward
while its rule says, the clock telling local time as a wall clock does (`UDreamWorldCalendar::IsDaylightSaving`). A
custom calendar (`UDreamCalendarAsset`: its months' names and days, its week, its era, the year it counts from; the
clock config's `Calendar`) tells the dates -- `UDreamWorldSubsystem::GetCalendarDate` gives the year, month, day,
weekday, their names and the time -- while the sun and the seasons still go by the Gregorian year underneath. Besides
the hour, the date, sunrise and sunset, the clock fires `OnMinuteChanged` each game minute (once a frame at most) and
`OnTimeEvent` for each time registered with `RegisterTimeEvent` (a name and an hour, every day) as it passes it. A
weather's name for a player is its preset's `DisplayName`, localizable (the built-in presets' under the namespace
`DreamWeatherPresets`); `UDreamWeatherSubsystem::GetCurrentWeatherDisplayName` gives the current one's.

### Weather radar

`UDreamWeatherRadar` draws the weather map as a weather radar round the view (or a middle `SetCenter` holds), north
up: the precipitation in the radar's colour steps (by default muted: a grey green, a grey yellow, a terracotta and a
dull rose, from light to extreme, each from its threshold; snow's own grey blue as the view's precipitation falls as
snow), the cloud cover a faint veil under it,
range rings and a cross at the middle -- into a texture any UI can show (`GetRadarTexture`, redrawn every
`RefreshSeconds`), with `UDreamWeatherRadarImage`, a UMG image (the *Dream Weather* palette) that shows it, and
`ADreamWeatherRadarDisplay`, a screen in the world showing it (the showcase has one ahead of the start). Its look --
reach, rings, every step's colour and threshold, the veil, the background, the marks -- is `FDreamWeatherRadarLook`
(`SetLook`). For a game's UI and gameplay: `WorldToRadar` / `RadarToWorld` place a point on the picture and back
(markers), `SampleRadar` says what it shows at a point and its step, `FindNearestPrecipitation` finds the nearest rain
within a reach (an approaching shower). The material, `M_DreamWeatherRadar`, can go on any surface: `ApplyTo` sets a
dynamic instance of it.

### Storms

**Moving storms.** Any zone can move: *Motion* (`FDreamWeatherZoneMotion`) gives it a drift (km per game hour) or a
path (the first spline of its actor, at `PathSpeed`, looping or not), a lifetime and fades in and out (game hours).
Where it is and how strong is a pure function of the world time from `StartSeconds` — the same on every machine, after
a load, in a replay, and for a past moment the surfaces integrate over (`ApplyWeatherInfluenceAt` places the zone for
the time it is asked about). A zone whose motion has no start time has no effect until `StartMotion`. The actor itself
stays where it was placed (`GetFootprintCenter` says where its weather is); the weather map, so the sky's clouds, and
the lightning (it strikes where the lightning is strongest) go with it. `ADreamWeatherStorm` is a ready one: a round
zone of `WP_Storm` 15 km in radius with a `Path` spline; spawn it and call `StartMotion`, give it a start time, or tick
`bStartOnBeginPlay`. `bAffectsQueries` off leaves a zone only drawing into the weather map.

**Storms that come by themselves.** `ADreamWeatherStormSpawner` cuts time into slots (`SlotHours`); each slot holds a
storm or not by the season's chance (`SeasonChance`), drawn from the world seed and the slot alone. A storm is of a
kind drawn by the season's weights (`Kinds`: a preset with spring–winter weights), starts somewhere in its slot, lives
`LifetimeHours`, and crosses on a straight line from the prevailing direction (± `DirectionVariation`) through a point
within `PassRadius` of the spawner — place it where storms should pass. The spawner is itself the weather influence:
a query for any moment, now or past, gets the storms alive then, so the surfaces' integration sees storms long gone.
While a storm lives the spawner keeps an `ADreamWeatherStorm` for it that only draws it into the weather map (the
sky). `GetStormsAt(WorldSeconds)` lists the storms of any moment, for a forecast.

### Temperature and climate

`ADreamTemperatureVolume` (or `UDreamTemperatureVolumeComponent`): a sphere or box with a temperature of its own,
fading out over `Falloff` — **Offset** (a campfire: +20 °C) or **Replace** (a heated room, a cave; `Weight` is its
insulation, and it measures itself against the air of the moment asked). It is a weather influence, so every query
sees it (`GetTemperatureAt`, snow turning to rain by the fire, the characters' weather status).

**Climates.** The *Climate* selector (`UDreamWeatherSelector_Climate`) takes a Köppen type (25 of them, `Af` … `EF`,
each a table of representative seasons: mean high and low, precipitation, cloudiness, whether dust blows;
`FDreamClimate::Koppen`) or your own `UDreamClimatePreset`, and from it draws the weather — clear, cloudy, overcast,
rain, storm, snow, fog, dust, by each season's odds (wet seasons rain, cold ones snow, warm wet ones storm) — and
gives the temperature model its yearly mean and swings in place of the project settings'.

### Characters

**Weather status** (`UDreamWeatherStatusComponent`, module `DreamWeatherFX`): put it on the player. A few times a
second it asks what reaches the actor — rain and snow after the weather's shelters and a trace for a roof above it,
the wind field's speed there, dust, the temperature with temperature volumes — and keeps `GetStatus()`: the exposures
now, and what has built up (`Wetness`, `SnowCover`, `Dustiness`, `Chill`; real-time rates: soaked in `WetSeconds` of
full rain, drying faster in warm and windy air, snow melting into wetness above freezing, rain washing dust off) and
how cold it `FeelsLike` (wind and wet clothes). `OnExposureChanged(Exposure, bStarted)` fires with hysteresis for
rain, snow, wind, dust, cold, heat, soaked and freezing. With `bWriteMaterialParameters` it sets `DreamWet` /
`DreamSnow` / `DreamDust` / `DreamWind` on every mesh of the actor (dynamic material instances); with
`bFreezingBreath` it breathes out mist (`NS_DreamFreezingBreath`) at `BreathSocket` when it feels colder than
`BreathTemperature`.

### Dust and debris

The weather's effects have a third layer of fall paths, the **dust layer** (*Dream Weather Effects ▸ Dust and
Debris*): nearly flat, downwind, a few metres up, ending at walls, roofs and shelters — traced only while dust blows or
the wind is strong. `NS_DreamDust` streams motes along them, swirling about their paths, with a few large soft puffs
for the near haze (its amount is the Dust channel; what is already flying fades with the dust within half a second,
so no haze outlives the storm though its particles live 20 s). In a strong wind (`DebrisWindSpeed`) the wind's debris — leaves,
and the odd twig — starts along the same paths (`NS_DreamWindLeaves`, its Debris emitter; `M_DreamWindDebris`) and the
wind field carries it. How much debris and its colour follow the season (`DebrisSeasonAmount`, `DebrisSpring` …
`DebrisWinter`: autumn leaves, a bare winter). Nothing blows in indoors.

### Sequencer

Module `DreamWorldSequencer` (track editors in `DreamWorldPlayEditor`); *+ Track ▸ Dream World* in a level sequence:

* **Time of Day** — the section's `Hours` curve (0–24, beyond into the next days, from the clock's start date or the
  section's own `Date`) sets the world clock while it is active. The clock does not run on its own meanwhile
  (`BeginExternalDrive`); playing runs it there, so the hour's events fire; scrubbing or a cut jumps it.
* **Weather** — sections of three kinds: a **preset** (forced while active at its weight; the section's easing is the
  transition, overlap two to crossfade, the lower row first), **channels** (curves per weather channel; only keyed
  ones apply), **lightning** (every key a strike, its value the intensity, at `Location` relative to the view or in
  the world, scattered the same way on every play). They go through the weather's modifier stack
  (`UpdateModifier` keeps one modifier per section per sequence instance): the tracks keep no weather of their own.
* **Offline renders.** `DreamWorld.CinematicMode` (-1 by default: on while a Movie Render Queue render drives the
  frames; 1 always, 0 never; `SetCinematicModeOverride`): every frame a full update with no easing anywhere — the
  sky's lighting snaps, the view probe casts its whole round — so each rendered frame stands on its own.

Multiplayer is not done yet; the clock's drive and the modifiers are the one place the state lives, ready for it.

### Gameplay showcase

`UnrealEditor-Cmd.exe <project> -run=DreamWorldPlaySetup -ShowcaseMap` (after `DreamDynamicWorldSetup`) writes
`/DreamDynamicWorld/Play/Maps/Lvl_DreamWorldPlay` and the audio assets under `/DreamDynamicWorld/Audio`: a hut (go
in: the rain goes dull; stand in the door), a porch (a roof overhead, open all round), a grove (leaves), a campfire
(warm air), crates the wind pushes, a storm that drifts in from the west on the morning of 21 June (over the area around ten, gone by half
past twelve), a storm spawner, and 7 km to the north a snowfield of no regular shape (an outline zone some 7 km
across with a bay cut into its south side) over land that reads the surface maps, under weather that runs clear → rain
→ storm → cloudy every three hours.
`-ForceAudio` builds the graphs again, `-Force` the level.

## Showcase

Everything the plugin does in one place, to look it over as a whole: **`/DreamDynamicWorld/Showcase/Maps/Lvl_DreamWorldShowcase`**,
written by `-run=DreamDynamicWorldSetup -WorldShowcase` (with the dusk level and its assets below). It is built afresh
on every run, so rerun it after updating the plugin to take in what it has gained.

**The set.** Rolling ground and a paved square under the surface materials (the terrain is soft ground, the square
hard), realistic props (stone, metal, wood) and a toon group (toon props and a toon figure) on the square. Beyond it a
**hut** to step into — stone walls that block the view probe (inside counts as indoors: the rain goes dull), a doorway,
a wide window looking out, a roof the rain knows, a dim warm lamp over the table so the room reads by day and night —
and a **porch** before its door that only a roof capture box tells the weather about. A **window** stands in the open with the rain on its glass; a **pond** fills the dip (its rim follows
the bank just inside the waterline); a **grove** on the rolling ground has green crowns tagged as leaves (the rain
sounds on them); a **campfire** warms the air around it (a temperature volume) and keeps the ground round it clear of
snow and puddles (a mask brush); **crates** slide and tip in a gale (`UDreamWindForceComponent`); an irregular
**snow field** (a polygon zone) lies far ahead; a **storm** drifts in from the west in the morning of the first day. A
wind terrain map baked over the set gives the roofs and the dips the puddles follow (props, posts and the crates are
pass-through).

**The far land.** Beyond the set the land runs on to the horizon (`SM_DreamShowcaseFarLand`, rings out to 35 km under
the terrain material, so snow lies on it): it meets the set's ground without a step and rises into near hills 5–7.5 km
out to the south-west and north-east (to some 600 m), mid mountains 12–16 km to the west (to 1.3 km) and a far range
24–30 km to the south (to 2 km), with low ranges closing the horizon all round. It is there for what lies between the
eye and the land — the aerial perspective, the fog — and has no collision.

**The day.** Twelve real minutes a day, from 8:00 on 20 January (so the snow settles). The weather is a schedule
(`UDreamWeatherSelector_Sequence`) that repeats every 24 hours:

| Hours | Weather |
| :-- | :-- |
| 0–9 | clear (the night before, the morning) |
| 9–11 | cloudy |
| 11–13 | rain |
| 13–15 | storm (thunder) |
| 15–17 | after the rain (`WP_Overcast`, the ground still wet) |
| 17–20 | snow |
| 20–22 | dust |
| 22–24 | clear |

From 20:00 to 5:00 a sky modifier (`DSM_ShowcaseAurora`, an entry in the sky's *Conditional Modifiers*) lights the
aurora; the sky shows it only once it is dark.

**The dusk level.** `Lvl_DreamWorldShowcaseDusk` is the same set with a level sequence playing at once in cinematic
mode (no movement, no HUD): `LS_DreamWorldShowcaseDusk`, twenty seconds of dusk into night (the time track, 17:00 to
23:00) while the clear sky turns to rain (the weather track: clear, then rain eased in over four seconds). Open the
sequence in Sequencer to scrub it.

**Looking it over.** A shot tour stops the clock in each weather of the schedule and takes seven views — the wide
shot, the toon group, inside the hut, the doorway, the pond, the grove, the snow field — then forces snow and dust by
day and a storm by night (the schedule has them the other way round), a winter morning after an evening's rain and a
clear, still night — frost and black ice at dawn, the thaw by late morning, then rime in a freezing fog (`-Parts cold`,
its own sheet) — looks at the far land from beside the hut (9 m
up, to the south-west) at noon, in the evening, overcast and in fog with the aerial perspective and the height fog each
on and off (`-Parts far`), and takes a frame every three seconds of the dusk sequence:
`Tools/Showcase/Invoke-ShowcaseTour.ps1 -Project <host>.uproject`, then `python Tools/Showcase/Make-ShowcaseSheet.py
<screenshots> <out>` for the overviews (rows the weathers, columns the views — the cars among them; the cold's and the far land's own sheets). The tour is plain `DreamWorld.Script`
files (`Tools/Showcase/tour_*.dws`: `time`, `move`, `aim`, `settle`, `shot` lines). After each jump of the clock they
wait four seconds, for the drops of the weather before to land; inside the hut, two seconds before the shot, for the
indoor exposure to settle. `DreamWorld.ShotTour Hours=8/12/16/22` takes a quick round from the level's camera.
`DreamWorld.Showcase.Contents` and `DreamWorld.Showcase.DuskSequence` check that the levels hold all of the above.

**Timing it.** `Tools/Showcase/Measure-ShowcaseGpu.ps1` times the GPU features one at a time on the showcase level at
1920x1080, each at a fixed view and weather: the surface maps, the surface materials, trails, ripples, window glass,
the rain, snow, dust and debris particles, the cloud renderer, light shafts, the aurora, the star map, the lens flare,
drops and frost on the lens, heat haze, the rainbow and an eclipse. A feature with a switch is timed off and on (its own
GPU events, or the whole frame), one with passes of its own by those passes; each figure is the median of `-Samples`
(7) ProfileGPU dumps. It starts only on a quiet GPU (under `-MaxBusy`, 15 %, by nvidia-smi; after `-WaitMinutes` it
gives up and names the processes using it, stopping none), samples every process's GPU use while it runs, and writes a
Markdown table next to the run's log: the feature, whether it is on by default, its cost, and how busy the GPU was with
other work meanwhile. A whole run takes some minutes; `-Only Rain,LensFlare -Samples 1` is a quick check, `-Shots`
pictures each feature's view, `-Log` reads a run again.

## Surfaces

Module `DreamSurface`: what the weather leaves on the ground. Each surface carries four amounts, 0..1
(`FDreamSurfaceState`): **Wetness** (the film of water: dark and glossy), **Water** (water standing where the surface lies low:
rising, it fills the deepest dips first; full, a real dip holds it all over and flat ground in its deepest small hollows
only, some tenth of it), **Snow** (of the deepest cover, *Max Snow Depth*,
30 cm by default) and **Dust**; and what the cold makes of them, four more: **Ice** (a glaze of clear ice), **Frost**
(hoarfrost), **Rime** and **Frozen Water** (how much of the standing water is frozen over).

**How they change.** Rain soaks a surface within minutes, then fills its hollows; after the rain the film dries in
an hour or two of sun and wind (slower in the shade, the cold or still air), the puddles outlast it. Snow settles
below freezing (warm ground melts the flakes as they land), melts above it — faster in the sun and the rain — and
the melt wets the ground. A dust storm lays dust, a dry wind a little, and rain washes it off. Under a roof
(shelters, the wind's baked roofs) nothing lands, and the shade keeps the sun off. The rates are in *Project
Settings ▸ Dream Surface ▸ Rates*, in game hours.

**The cold.** What freezes is the ground, not the air: its temperature is the air's, warmer in the sun (*Sun Warming*,
6 °C in a full sun) and at night colder under a clear sky, which it radiates its warmth away to (*Sky Cooling*, 4 °C
under a clear, still sky, less in a wind; cloud and roofs keep it in). So on a clear, still night with the air at a
couple of degrees above freezing the ground freezes, and once the sun is on it it thaws. On frozen ground:

- **black ice** — the wet film freezes into a glaze within the half hour (*Freeze Hours*, at −2 °C on the ground), and
  rain landing on it freezes as it lands, wetting nothing; the glaze melts back into the film as the ground warms
  above freezing (*Thaw Hours*, at 5 °C in the sun);
- **frozen puddles** — the standing water freezes over in a few hours (*Puddle Freeze Hours*) and while frozen neither
  fills nor dries;
- **frost** — hoarfrost grows under a clear, still sky (*Frost Hours*; a fifth as fast under cloud, none in a wind or
  while something falls) and goes within the half hour once the sun is on it;
- **rime** — a freezing fog (the weather's fog channel) builds it on everything, some three times as fast in a full
  wind (*Rime Hours*).

The weather has no rain below some −1 °C of air (what falls turns to snow near freezing), so the glaze comes from a
wet surface freezing after the rain or from rain on ground still frozen. The presets `WP_Frost` (a clear, still, cold
night) and `WP_FreezingFog` show them.

**Deterministic, nothing saved.** A surface's state is the weather integrated over its past (*Memory Hours*, 48 by
default, in *Step Minutes* of 15) on the timeline's own grid: the same moment gives the same state whenever and
however often it is asked, on any machine, after a load or a jump in time. The weather of a past step comes from:

- **the schedule** — a pure function of the time and seed, known for any past moment;
- **what this session saw** — modifiers and forced weather fade in real time, so their past values cannot be worked
  out afterwards: each step the session runs through records the weather it had (modifiers included) when it began.
  A modifier therefore counts from the moment it took effect in this session; before that, and for every step the
  session never saw (before it started or was loaded, across a jump in time), the schedule stands. A loaded game has
  its modifiers back from the world snapshot, so they count again from the load on, the same on every machine.

Zones apply at each step's own time (`IDreamWeatherInfluence::ApplyWeatherInfluenceAt`), so a zone that moves with
the world time is where it was then. The formula is one file, `Shaders/Shared/DreamSurfaceShared.h`, compiled for the
CPU queries and the GPU surface map alike.

**Queries.** `UDreamSurfaceSubsystem::SampleSurface(Position)` (and `SampleSurfaceAt(Position, WorldSeconds)`),
`GetSnowDepthAt(Position)` in cm, `GetGripAt(Position)` — how much grip is left there, a multiplier on a surface's own
(1 as dry .. some 0.1 on black ice or a frozen puddle; the worst of the ice, frozen puddle, frost or rime, snow,
standing water and wet film there, each by how much of it covers the point, each one's grip at full cover in *Project
Settings ▸ Dream Surface ▸ Grip*) — and `GetViewState()`, the state in the open at the view this frame. A query
integrates the whole memory (some 190 steps of the weather at that point, about 0.1 ms), so ask it for the places
that matter (the player, a footstep), not for thousands of points a frame.

**Surface maps.** For the materials, the subsystem keeps the same state on the GPU in maps around the camera
(`Shaders/Private/DreamSurfaceMap.usf`): a **near map** (`RT_DreamSurfaceNear`, 256², *Near Map Extent* 128 m,
moving in eighths of its size), a **far map** as wide as the weather map (`RT_DreamSurfaceFar`, 128²), and a
**cover map** (`RT_DreamSurfaceCover`); beside each state map an ice map of the same size holds the cold's four
(`RT_DreamSurfaceNearIce`, `RT_DreamSurfaceFarIce`). Each texel integrates the same steps a query does — the global weather of each
step, the zones applied where the texel is (their footprint as the weather map has it; on cloud cover, darkness,
precipitation, snow ratio, temperature, dust, wind and fog), each where it stood and as strong as it was at that step (a
moving storm leaves its snow along its way, not 48 hours of it wherever it is now), then the shared formula — so a
map texel and `SampleSurface` at its centre agree (`DreamSurface.Gpu.MatchesCpu`, `DreamSurface.Gpu.MovingZone` for a
moving zone across a time jump and a forced change, and `DreamSurface.Gpu.PolygonZone` for an outline with a corner
cut out, `DreamSurface.Gpu.Ice` for frost, a glaze and rime in a freezing fog zone through a night, a thaw and a
jump), and a snow zone's border shows on the ground from afar. The whole
memory is integrated when a step goes by, the map moves or the zones change; in between each update only adds the
part of the current step gone by. The maps hold the state **in the open**; the cover map holds, per texel, the height
up to which something keeps the sky off (the wind's baked roofs, shelter boxes), and a material lowers the state of a
surface below it. What differs from a query: under partial cover (the windward edge of the eaves) a query integrates
the reduced weather while a material scales the open state; the map takes the hour's temperature at the view's height
(with each texel's own cloud cover, which takes some of the daily swing off, and its zones' offset), not at each
texel's height; and like the weather map it takes Override zones only, without their height limits. The maps are
placed by `DDW_SurfaceNear / Far` in the shared collection, beyond both the view's state stands (`DDW_SurfaceView`,
`DDW_SurfaceViewIce`). GPU cost: some 0.03 ms a frame (both maps
and the cover), 0.2 ms for an integration.

**Materials.** One node at the end of a material's attributes (Material Attributes in and out):

| Function | For | What the weather does |
| :-- | :-- | :-- |
| `MF_DreamSurfaceWeather` | props, buildings | wet: a porous surface darkens and a hard one turns glossier, down to a roughness of 0.2–0.35 (blurred highlights and reflections, never a mirror); metal does not darken; puddles where it lies low on what faces up (dark, mirror-smooth, flat); snow on what faces up first, steep slopes last, thinning out softly over metres; dust under the snow; the cold under the snow: a glaze darkens a surface as water does and turns it glossy with a cold tint, a frozen puddle is milky, flat and cracked, frost whitens a surface with matte, sparkling crystals without hiding it (most on what faces up), rime builds into a white crust on every face; both break up in fine clumps as they thin |
| `MF_DreamSurfaceWeatherTerrain` | ground | the same, and the snow stands up off the ground: World Position Offset by default, or Displacement with the *Nanite Tessellation* switch (set the displacement magnitude to *Max Snow Depth*) |
| `MF_DreamSurfaceWeatherToon` | toon surfaces | colour bands shift instead of darkening: the block's value drops a little and its saturation rises, the roughness drops (a toon material reads it as a wider highlight band); the roughness never drops below 0.4, so a lit toon material keeps the engine's reflections blurred; hard-edged puddles with a cheap sky reflection and a bright rim; hard-edged snow with an irregular edge, and dust; a glaze shifts the block toward a pale ice tint and widens the highlight band, a frozen puddle is a hard-edged pale block with crack lines, frost and rime a hard-edged white-blue cover with sparse glint dots |
| `MF_DreamGlassRain` | window glass, vehicle glass | drops gather on the pane while it rains on it and come and go; now and then one runs down a wandering path, leaving a thin trail and a few beads and wiping the drops it passes; after the rain the sitting drops stay while the pane is wet and go as it dries; a flat pane (a skylight) gets sitting drops only, and snow; on a vehicle's glass the air pushes the drops along and the wipers sweep them away (below) |
| `MF_DreamSurfaceSample` | your own | wet, water, snow, dust and exposure at a world position, and the ice, frost, rime and frozen water |
| `MF_DreamWaterRipples` | your own water | rain ripples on a tangent-space normal (a pond, a fountain), as many as the rain at the view brings |

**Tuning the look.** What the weather does to a surface is set in the editor, per material instance: every shift,
edge and colour the weather functions apply is a material parameter, its default the look described above. Put the
values a set of materials shares in a parent instance and let the materials inherit them.

| Group | Parameters (defaults) |
| :-- | :-- |
| *Weather Look* (realistic, terrain) | wet: `WetValueHard` 0.85 and `WetValuePorous` 0.4 (how much colour a wet hard / porous surface keeps), `WetRoughHard` 0.2 and `WetRoughPorous` 0.35 (the roughness it goes down to); water: `PuddleValue` 0.35, `PuddleRoughness` 0.02, `RippleStrength` 0.35; snow: `SnowColor`, `SnowRoughness` 0.6, `SnowCoverGain` 1 (above 1 a thin fall covers sooner), `PrintDarken` 0.12; dust: `DustColor`, `DustAmount` 0.85, `DustRoughness` 0.95; cold: `IceTint`, `IceRoughness` 0.06, `IceValue` 0.8 (how much colour a glazed surface keeps), `FrozenPuddleColor`, `FrozenPuddleRoughness` 0.15, `FrostColor`, `FrostRoughness` 0.75, `FrostCoverGain` 1, `FrostGlints` 0.5 (how much frost sparkles), `RimeBrightness` 1.04, `FrostAmount` 0.5 and `RimeAmount` 0.9 (how far a full frost / rime whitens a surface: a frost lets it show through) |
| *Toon Weather Look* | wet: `ToonWetValueDrop` 0.18, `ToonWetSaturationBoost` 0.35, `ToonWetRoughnessScale` 0.35 (the highlight band's widening); edges: `ToonEdgeWidth` 0.05 (half the width of every hard edge: 0 a cut line), `ToonRoughnessFloor` 0.4; water: `ToonPuddleThreshold` 0.5 (lower, wider puddles), `ToonPuddleValue` 0.45, `ToonPuddleSkyReflection` 0.55, `ToonPuddleRoughness` 0.05, `ToonPuddleRimWidth` 0.2, `ToonPuddleRimColor`, `ToonRippleBrightness` 0.18; snow: `ToonSnowThreshold` 0.5, `ToonSnowColor`, `ToonSnowRoughness` 0.7, `ToonPrintThreshold` 0.5; dust: `ToonDustThreshold` 0.4, `ToonDustColor`, `ToonDustAmount` 0.8, `ToonDustRoughness` 0.95; cold: `ToonIceThreshold` 0.5, `ToonIceTint`, `ToonIceTintAmount` 0.35, `ToonIceRoughnessScale` 0.25, `ToonFrozenPuddleThreshold` 0.5, `ToonFrozenPuddleColor`, `ToonCrackBrightness` 0.15, `ToonFrostThreshold` 0.45, `ToonFrostColor`, `ToonFrostRoughness` 0.8, `ToonFrostCoverGain` 1, `ToonGlintBrightness` 0.25, `ToonFrostAmount` 0.5, `ToonRimeAmount` 0.9 |

Where the weather lies (the wet film, the puddles' hollows, the snow's cover and edge pattern) stays the shared
formula, so a CPU query and every material agree on it; the parameters change how it looks there.
`DreamWorld.SurfaceLook.Parameters` checks that the demo materials carry every parameter with its default.

**Rain on it.** While it rains (at the view, `DDW_Weather0`; not under cover) the puddles ripple — rings spreading
from where drops land, more cells ringing the harder it rains, every ring with the same rhythm so a temporal filter
does not shimmer on it — and a surface not soaked yet shows short-lived wet spots where the drops land. Both are in
the material functions (no decals); the toon function draws them as hard-edged rings and dots. They stop with the
rain. Cost: some 0.03 ms of base pass at 1080p in the rain on the test level.

**Hard and soft surfaces.** The weather functions take an optional **Softness** input: 0 a hard surface (stone,
paving, wood, metal), 1 soft ground (soil, grass). Wet, soft ground mostly darkens and barely turns glossy (it drinks
the water), and its puddles are faint; the demo terrain is soft. **Where the water stands**: in small hollows of
*Project Settings ▸ Dream Surface ▸ Water ▸ Puddle Scale* (1.5 m by default) on flat ground, and in the ground's own
dips — where it lies below its surroundings within *Low Radius* (3 m), judged from the wind's baked level heights
(`DSF_GroundLow`; the cover map's third channel), so a level with a baked wind terrain map gets puddles in its real
valleys and hollows; without one, the small hollows only. Roofs over the cover map come from the wind's runtime
terrain map where it runs (the baked heights elsewhere). Walls, boxes and posts beside a point do not count as its
surroundings. **Where snow thins out** — a zone's border, a light fall, a thaw — it breaks up over metres to tens of
metres along a large pattern (`DSF_SnowEdge`), what faces up keeping it longest; the functions read the snow at four
points 5 m around a pixel along an offset that wanders by up to 10 m (`DSM_SnowSoft`), so a zone's border fades over
metres along an irregular line rather than the maps' texels (`SampleSurface` reads the point itself). The toon
function keeps that edge hard but irregular. The level heights take every surface's top, so small props would roof
the ground beside them: mark props, posts and windows pass-through for the wind (`UDreamWindPassThroughComponent` or
its tag) before baking.

They read the maps and the cover (`/DreamDynamicWorld/Surface/...`) and the shared collection only. The hollow pattern
puddles gather in and snow thins at (`DSF_Hollow`, `DSF_PuddleDepth`, `DSF_SnowEdge`) and the covers are in the shared
header, so a CPU query can tell whether a point stands in a puddle (`SampleContact` takes a hard surface). Their HLSL is one file, `Shaders/Private/DreamSurfaceMaterial.ush`, included by
each function's custom nodes (no function calls another). Demo materials: `M_DreamSurfaceDemo`, `M_DreamSurfaceToonDemo`
(a minimal cel look whose highlight band follows the roughness), `M_DreamSurfaceTerrainDemo`, `M_DreamGlassDemo`.

**Rain on glass.** `MF_DreamGlassRain` finds the pane's own axes from its geometric normal (across it horizontally, up
it), so it works on any pane at any angle without UVs. The drops bend the normal (their refraction and highlights),
smooth the roughness and make the pane a little less clear (opacity). They take the rain landing on the pane (none
under cover, as the ground) and the wetness the surface maps hold there, so a pane under eaves stays dry and the drops
outlast the rain as long as the ground's film does. The drops are a pure function of the place and time
(`DSF_GlassRain` in the shared header; `DreamSurface.GlassRain` checks it on the CPU): no texture, no render target.
A translucent glass material wants per-pixel lighting (*Surface ForwardShading*) for the drops' highlights to show.
Snow lies on a pane that faces up enough (a skylight, a windscreen), none on an upright one.

**Vehicle glass.** With its **Vehicle Glass** input at 1, `MF_DreamGlassRain` reads the drops in the glass mesh's own
frame (at its scale, so an unevenly scaled pane keeps round drops), so they stay put on a car's windows as it moves,
and:

- the air pushes the water along (`DSF_GlassFlow`): it drags a drop once it blows at some 4 m/s over the pane, holds
  one against gravity on an upright pane at 15 m/s (on a raked windscreen sooner), so at speed the runners go up the
  windscreen and the sitting drops streak and slide; on a side window the wind drives them across;
- a pane facing the way the car goes catches more rain (`DSF_GlassRainGain`: four times as much head-on at 97 km/h);
- the wipers sweep their arcs clear (`DSF_WiperSince`, `DSF_WiperClear`: the blade swings out and back, easing at the
  ends; behind it the rain lays the drops again within some three seconds in a downpour, more slowly in a drizzle, and
  a thin film shows right behind the blade); where they have swept the snow is gone, and the heater melts it.

A **Dream Surface Vehicle Glass** component (`UDreamSurfaceVehicleGlassComponent`) on the vehicle tells its glass —
the owner's primitives tagged `DreamGlass`, or all of them — how fast it moves (its velocity, or how far it moved, plus
*Extra Velocity*: a parked car shown as if it drove), whether the wipers run (`SetWipers`, *Wiper Period*) and the
*Heater*, through custom primitive data 24..30 (the velocity in each mesh's own frame; seconds per swing, 0 parked; the
time they started; the heater). Where the wipers are on the pane is the material's (group *Vehicle Glass*: `WiperPivot`,
`WiperPivot2` — a pivot in the mesh's frame and the blade's length, 0 none — and `WiperAngles`, from and to, degrees in
the pane: 0 across it, 90 up it); `GlassSnowColor` is the snow's. `M_DreamVehicleGlassDemo` and its instance
`MI_DreamVehicleGlassDemo` (made by the Setup, two wipers on the demo windscreen) show it; the showcase level has two
cars south of the set, one parked and one shown at 60 km/h with its wipers running (the tours' *Car* and *CarGlass*
views). `DreamSurface.VehicleGlass` checks the flow, the rain's gain, the wipers and what the component writes.

**Masks and roofs.** A **mask brush** (`ADreamSurfaceMask`, or `UDreamSurfaceMaskComponent` on anything) multiplies
what the weather leaves beneath it — 0 masks an amount out, above 1 strengthens it, per wet / water / snow / dust (the
glaze follows the wet, frost and rime the snow): a
heated square with no snow, a path kept clear, more water in a gutter. A box or circle seen from above (the weather
zones' footprint function), fading over *Fade Distance*; overlapping brushes multiply; `SampleSurface` and the maps
(a mask map beside the cover, `RT_DreamSurfaceMask`) both see it. A **roof capture box** (`ADreamSurfaceRoofCapture`)
captures from above the height of what stands inside it — a roof, a canopy, a deck — with a depth capture when it
registers in a game world, when it moves and on `CaptureNow`, so what lies beneath stays dry and free of snow with no
shelter box placed by hand. The heights go to the cover map (up to four boxes near the camera) and are read back once
per capture, so the box is a weather shelter as well: exposure, the rain's fall paths and `SampleSurface` see it too.

**Eaves.** A **drip line** (`ADreamSurfaceDripLine`, or `UDreamSurfaceDripLineComponent` on anything; along its X
axis, *Length* long) along a roof's lower edge drips while it rains and after: how hard (`DSF_EaveDrip`) follows the
rain landing on the roof and, once it stops, the roof's own film (the state in the open at the eave,
`SampleOpenSurface`) — it drips on while the film stays above *Drip From Wetness* (0.55 of a soaked film), fewer and
fewer drops as it dries, so a shaded, still eave drips longest; frozen, it stops. *Drops Per Metre* (8 a second in a
full rain) sets how many. The drops (`NS_DreamEaveDrips`: they form along the eave, fall freely and splash on the
ground traced below each end) ring the puddles they land in (the trail map, near the view). `GetDripStrength`,
`GetDropRate` ask it; `DreamSurface.Dump` lists every drip line with its strength, rate and live particles. The
showcase's hut and porch have them on their edges. `DreamSurface.EaveDrips` checks the rain, the drying and the
puddles' rings.

**Footprints.** A **surface interaction** component (`UDreamSurfaceInteractionComponent`) on a character, a foot or a
wheel leaves prints: every *Step Distance* it moves (or on `PrintNow`) it traces down to the ground, asks what is
there (`UDreamSurfaceSubsystem::SampleContact`: dry, wet, puddle, snow, dust, ice or frost, with the state, the
puddle's depth, the snow's and the grip left) and calls `OnContact` with it — the place for a footstep sound or a splash — then presses a print
with `Stamp(Location, Radius, Strength)`. The prints go to a **trail map** around the camera
(`RT_DreamSurfaceTrails`, 1024² over *Trail Extent*, 51.2 m; placed by `DDW_SurfaceTrails`): how deep a print is and
how stirred the water. The materials sink the snow and the dust where it was trodden (the terrain function lowers
its snow too, the toon function cuts hard-edged holes) and ring the puddles where a step went in. Prints fill in as
fresh snow falls (*Print Fill Hours* of snow), the wind blows them over (*Print Wind Hours*) and they fade in any
case (*Print Fade Hours*, in game hours); the stirred water settles in *Ripple Seconds*, real time. The map follows
the camera in eighths of its size and keeps what it carried over; prints that leave it are gone. The trail map is
for the look: `SampleSurface` does not see the prints.

**Test level.** `Lvl_DreamSurfaceTest` (`-run=DreamDynamicWorldSetup -SurfaceTestMap`): a pavement under a canopy with
a shelter box (dry beneath it), a pergola found by a roof capture box, a heated circle (a mask brush), a pond, a window, a wind
terrain map baked over the test area (roofs, and the dips the puddles follow), realistic
props (stone, metal, wood) and toon props and a toon figure, rolling ground,
and a snow zone of no regular shape far ahead (an outline zone) whose border shows on the ground. The schedule is clear; force the weather
(`DreamWeather.Set WP_Rain`) with a short day (`DreamWorld.DayLength 0.5`) to watch it change.

Console: `DreamSurface.Dump`, `DreamSurface.Remap` (integrate afresh), `DreamSurface.SaveMaps [name]` (PNGs under
`Saved/DreamSurface`), `DreamSurface.IntegrateEveryFrame 1` (to measure an integration), `DreamSurface.Stamp <x> <y>
[radius] [strength]` (one print) and `DreamSurface.Walk <x0> <y0> <x1> <y1> [stride]` (a line of footprints).

The rest of this document is **Wind**.

> Renamed from `DreamWind` on 2026-08-18. Content moved from `/DreamWind/<X>` to
> `/DreamDynamicWorld/Wind/<X>`; the plugin's `Config/DefaultEngine.ini` carries the `[CoreRedirects]`
> entry that keeps old references loading — **copy it into your project's `Config/DefaultEngine.ini`**
> (package redirects are read before plugin configs are merged, so a plugin-side copy alone does not
> apply). Module names, class names, `DreamWind.*` console variables and MPC parameter names are
> unchanged.

## Atmosphere

Module `DreamAtmosphere`. The air's scattering from one physical model, `Shaders/Shared/DreamAtmosphereShared.h` and
`DreamAtmosphereScattering.h`, compiled as HLSL for the GPU and as C++ (`DreamAtmosphereHlslShim.h`) for the CPU: a
planet with a shell of air, Rayleigh scattering from the gas (blue, an 8 km scale height), Mie scattering and absorption
from the haze (white, forward, 1.2 km), an ozone layer that absorbs (some 25 km up: it keeps the twilight zenith blue),
the ground's albedo, and the light scattered many times folded into one table (Hillaire 2020). Luminance comes out per
unit of the light's illuminance outside the air, so the sun and the moon read the same tables.

The air is `FDreamAtmosphereParameters` (coefficients per megametre; an earth's by default), from *Project Settings ▸
Dream Atmosphere* until a sky style sets its own (`UDreamAtmosphereSubsystem::SetParameters`). From it the world's
`UDreamAtmosphereSubsystem` builds three lookup tables on the GPU (`UDreamAtmosphereTables`, render targets a material
can sample), and rebuilds them only when the air changes:

| table | size | holds |
|---|---|---|
| transmittance | 256 × 64 | the light's transmittance to the top of the air, by height and zenith angle (Bruneton's coordinates) |
| multiple scattering | 32 × 32 | the light scattered many times, by height and light angle (dense about the horizon) |
| sky | 64 × 32 × 32 | the sky seen from near the ground, by view elevation, angle round from the light and light elevation (both dense about the horizon, the light from 20 degrees under it) |

The CPU's integrals are exact and slow (some milliseconds a sky value): they are for the few values gameplay and the sky
want, and for the tests, which hold the GPU's tables to them (`DreamAtmosphere.Gpu.TablesMatchCpu`: 4 %, 8 % in
twilight, where the earth's shadow edge is read from tables of finite resolution).

**The sky style's air and grade.** A sky style carries the air it is made of (*Atmosphere*, an
`FDreamAtmosphereParameters`) and its look over it (*Grade*, `FDreamSkyGrade`): the air gives how the sky's colours
turn with the sun and spread over the sky; the grade gives the style's look on top, the same for every colour that comes
from the air (the dome, the fog, the clouds' sky, the ambient light), by one function shared by the shaders and the CPU
(`Shaders/Shared/DreamSkyGradeShared.h`, `DSG_Grade`). A new grade is the identity. Its controls:

| control | what it does |
|---|---|
| *Gradient Blend* | 0 (every built-in style): the physical sky, graded. 1: the analytic gradient of the *Dome* group, the old sky exactly (lighting and dome alike). Between: a blend, for a hand-drawn look over the air or to compare. |
| *Match Style Brightness* | On (default): the sky as bright as the *Dome* group's zenith and horizon curves at every hour, its colours and their spread the air's, so the style's exposure, light and ambient keep their tuning. A real twilight falls off by thousands, which a game's exposure does not follow: off, the air's own brightness (for a physical camera). |
| *Light Saturation* | How much of the air's colour the sunlight keeps: 1 the air's deep reds at sunset, the default 0.7 a restrained warm light, 0 white. |
| *Exposure*, *Saturation*, *Hue Shift*, *Contrast* | Curves over the sun's elevation: brighter or dimmer, richer (an anime sky) or greyer, turned toward cyan or violet, the contrast about a middle grey. |
| *Tint*, *Zenith Tint*, *Horizon Tint*, *Tint Spread* | A colour over the whole sky, and one overhead and one at the horizon blending down the sky (below 1 the zenith's reaches low), each a curve over the day. |
| *Afterglow* | The twilight (sun from 0 to 12 degrees under) scaled. |
| *Grade ▸ Weather* | What an overcast leaves of the saturation and takes off the exposure, the dust's colour and strength, how grey fog makes the sky. |

`DreamSky.Grade` checks the grade (the identity, each control, the tints down the sky, the weather).

**The sky on the air.** With the atmosphere's tables built (a renderer; the CPU keeps a copy of the sky table, read back
once per build), the sky is drawn and lit from them (`DreamSky::AtmosphereBridge`, after the style's weather):
the dome shows the physical sky lit by the sun and the moon (`DSky_SkyBackdrop`, which the sky cloud renderer calls the
same way for the air between its clouds: the sky table, `DDW_AtmosphereSun` / `DDW_AtmosphereMoon`, the grade's
`DDW_SkyGrade0..3`); the style's sun halo, moon halo and painted afterglow belong to the gradient and fade with it (the air
has its own glow and twilight). The sun light is the sun outside the air times its transmittance (the weather's dimming
kept), its colour softened by *Light Saturation* and tinted by the grade; the ambient light, the fog colour and the dome's
zenith and horizon (which the sky light's capture watches) come from the sky sampled on the CPU through the grade; the
height fog keeps only the weather's density (the clear air is the aerial perspective's). In the sky's colour the moon
lights as the real one does (*Project Settings ▸ Dream Atmosphere ▸ Moon Sky Illuminance*, its share of the sun's), so
the twilight keeps the sun's indigo instead of a moonlit blue; where the air has next to no light of its own (the sun far
under, the moon down) the style's night takes over. The moon's light on the scene keeps the style's curves. Without the
tables everything is the style's curves. `DreamSky.Atmosphere.PhysicalSky` (with a renderer) checks the sun through the
air, the matched brightness, the haze left to the aerial perspective, and that a gradient blend of 1 gives the style's
lighting exactly.

**Without Shader Model 5 (mobile).** Where the GPU cannot build the tables the sky comes from a sky table baked ahead for
the style's air (the style's *Atmosphere ▸ Baked Sky*: a volume texture `<style>_SkyTable` in half floats, its values times
10^4 so the twilight keeps its digits, and for the CPU the zenith and the horizon all round for each light elevation).
Setup bakes the default style's when it runs with a renderer (`-run=DreamDynamicWorldSetup -AllowCommandletRendering`,
without `-nullrhi`); `DreamSky.BakeSky [style path]` in the editor bakes any style's. A bake stands only for the air it
was made from: bake again after changing *Atmosphere* (until then the sky is the gradient there). The aerial
perspective is off there (it needs the GPU's tables). `r.DreamAtmosphere.ForceBakedSky 1` takes the baked sky on a
desktop GPU, to compare; `DreamSky.Atmosphere.BakedSky` checks that it lights the sky as the GPU's tables do.

**Comparing with the old sky.** `r.DreamSky.PhysicalSky 0` draws and lights the sky from the style's curves alone,
`r.DreamSky.GradientBlendOverride <0..1>` sets the blend (below 0: the style's own), `r.DreamAtmosphere.AerialPerspective 0`
takes the air off the scene. What to look for, old against new: at noon a deeper blue zenith and a paler, brighter
horizon; at sunset a deep blue sky over an orange band along the horizon where the old was mauve all over; in twilight
an indigo sky over a red-orange band; the night, the overcast and the brightness of every hour the same (the style's);
in dust an ochre air; from a height, distant land bluer and paler with distance, meeting the sky at the horizon, and a
fog day's fog the weather's (height fog, the dome's haze and the clouds' copy of the fog all take the same colour and
density). With *Gradient Blend* at 1 the picture is the old one to the pixel (within a frame's noise).

**Aerial perspective.** The air between the eye and the scene: every frame a froxel volume in front of the camera (32 ×
32 cells, 32 depth slices to 64 km, denser near the eye) holds the light the air scatters toward the eye from the sun and
the moon and what it lets through, from the same model and tables
(`Shaders/Private/DreamAtmosphereAerial.usf`); a full-screen pass lays it over the opaque scene by its depth, before the
depth of field. Distant land turns blue and pale and meets the sky at the horizon; at night only the moon's faint light
is in it. The sky is left alone (the far plane, and the dome beyond *Sky Distance*): the dome carries the air already,
and the sky cloud renderer lays its own haze over its clouds by their depth. The world's own view extension draws it
(`FDreamAtmosphereViewExtension`, priority 10: extensions run highest first, so it comes before every pass the sky's
extension adds there, the clouds it lays over the scene from above the cloud base among them, which therefore never
take this air twice). No extension hook comes between the lighting and the translucency, where the engine's fog goes
(`PrePostProcessPass` takes the renderer's internal inputs, out of a plugin's reach), so near translucency takes the air
of what stands behind it: little, that near. The way to have it exact later: a material function for translucent
materials that reads the volume at the pixel's own depth, and the composite leaving those pixels to it. *Project Settings ▸ Dream Atmosphere ▸ Aerial Perspective*: on or off, how far, *Distance
Scale* (above 1 a game's few kilometres read as a landscape's tens), the sea level, the volume's cells and steps;
`r.DreamAtmosphere.AerialPerspective 0` turns it off to compare. **The height fog is the weather's**: the air itself is
the aerial perspective's, so a sky's base haze in the height fog would count it twice (the sky style hands its haze over
when it takes the atmosphere on). The lights it uses until the sky style drives them: *Lights* (the sun's and the full
moon's illuminance outside the air, in the scene's units; the clock's sun and moon give the directions, the moon's
phase dims it). **It follows the weather** (`FDreamAtmosphereWeather`, which a sky sets every frame): under a cloud
deck the air is lit by the deck, not the sun — the sun's and the moon's share dimmed by the style's *Overcast Light
Dimming* (and dust's *Dust Sun Dimming*), the rest given back as an even grey as bright as the clear sky's horizon
(darker under a storm, *Storm Darkening*), and the grade's weather (*Overcast Saturation*, *Overcast Exposure*, *Fog
Grey*, the dust's tint) over it as over the sky, so overcast hills turn grey and dim, not a fair day's blue. Fair
weather leaves the air exactly as it was; `r.DreamAtmosphere.AerialWeather 0` keeps it clear air whatever the weather,
to compare. `DreamAtmosphere.Gpu.AerialMatchesCpu` holds the volume to the CPU's walk
along each cell's ray; `DreamSky.Atmosphere.AerialWeather` the weather the sky hands it.

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

### Pushing things and shaking cameras

**Wind pushes things** (`UDreamWindForceComponent`, module `DreamWind`): on an actor whose primitives simulate
physics, aerodynamic drag from the wind field at each body (½ ρ C<sub>d</sub> A |v| v, A from the bounds facing the
wind, the body's own velocity taken off), `ForceScale`, `MaxForce`, and with `bTestBlocking` a trace upwind for walls
the wind field does not know of.

**Camera shake** (`UDreamWindCameraModifier`, module `DreamWeatherFX`): from `Threshold` wind speed at the camera to
`FullSpeed`, the view buffets (pitch, yaw, roll), in bursts with the gusts, calmed indoors by the view probe.
*Dream Weather Effects ▸ Wind Camera Shake* adds it to every local player; `Enable(PlayerController)` does it by hand.

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

**The runtime terrain map.** Without baking anything, the wind looks at the level round the camera while it runs
and works out the same three things a baked map gives — where walls cast a wind shadow, how the ground speeds the
wind over ridges and slows it in hollows, and which way slopes steer it — for streamed, moving and procedural worlds
alike. *Project Settings ▸ Dream Wind ▸ Terrain | Runtime*, on by default (`bRuntimeTerrain`):

* Two levels follow the camera: **near** 256 m at 1 m texels (`RuntimeNearExtent`, `RuntimeNearResolution`) for the
  shadows of walls and houses, **far** 2 km at 8 m (`RuntimeFarExtent`, `RuntimeFarResolution`) for hills: its
  speed-up and steering, and shadows out to `RuntimeFarShadowRange` (500 m). Each is a ring buffer over the world, so
  as the camera moves only the strip that came into view is looked at (one eighth of the level at a time).
* The heights come from a top-down depth capture `RuntimeCaptureAltitude` (1 km) above the camera, without foliage,
  grass, particles, translucency, skeletal meshes, decals and pass-through actors. At most one capture runs a frame:
  what came into view first, and otherwise, every `RuntimeRefreshInterval` frames (4), the next of each level's
  `RuntimeRefreshStrips` (16) refresh strips, the levels in turn, so a moved building or a streamed-in cell shows up
  within about two seconds by itself. Gameplay that moves big blockers calls **`MarkObstaclesDirty(Bounds)`** on the
  wind subsystem to have the box looked at again at once.
* Compute passes turn the heights into the derived map whenever they change or the wind turns by 5°: the speed-up and
  steering from the blurred heights (the bake's `RuntimeShape` settings), and the wind shadow height swept along the
  current wind (`OcclusionRange` near, `RuntimeFarShadowRange` far, falling `OcclusionSlope` per cm). A sample under
  the shadow height loses wind by `OcclusionStrength`, fading in over `RuntimeShadowFadeBand` (1 m) below it. One
  texture read per level replaces the baked map's 12-tap march.
* **CPU side.** Gameplay, cloth, the weather's roof check and the CPU wind sample read a grid of their own: 64×64 cells
  of 4 m round the camera, its heights from asynchronous box sweeps through the level's collision (256 a frame, then a
  row a frame again), derived the same way. It needs no renderer, so dedicated servers have it too. Cells not swept
  yet leave the sample to the baked map.
* Who reads it: materials (`MF_DreamWindSample`), Niagara (the wind DI), the dynamic volume, the weather's exposure and
  the surface cover map. Only one world at a time owns the shared render targets (`DreamWorldSharedTargets`): in PIE
  with several worlds the others fall back to the baked map; every world has its own CPU grid.
* **The baked map stays** as the fallback: for where the runtime map is off (`bRuntimeTerrain`, `DreamWind.RuntimeTerrain
  0`, no SM5 compute, mobile) and for the puddles' ground dips. Both on, the runtime map wins wherever it has looked.
* Measured on the wind field test level (RTX 4070 Ti SUPER, 1920×1080, 40 000 grass blades, `Measure-ShowcaseGpu.ps1
  -Scene WindField`): the wind shadow costs **0.04–0.30 ms** of the frame with the runtime map, against 0.71–0.74 ms for
  the baked march; its capture scatter and derive passes take 0.06 ms in the frames they run. Each scene capture is a
  render with a fixed cost of about 0.3 ms, which is why they are spread out.

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
* **Slices** (seen in the 3D view and in screenshots, which the arrows are not): `DreamWind.Slice <mode> [height]
  [size] [yaw]` lays a plane under the camera (`ADreamWindSliceActor`, also placeable): `0` the whole wind (hue =
  direction, brightness = speed; the runtime terrain map where it runs), `1` the baked terrain map alone (grey = how
  much wind its shadow leaves, red inside a solid, warm where it speeds the wind up, cool where it slows it), `2` the
  dynamic volume alone (its residual, and the near window's edge), `3` the engine's global distance field (red on a
  surface, white near one, blue farther off, magenta where the view had none), `4` the runtime terrain map alone (as
  `1`). A yaw stands the plane up, 15 m ahead of the camera, across that yaw. `DreamWind.Slice off`.
* `DreamWind.Occlusion 0|1|-1` switches the terrain maps' wind shadow off for every consumer (to time it, or to compare);
  `DreamWind.RuntimeTerrain 0|1|-1` the runtime terrain map (off: back to the baked map).
* **Global distance field.** The renderer keeps it up to date only while something uses it: Lumen *with software
  tracing* (with hardware ray tracing Lumen never asks for it), distance field AO, or a material or Niagara system that
  reads it. `DreamWind.DistanceField 1` asks for it on the wind's behalf (`r.AOGlobalDistanceField.DetailedNecessityCheck
  0` and `r.GlobalDistanceField.DetailedNecessitySufficient 1` while asked, put back after); the project still needs
  *Generate Mesh Distance Fields*. `DreamWind.Gpu.DistanceFieldProbe` checks it on the wind field test level.

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

**Wind field test level.** `/DreamDynamicWorld/Wind/Maps/Lvl_DreamWindFieldTest` is generated (`Setup -WindFieldTestMap`)
for measuring and comparing the wind field itself: flat ground with 40 000 blades of wind grass, a 10 x 8 x 6 m house, a
24 m wall across the wind, an L-shaped corner opening downwind, a conical hill, a clearing with a radial burst every
4 s, a steady 900 cm/s wind along +x with no weather actor, and the terrain map baked over it. Where its pieces stand is
in `DreamWindTestMap.h` (`DreamWorld::Setup::WindTest`). `Measure-ShowcaseGpu.ps1 -Scene WindField` times the wind on
it (the volume update, the wind shadow's march in the materials and in the volume, the volume's material reads, the
grass).

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
  render targets at a time (the plugin's shared render target rule: see *Clock and sky*).
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
solar eclipses for the moon's phase), the stars' sidereal time (the epoch, a January evening in both modes, four
minutes a night, no jump at midnight), the scheduler, the parameter ids and the view probe (a closed room, a
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
geometry, wind, the forward bias, spreading, a drop's own segment across a gust) and `DreamWeather.FX.*` the rain
effects driven by real weather.
`Automation RunTests DreamWind.` — `DreamWind.Shared.*` are pure C++ over the shared model
(determinism, continuity across lattice cells and in time, calm wind, source blending incl. jets and
shelters, packing, terrain, occlusion: max-height atlas, tap levels, wall shadow both ways, holes/ripple),
`DreamWind.Perf.CpuSample` times the CPU sample, `DreamWind.Subsystem.TickAndImpulse` drives a real
subsystem tick with an impulse in whichever editor/game world is up, `DreamWind.Terrain.GridShadow` the runtime
terrain map's CPU grid (a wall's lee and its wedge, the wind turning, unswept cells). With a renderer, on the wind
field test level: `DreamWind.Gpu.RuntimeTerrain` (the GPU's map has the house's lee where the wedge says and none
upwind, the CPU grid agrees, and the lee follows the house when it moves, found by the refresh strips alone) and
`DreamWind.Gpu.DistanceFieldProbe`.
`DreamAtmosphere.Model.*` hold the shared model on the CPU to closed forms and to what skies do (the transmittance
overhead, the red sun on the horizon, the earth's shadow, the tables' coordinates both ways, a blue noon zenith about
as bright as a clear sky's, a paler horizon, the haze's glow round the sun, a warm sunset, twilight dimming to night
with a blue zenith, the haze along the ground: its closed-form transmittance, blue, building up with distance, bright
toward a low sun); `DreamAtmosphere.Gpu.*` (with a renderer) the GPU's tables and aerial perspective volume to the
CPU's integrals, and that the tables are rebuilt only when the air changes.

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
  DreamDynamicWorld.uplugin
  Config/DefaultEngine.ini  CoreRedirects for the old /DreamWind mount point and MPC_DreamWind
  Source/DreamDynamicWorld  core: world clock, sun and moon, shared parameters, scheduler, snapshots, console
  Source/DreamSky           sky actor, style, lighting director
  Source/DreamWeather       weather channels, presets, selectors, modifiers, zones, queries, shelters, lightning,
                            fall paths, weather map pass
  Source/DreamWeatherFX     rain and snow around the view (Niagara; loads after DreamWeather)
  Shaders/Shared/DreamWeatherShared.h     zone falloff (HLSL + C++)
  Shaders/Private/DreamWeatherMap.usf     the weather map compute pass
  Shaders/Private/DreamSkyCloudRender.usf the sky cloud renderer's passes
  Source/DreamWorldEditor   time-of-day toolbar slider, DreamDynamicWorldSetup commandlet
  Tools/TestHost/           the test host template, its generator and the test runner
  Source/DreamWind          wind runtime: subsystem, settings, source component, global actor, volume renderer,
                            terrain map (+ occlusion), pass-through component, demo actors (source actor incl.
                            shelter box, grass field, impulse emitter)
  Source/DreamWindNiagara   UNiagaraDataInterfaceDreamWind
  Source/DreamWindEditor    Tools menu, asset setup, DreamWindSetup commandlet
  DShader/                  MF_DreamWindSample.dsf, M_DreamWindGrass.dsm, M_DreamWindDebug.dsm, Sky/M_DreamSky.dss,
                            Sky/M_DreamCloudShadow.dss, Weather/M_DreamRainDrop.dss, Weather/M_DreamRainSplash.dss
  DFX/                      DreamWindForce.dfm, NS_DreamWindLeaves.dfs, NS_DreamWindDebug.dfs,
                            Modules/DreamFallPath.dfm, Effects/NS_DreamRain.dfs
  Content/MPC_DreamDynamicWorld  the shared parameter collection
  Content/Sky/                   M_DreamSky, M_DreamCloudShadow, RT_DreamCloudPanorama, DS_DreamSkyStylized, T_DreamCloudNoise, Maps/Lvl_DreamSkyTest
  Content/Wind/                  generated assets (RT, materials, Niagara) — regenerable
  Content/Wind/Presets/          DWP_Calm / DWP_Breeze / DWP_Storm (UDreamWindPreset)
  Content/Wind/Maps/             Lvl_DreamWindTest + its baked wind terrain
```

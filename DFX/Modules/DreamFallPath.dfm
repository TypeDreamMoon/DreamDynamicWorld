// DreamWeather - put a particle on one of the weather's fall paths.
//
// The weather traces a few hundred fall paths a second around the view (FDreamWeatherFallPaths, C++): each
// starts high above, falls along the drops' velocity (down and downwind) and ends where it hits something,
// or under a shelter. Effects draw thousands of drops by picking paths at random, so no drop needs a
// collision query of its own and none falls indoors.
//
//   Drops:    AtEnd = false. Starts the particle at the top of the path, or with Anywhere at a random point
//             along it, moving at FallVelocity, with a lifetime that ends exactly where the path does. Drops
//             spread evenly along their paths are what steady rain looks like at any instant, so spawning the
//             steady number of drops in one frame with Anywhere = 1 fills the air at once (a start, a teleport);
//             after that they start at the top (Anywhere = 0).
//   Splashes: AtEnd = true. Puts the particle at the end of a landed path and writes the surface normal to
//             Particles.DreamSurfaceNormal; a path that ended in the air kills it.
//
// Paths:   xyz = where the path ends, w = seconds a drop falls along it.
// Normals: the surface the path landed on, zero when it ended in the air.
Module(Name="Weather/Modules/DreamFallPath", Root="Plugin.DreamDynamicWorld")
{
    Settings = {
        Usage       = ParticleSpawn;
        Category    = "DreamWeather";
        Description = "Places the particle on a random weather fall path: a drop at the path's start (or anywhere along it), dying where the path lands; or with AtEnd, a splash on the surface at a landed path's end.";
    }

    Inputs = {
        DI<ArrayFloat4> Paths;
        DI<ArrayFloat3> Normals;
        Vector FallVelocity = (0, 0, -900) [ Description="The drops' velocity, cm/s, down and downwind." ];
        bool   AtEnd        = false [ Description="Place at the end of a landed path (a splash) instead of along it (a drop)." ];
        float  Anywhere     = 0.0   [ Description="0..1: share of drops that start at a random point along their path rather than at its top (1 for a frame that fills the air at once)." ];
        float  MinLifetime  = 0.05  [ Description="Seconds; a path shorter than this still shows its drop for a moment." ];
    }

    Body = {
        int Count;
        Paths.Length(Count);
        int Index = clamp(int(rand(1.0) * float(Count)), 0, max(Count - 1, 0));

        float4 Path;
        Paths.Get(Index, Path);
        float3 Normal;
        Normals.Get(Index, Normal);

        Vector Particles.DreamSurfaceNormal = float3(0.0, 0.0, 1.0);
        float  FallTime = max(Path.w, 0.0);
        if (AtEnd)
        {
            Particles.Position = Path.xyz;
            Particles.DreamSurfaceNormal = Normal;
            // No paths yet, or this one ended in the air: no splash.
            Particles.Lifetime = (Count > 0 && dot(Normal, Normal) > 0.25) ? Particles.Lifetime : 0.0;
        }
        else
        {
            float Along = rand(1.0) < Anywhere ? rand(1.0) : 0.0;
            Particles.Position = Path.xyz - FallVelocity * (FallTime * (1.0 - Along));
            Particles.Velocity = FallVelocity;
            Particles.Lifetime = Count > 0 ? max(FallTime * (1.0 - Along), MinLifetime) : 0.0;
        }
    }
}

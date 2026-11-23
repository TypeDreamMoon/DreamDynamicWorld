// DreamWind - let the wind fluid push a falling particle off its path.
//
// Precipitation falls along the weather's fall paths, each slanted by the CPU's wind where it starts (no drop
// falls indoors). This adds what the wind fluid does on top (DreamWind data interface, SampleWindDeviation: the
// dynamic volume's deviation from the background wind): a blast pushes the rain and the snow aside, snow spins in a
// building's corner eddies. Response scales it (rain 1, sleet 0.5, hail hardly any); how far the wind may push a
// particle off its path is held within MaxOffset, and one the wind pushed below the top surface under it (in under a
// roof) dies there, so the snow still stays outdoors.
//
// Place it in Particle Update after the particle's own velocity is set (its path's, its sway), BEFORE Solve Forces and
// Velocity: it adds to Particles.Velocity.
Module(Name="Wind/Modules/DreamWindDeviation", Root="Plugin.DreamDynamicWorld")
{
    Settings = {
        Usage       = ParticleUpdate;
        Category    = "DreamWind";
        Description = "Adds the wind fluid's deviation (scaled by Response) to Particles.Velocity, keeping the particle within MaxOffset of where it would be without it, and kills one pushed below the top surface under it.";
    }

    Inputs = {
        DI<DreamWind> Wind;
        float Response  = 1.0   [ Description="How much of the wind fluid's deviation the particle takes (0 = none, 1 = all of it)." ];
        float MaxOffset = 300.0 [ Description="cm: the farthest the deviation may push the particle off its path." ];
        float TopMargin = 30.0  [ Description="cm: a particle this far below the top surface under it (the wind blew it in under a roof) dies." ];
    }

    Body = {
        // (out locals left uninitialised: see DreamWindForce)
        float3 Deviation;
        float  Top;
        Wind.SampleWindDeviation(Particles.Position, Deviation, Top);

        // How far the wind has pushed it so far (DreamFallPath starts it at zero), held within MaxOffset; the velocity
        // carries this frame's share.
        Vector Particles.DreamWindOffset;
        float  Step = max(Engine.DeltaTime, 0.0001);
        float3 Before = Particles.DreamWindOffset;
        float3 After = Before + Deviation * Response * Engine.DeltaTime;
        float  Length = length(After);
        After = Length > MaxOffset ? After * (MaxOffset / Length) : After;
        Particles.DreamWindOffset = After;
        Particles.Velocity = Particles.Velocity + (After - Before) / Step;

        Particles.Lifetime = Particles.Position.z < Top - TopMargin ? 0.0 : Particles.Lifetime;
    }
}

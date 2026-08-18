// DreamWind - relax particle velocity toward the world wind.
//
// The wind comes from the DreamWind data interface (UNiagaraDataInterfaceDreamWind): analytic global
// wind + live sources, plus the dynamic volume on GPU sims. Air drag is modelled as an exponential
// approach to the air velocity, so the result is frame-rate independent and never overshoots.
//
// Place it in Particle Update BEFORE Solve Forces and Velocity; gravity and other forces still add
// on top through Physics.Force.
Module(Name="Wind/Modules/DreamWindForce", Root="Plugin.DreamDynamicWorld")
{
    Settings = {
        Usage       = ParticleUpdate;
        Category    = "DreamWind";
        Description = "Relaxes Particles.Velocity toward the DreamWind wind velocity at the particle (drag), and writes Particles.DreamWindGust.";
    }

    Inputs = {
        DI<DreamWind> Wind;
        float Drag     = 2.0 [ Description="How fast the particle adopts the air velocity, 1/s. 2 = 86% in one second, 20 = almost instantly." ];
        float Strength = 1.0 [ Description="Scales the wind velocity the particle is pulled toward." ];
        float Lift     = 0.0 [ Description="Extra upward velocity per unit of gust above nominal, cm/s. Leaves lift in gusts." ];
    }

    Body = {
        // Leave the out locals uninitialised: the CPU VM's HLSL front end (hlslcc) constant-folds an
        // initialised local into the call and then rejects it as a non-lvalue out argument.
        float3 WindVelocity;
        float  Gust;
        Wind.SampleWind(Particles.Position, WindVelocity, Gust);

        float3 Target = WindVelocity * Strength;
        Target.z += Lift * max(Gust - 1.0, 0.0);

        float Blend = 1.0 - exp(-max(Drag, 0.0) * Engine.DeltaTime);
        Particles.Velocity = lerp(Particles.Velocity, Target, Blend);
        float Particles.DreamWindGust = Gust;
    }
}

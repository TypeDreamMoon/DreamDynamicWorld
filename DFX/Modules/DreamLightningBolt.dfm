// DreamWeather - one segment of a lightning bolt.
//
// A bolt is a jagged line from Start (the cloud base) down to End (where it strikes), plus a few short
// branches forking off it. Spawn MainSegments + Branches * BranchSegments particles in one burst; each takes the
// segment its spawn index names, so the particles of one burst draw the whole bolt. A segment is a sprite
// standing on the segment's midpoint, its velocity pointing along the segment (a velocity-aligned sprite then
// draws it as a strip; nothing moves the particle), its size (width, length).
//
// The jag is smooth noise of a few octaves along the bolt, zero at both ends of the main channel and at the
// root of each branch, so the pieces always join.
Module(Name="Weather/Modules/DreamLightningBolt", Root="Plugin.DreamDynamicWorld")
{
    Settings = {
        Usage       = ParticleSpawn;
        Category    = "DreamWeather";
        Description = "Places the particle on one segment of a jagged lightning bolt from Start to End (with branches), by its spawn index within the burst.";
    }

    Inputs = {
        Vector Start          = (0, 0, 120000) [ Description="Where the bolt leaves the cloud, world cm." ];
        Vector End            = (0, 0, 0)      [ Description="Where it strikes, world cm." ];
        float  Seed           = 0.0            [ Description="Picks this bolt's shape." ];
        float  MainSegments   = 24.0           [ Description="Segments of the main channel." ];
        float  Branches       = 3.0            [ Description="Branches forking off it." ];
        float  BranchSegments = 8.0            [ Description="Segments of each branch." ];
        float  Jag            = 0.09           [ Description="Sideways wander, as a share of the bolt's length." ];
        float  Width          = 60.0           [ Description="Width of the main channel, cm (branches are thinner)." ];
    }

    Body = {
        int Index = ExecIndex();
        int Main = max(int(MainSegments), 1);
        int PerBranch = max(int(BranchSegments), 1);

        float3 Axis = End - Start;
        float Length = max(length(Axis), 1.0);
        float3 Dir = Axis / Length;
        float3 Side = normalize(cross(Dir, abs(Dir.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0)));
        float3 Side2 = cross(Dir, Side);

        // This segment's path: the main channel, or a branch (its root on the main channel, heading off sideways
        // and down, a quarter of the length or so).
        float3 From = Start;
        float3 Way = Axis;
        float Lane = Seed * 7.13;
        float T0 = 0.0;
        float T1 = 1.0;
        float W = Width;
        bool Rooted = false;
        if (Index < Main)
        {
            T0 = float(Index) / float(Main);
            T1 = float(Index + 1) / float(Main);
        }
        else
        {
            int Branch = (Index - Main) / PerBranch;
            int Step = (Index - Main) - Branch * PerBranch;
            float H0 = frac(sin(float(Branch) * 12.9898 + Seed * 78.233) * 43758.5453);
            float H1 = frac(sin(float(Branch) * 39.346 + Seed * 11.135) * 24634.6345);
            float H2 = frac(sin(float(Branch) * 73.156 + Seed * 52.235) * 13758.937);
            float Root = 0.15 + 0.55 * H0;
            // The root point on the main channel (the same jag as the main channel there).
            float2 RootJag = float2(0.0, 0.0);
            float Amp = 1.0;
            float Freq = 3.0;
            for (int k = 0; k < 4; ++k)
            {
                float X = Root * Freq + Lane + float(k) * 11.7;
                float I0 = floor(X);
                float F = X - I0;
                float U = F * F * (3.0 - 2.0 * F);
                float A0 = frac(sin(I0 * 12.9898 + float(k) * 78.233) * 43758.5453);
                float A1 = frac(sin((I0 + 1.0) * 12.9898 + float(k) * 78.233) * 43758.5453);
                float B0 = frac(sin(I0 * 39.346 + float(k) * 11.135) * 24634.6345);
                float B1 = frac(sin((I0 + 1.0) * 39.346 + float(k) * 11.135) * 24634.6345);
                RootJag += Amp * (float2(lerp(A0, A1, U), lerp(B0, B1, U)) - 0.5);
                Amp *= 0.5;
                Freq *= 2.3;
            }
            From = Start + Axis * Root + (Side * RootJag.x + Side2 * RootJag.y) * Jag * Length * sin(3.14159265 * Root);
            float Turn = H1 * 6.2831853;
            float3 Out = normalize(Dir * 0.75 + (Side * cos(Turn) + Side2 * sin(Turn)) * 0.85);
            Way = Out * Length * lerp(0.15, 0.3, H2);
            Lane = Seed * 3.71 + float(Branch) * 19.3;
            T0 = float(Step) / float(PerBranch);
            T1 = float(Step + 1) / float(PerBranch);
            W = Width * 0.45 * (1.0 - 0.7 * T0);
            Rooted = true;
        }

        // The two ends of the segment.
        float WayLength = max(length(Way), 1.0);
        float3 WayDir = Way / WayLength;
        float3 WSide = normalize(cross(WayDir, abs(WayDir.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0)));
        float3 WSide2 = cross(WayDir, WSide);
        float3 Ends[2];
        for (int e = 0; e < 2; ++e)
        {
            float T = e == 0 ? T0 : T1;
            float2 Jagged = float2(0.0, 0.0);
            float Amp = 1.0;
            float Freq = 3.0;
            for (int k = 0; k < 4; ++k)
            {
                float X = T * Freq + Lane + float(k) * 11.7;
                float I0 = floor(X);
                float F = X - I0;
                float U = F * F * (3.0 - 2.0 * F);
                float A0 = frac(sin(I0 * 12.9898 + float(k) * 78.233) * 43758.5453);
                float A1 = frac(sin((I0 + 1.0) * 12.9898 + float(k) * 78.233) * 43758.5453);
                float B0 = frac(sin(I0 * 39.346 + float(k) * 11.135) * 24634.6345);
                float B1 = frac(sin((I0 + 1.0) * 39.346 + float(k) * 11.135) * 24634.6345);
                Jagged += Amp * (float2(lerp(A0, A1, U), lerp(B0, B1, U)) - 0.5);
                Amp *= 0.5;
                Freq *= 2.3;
            }
            // Pinned at both ends of the main channel; a branch is pinned only at its root.
            float Envelope = Rooted ? T : sin(3.14159265 * T);
            Ends[e] = From + Way * T + (WSide * Jagged.x + WSide2 * Jagged.y) * Jag * WayLength * Envelope;
        }

        float3 Segment = Ends[1] - Ends[0];
        Particles.Position = (Ends[0] + Ends[1]) * 0.5;
        Particles.Velocity = Segment;
        Particles.SpriteSize = float2(W, length(Segment) * 1.15);
    }
}

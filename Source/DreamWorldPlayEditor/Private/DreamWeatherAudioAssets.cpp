// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherAudioAssets.h"
#include "DreamAmbiencePreset.h"
#include "DreamWeatherAudioSubsystem.h"

#include "Editor.h"
#include "Interfaces/MetasoundOutputFormatInterfaces.h"
#include "Misc/PackageName.h"
#include "MetasoundBuilderSubsystem.h"
#include "MetasoundEditorSubsystem.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundSource.h"

#include "Sound/SoundClass.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogDreamWorldPlay, Log, All);

namespace DreamWorldPlay
{
	static const TCHAR* AudioPath = TEXT("/DreamDynamicWorld/Audio");

	bool SaveAsset(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		const bool bMap = Asset->IsA<UWorld>();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(),
			bMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_None;
		const bool bSaved = UPackage::SavePackage(Package, Asset, *Filename, Args);
		UE_LOG(LogDreamWorldPlay, Display, TEXT("%s %s"), bSaved ? TEXT("Saved") : TEXT("Could not save"), *Filename);
		return bSaved;
	}

	/** A thin layer over the MetaSound builder that logs every step that fails. */
	class FGraph
	{
	public:
		FGraph(const TCHAR* InName, int32 Outputs)
			: Name(InName)
		{
			EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
			Builder = UMetaSoundBuilderSubsystem::GetChecked().CreateSourceBuilder(FName(InName), OnPlay, OnFinished, AudioOut, R,
				Outputs == 1 ? EMetaSoundOutputAudioFormat::Mono : EMetaSoundOutputAudioFormat::Stereo, /*bIsOneShot*/ true);
			Check(R, TEXT("create the builder"));
			if (Builder)
			{
				Builder->AddToRoot();
			}
		}

		~FGraph()
		{
			if (Builder)
			{
				Builder->RemoveFromRoot();
			}
		}

		bool IsValid() const { return Builder != nullptr && bOk; }

		FMetaSoundNodeHandle Node(const TCHAR* Namespace, const TCHAR* ClassName, const TCHAR* Variant)
		{
			EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
			const FMetaSoundNodeHandle N = Builder ? Builder->AddNodeByClassName(FMetasoundFrontendClassName(Namespace, ClassName, Variant), R) : FMetaSoundNodeHandle();
			Check(R, *FString::Printf(TEXT("add node %s.%s.%s"), Namespace, ClassName, Variant));
			return N;
		}

		FMetaSoundBuilderNodeInputHandle In(const FMetaSoundNodeHandle& N, const TCHAR* Pin)
		{
			EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
			const FMetaSoundBuilderNodeInputHandle H = Builder ? Builder->FindNodeInputByName(N, Pin, R) : FMetaSoundBuilderNodeInputHandle();
			Check(R, *FString::Printf(TEXT("find input '%s'"), Pin));
			return H;
		}

		FMetaSoundBuilderNodeOutputHandle Out(const FMetaSoundNodeHandle& N, const TCHAR* Pin)
		{
			EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
			const FMetaSoundBuilderNodeOutputHandle H = Builder ? Builder->FindNodeOutputByName(N, Pin, R) : FMetaSoundBuilderNodeOutputHandle();
			Check(R, *FString::Printf(TEXT("find output '%s'"), Pin));
			return H;
		}

		void Connect(const FMetaSoundBuilderNodeOutputHandle& From, const FMetaSoundBuilderNodeInputHandle& To)
		{
			EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
			if (Builder)
			{
				Builder->ConnectNodes(From, To, R);
			}
			Check(R, TEXT("connect"));
		}

		template <typename T>
		void Default(const FMetaSoundBuilderNodeInputHandle& Pin, T Value)
		{
			FMetasoundFrontendLiteral Literal;
			Literal.Set(Value);
			EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
			if (Builder)
			{
				Builder->SetNodeInputDefault(Pin, Literal, R);
			}
			Check(R, TEXT("set a default"));
		}

		template <typename T>
		FMetaSoundBuilderNodeOutputHandle Input(const TCHAR* InputName, const TCHAR* DataType, T Value)
		{
			FMetasoundFrontendLiteral Literal;
			Literal.Set(Value);
			return AddInput(InputName, DataType, Literal);
		}

		FMetaSoundBuilderNodeOutputHandle ObjectInput(const TCHAR* InputName, const TCHAR* DataType)
		{
			return AddInput(InputName, DataType, FMetasoundFrontendLiteral());
		}

		// ---- Small patterns ------------------------------------------------------------------

		/** Value * Other, both floats. */
		FMetaSoundBuilderNodeOutputHandle MulF(const FMetaSoundBuilderNodeOutputHandle& A, const FMetaSoundBuilderNodeOutputHandle& B)
		{
			const FMetaSoundNodeHandle N = Node(TEXT("UE"), TEXT("Multiply"), TEXT("Float"));
			Connect(A, In(N, TEXT("PrimaryOperand")));
			Connect(B, In(N, TEXT("AdditionalOperands")));
			return Out(N, TEXT("Out"));
		}

		/** 1 - Value. */
		FMetaSoundBuilderNodeOutputHandle OneMinus(const FMetaSoundBuilderNodeOutputHandle& A)
		{
			const FMetaSoundNodeHandle N = Node(TEXT("UE"), TEXT("Subtract"), TEXT("Float"));
			Default(In(N, TEXT("PrimaryOperand")), 1.0f);
			Connect(A, In(N, TEXT("AdditionalOperands")));
			return Out(N, TEXT("Out"));
		}

		/** Audio scaled by a float. */
		FMetaSoundBuilderNodeOutputHandle Gain(const FMetaSoundBuilderNodeOutputHandle& Audio, const FMetaSoundBuilderNodeOutputHandle& Level)
		{
			const FMetaSoundNodeHandle N = Node(TEXT("UE"), TEXT("Multiply"), TEXT("Audio by Float"));
			Connect(Audio, In(N, TEXT("PrimaryOperand")));
			Connect(Level, In(N, TEXT("AdditionalOperands")));
			return Out(N, TEXT("Out"));
		}

		FMetaSoundBuilderNodeOutputHandle Sum(const FMetaSoundBuilderNodeOutputHandle& A, const FMetaSoundBuilderNodeOutputHandle& B)
		{
			const FMetaSoundNodeHandle N = Node(TEXT("UE"), TEXT("Add"), TEXT("Audio"));
			Connect(A, In(N, TEXT("PrimaryOperand")));
			Connect(B, In(N, TEXT("AdditionalOperands")));
			return Out(N, TEXT("Out"));
		}

		/** A float eased toward its target over Seconds (no zipper noise when the game sets it a frame at a time). */
		FMetaSoundBuilderNodeOutputHandle Ease(const FMetaSoundBuilderNodeOutputHandle& Target, float Seconds)
		{
			const FMetaSoundNodeHandle N = Node(TEXT("UE"), TEXT("InterpTo"), TEXT("Audio"));
			Connect(Target, In(N, TEXT("Target")));
			Default(In(N, TEXT("Interp Time")), Seconds);
			return Out(N, TEXT("Value"));
		}

		/** The low-pass cutoff of a muffle, exactly as DreamWeatherAudio::MuffleToCutoff (log from 20 kHz down to MinCutoff). */
		FMetaSoundBuilderNodeOutputHandle Cutoff(const FMetaSoundBuilderNodeOutputHandle& Muffle, const FMetaSoundBuilderNodeOutputHandle& MinCutoff)
		{
			const FMetaSoundNodeHandle N = Node(TEXT("UE"), TEXT("Linear To Log Frequency"), TEXT(""));
			Connect(OneMinus(Muffle), In(N, TEXT("Value")));
			Default(In(N, TEXT("Min Domain")), 0.0f);
			Default(In(N, TEXT("Max Domain")), 1.0f);
			Connect(MinCutoff, In(N, TEXT("Min Range")));
			Default(In(N, TEXT("Max Range")), 20000.0f);
			return Out(N, TEXT("Frequency"));
		}

		FMetaSoundBuilderNodeOutputHandle LowPass(const FMetaSoundBuilderNodeOutputHandle& Audio, const FMetaSoundBuilderNodeOutputHandle& CutoffHz)
		{
			const FMetaSoundNodeHandle N = Node(TEXT("UE"), TEXT("One-Pole Low Pass Filter"), TEXT("Audio"));
			Connect(Audio, In(N, TEXT("In")));
			Connect(CutoffHz, In(N, TEXT("Cutoff Frequency")));
			return Out(N, TEXT("Out"));
		}

		/** A wave player started by OnPlay. */
		FMetaSoundNodeHandle WavePlayer(const TCHAR* Variant, const FMetaSoundBuilderNodeOutputHandle& Wave, bool bLoop)
		{
			const FMetaSoundNodeHandle N = Node(TEXT("UE"), TEXT("Wave Player"), Variant);
			Connect(OnPlay, In(N, TEXT("Play")));
			Connect(Wave, In(N, TEXT("Wave Asset")));
			Default(In(N, TEXT("Loop")), bLoop);
			return N;
		}

		/** Build it to <AudioPath>/<Name> and save. An existing asset is kept unless bForce. */
		UObject* Finish(bool bForce)
		{
			if (!IsValid())
			{
				UE_LOG(LogDreamWorldPlay, Error, TEXT("%s: the graph did not build; nothing written."), *Name);
				return nullptr;
			}
			const FString PackageName = FString(AudioPath) / Name;
			if (UObject* Existing = LoadObject<UObject>(nullptr, *(PackageName + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				if (!bForce)
				{
					UE_LOG(LogDreamWorldPlay, Display, TEXT("%s exists (-ForceAudio to build it again)."), *PackageName);
					return Existing;
				}
				// Let go of the file and move the old package aside: the new asset takes the name.
				UPackage* Old = Existing->GetOutermost();
				ResetLoaders(Old);
				Old->Rename(*MakeUniqueObjectName(nullptr, UPackage::StaticClass(), *FString::Printf(TEXT("/Temp/%sOld"), *Name)).ToString(), nullptr,
					REN_DontCreateRedirectors | REN_NonTransactional);
				IFileManager::Get().Delete(*FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), false, true, true);
			}
			UMetaSoundEditorSubsystem* Editor = GEditor ? GEditor->GetEditorSubsystem<UMetaSoundEditorSubsystem>() : nullptr;
			if (!Editor)
			{
				UE_LOG(LogDreamWorldPlay, Error, TEXT("%s: no MetaSound editor subsystem."), *Name);
				return nullptr;
			}
			EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
			const TScriptInterface<IMetaSoundDocumentInterface> Asset = Editor->BuildToAsset(Builder, TEXT("DreamDynamicWorld"), Name, AudioPath, R);
			UObject* Object = Asset.GetObject();
			if (R != EMetaSoundBuilderResult::Succeeded || !Object)
			{
				UE_LOG(LogDreamWorldPlay, Error, TEXT("%s: could not build the asset."), *Name);
				return nullptr;
			}
			return SaveAsset(Object) ? Object : nullptr;
		}

		UMetaSoundSourceBuilder* Builder = nullptr;
		FMetaSoundBuilderNodeOutputHandle OnPlay;
		FMetaSoundBuilderNodeInputHandle OnFinished;
		TArray<FMetaSoundBuilderNodeInputHandle> AudioOut;

	private:
		FMetaSoundBuilderNodeOutputHandle AddInput(const TCHAR* InputName, const TCHAR* DataType, const FMetasoundFrontendLiteral& Literal)
		{
			EMetaSoundBuilderResult R = EMetaSoundBuilderResult::Failed;
			const FMetaSoundBuilderNodeOutputHandle H = Builder ? Builder->AddGraphInputNode(InputName, DataType, Literal, R) : FMetaSoundBuilderNodeOutputHandle();
			Check(R, *FString::Printf(TEXT("add input '%s' (%s)"), InputName, DataType));
			return H;
		}

		void Check(EMetaSoundBuilderResult R, const TCHAR* What)
		{
			if (R != EMetaSoundBuilderResult::Succeeded)
			{
				UE_LOG(LogDreamWorldPlay, Error, TEXT("%s: could not %s."), *Name, What);
				bOk = false;
			}
		}

		FString Name;
		bool bOk = true;
	};

	/**
	 * MS_DreamWeatherVoice: plays one sound wave (Wave) once or looped (Loop), low-passed by Muffle and with the
	 * left and right gains eased toward Left and Right. Intensity, Gust and Distance are accepted (and unused) so the
	 * same parameters can be sent to every voice.
	 */
	static bool BuildVoiceGraph(bool bForce)
	{
		FGraph G(TEXT("MS_DreamWeatherVoice"), 2);
		if (!G.Builder || G.AudioOut.Num() < 2)
		{
			return false;
		}
		const FMetaSoundBuilderNodeOutputHandle Wave = G.ObjectInput(*UDreamWeatherAudioSubsystem::WaveParam.ToString(), TEXT("WaveAsset"));
		const FMetaSoundBuilderNodeOutputHandle Loop = G.Input(*UDreamWeatherAudioSubsystem::LoopParam.ToString(), TEXT("Bool"), true);
		const FMetaSoundBuilderNodeOutputHandle Left = G.Input(*UDreamWeatherAudioSubsystem::LeftParam.ToString(), TEXT("Float"), 0.0f);
		const FMetaSoundBuilderNodeOutputHandle Right = G.Input(*UDreamWeatherAudioSubsystem::RightParam.ToString(), TEXT("Float"), 0.0f);
		const FMetaSoundBuilderNodeOutputHandle Muffle = G.Input(*UDreamWeatherAudioSubsystem::MuffleParam.ToString(), TEXT("Float"), 0.0f);
		const FMetaSoundBuilderNodeOutputHandle MinCutoff = G.Input(*UDreamWeatherAudioSubsystem::MinCutoffParam.ToString(), TEXT("Float"), 600.0f);
		G.Input(*UDreamWeatherAudioSubsystem::IntensityParam.ToString(), TEXT("Float"), 0.0f);
		G.Input(*UDreamWeatherAudioSubsystem::GustParam.ToString(), TEXT("Float"), 0.0f);
		G.Input(*UDreamWeatherAudioSubsystem::DistanceParam.ToString(), TEXT("Float"), 0.0f);

		const FMetaSoundNodeHandle Player = G.Node(TEXT("UE"), TEXT("Wave Player"), TEXT("Stereo"));
		G.Connect(G.OnPlay, G.In(Player, TEXT("Play")));
		G.Connect(Wave, G.In(Player, TEXT("Wave Asset")));
		G.Connect(Loop, G.In(Player, TEXT("Loop")));
		G.Connect(G.Out(Player, TEXT("On Finished")), G.OnFinished);

		const FMetaSoundBuilderNodeOutputHandle CutoffHz = G.Cutoff(Muffle, MinCutoff);
		const FMetaSoundBuilderNodeOutputHandle Channel[2] = { G.Out(Player, TEXT("Out Left")), G.Out(Player, TEXT("Out Right")) };
		const FMetaSoundBuilderNodeOutputHandle Level[2] = { Left, Right };
		for (int32 c = 0; c < 2; ++c)
		{
			G.Connect(G.Gain(G.LowPass(Channel[c], CutoffHz), G.Ease(Level[c], 0.15f)), G.AudioOut[c]);
		}
		return G.Finish(bForce) != nullptr;
	}

	/**
	 * MS_DreamAmbienceForest: birds by day (one of Birds every ~2 s, spread left and right), insects at night, the
	 * wind in the trees (TreeWindLight with any wind, TreeWindHeavy with a strong one); rain quiets the animals.
	 * Muffled and placed by Left / Right / Muffle like the weather's voices.
	 */
	static UObject* BuildForestGraph(bool bForce)
	{
		FGraph G(TEXT("MS_DreamAmbienceForest"), 2);
		if (!G.Builder || G.AudioOut.Num() < 2)
		{
			return nullptr;
		}
		const FMetaSoundBuilderNodeOutputHandle Birds = G.ObjectInput(TEXT("Birds"), TEXT("WaveAsset:Array"));
		const FMetaSoundBuilderNodeOutputHandle Insects = G.ObjectInput(TEXT("Insects"), TEXT("WaveAsset"));
		const FMetaSoundBuilderNodeOutputHandle WindLight = G.ObjectInput(TEXT("TreeWindLight"), TEXT("WaveAsset"));
		const FMetaSoundBuilderNodeOutputHandle WindHeavy = G.ObjectInput(TEXT("TreeWindHeavy"), TEXT("WaveAsset"));
		const FMetaSoundBuilderNodeOutputHandle Day = G.Input(TEXT("Day"), TEXT("Float"), 1.0f);
		const FMetaSoundBuilderNodeOutputHandle Night = G.Input(TEXT("Night"), TEXT("Float"), 0.0f);
		const FMetaSoundBuilderNodeOutputHandle Rain = G.Input(TEXT("Rain"), TEXT("Float"), 0.0f);
		const FMetaSoundBuilderNodeOutputHandle Wind = G.Input(TEXT("Wind"), TEXT("Float"), 0.0f);
		const FMetaSoundBuilderNodeOutputHandle Left = G.Input(TEXT("Left"), TEXT("Float"), 1.0f);
		const FMetaSoundBuilderNodeOutputHandle Right = G.Input(TEXT("Right"), TEXT("Float"), 1.0f);
		const FMetaSoundBuilderNodeOutputHandle Muffle = G.Input(TEXT("Muffle"), TEXT("Float"), 0.0f);
		const FMetaSoundBuilderNodeOutputHandle MinCutoff = G.Input(TEXT("MinCutoff"), TEXT("Float"), 600.0f);

		// Birds: a random one now and then, somewhere between left and right.
		const FMetaSoundNodeHandle Repeat = G.Node(TEXT("UE"), TEXT("TriggerRepeat"), TEXT(""));
		G.Connect(G.OnPlay, G.In(Repeat, TEXT("Start")));
		G.Default(G.In(Repeat, TEXT("Period")), 2.2f);
		const FMetaSoundNodeHandle Pick = G.Node(TEXT("Array"), TEXT("Random Get"), TEXT("WaveAsset:Array"));
		G.Connect(G.Out(Repeat, TEXT("RepeatOut")), G.In(Pick, TEXT("Next")));
		G.Connect(Birds, G.In(Pick, TEXT("In Array")));
		const FMetaSoundNodeHandle Bird = G.Node(TEXT("UE"), TEXT("Wave Player"), TEXT("Mono"));
		G.Connect(G.Out(Pick, TEXT("On Next")), G.In(Bird, TEXT("Play")));
		G.Connect(G.Out(Pick, TEXT("Value")), G.In(Bird, TEXT("Wave Asset")));
		const FMetaSoundNodeHandle Where = G.Node(TEXT("UE"), TEXT("RandomFloat"), TEXT(""));
		G.Connect(G.Out(Repeat, TEXT("RepeatOut")), G.In(Where, TEXT("Next")));
		G.Default(G.In(Where, TEXT("Min")), -0.8f);
		G.Default(G.In(Where, TEXT("Max")), 0.8f);
		const FMetaSoundNodeHandle Pan = G.Node(TEXT("UE"), TEXT("Stereo Panner"), TEXT(""));
		G.Connect(G.Out(Bird, TEXT("Out Mono")), G.In(Pan, TEXT("In")));
		G.Connect(G.Out(Where, TEXT("Value")), G.In(Pan, TEXT("Pan Amount")));

		const FMetaSoundNodeHandle InsectPlayer = G.WavePlayer(TEXT("Stereo"), Insects, true);
		const FMetaSoundNodeHandle LightPlayer = G.WavePlayer(TEXT("Stereo"), WindLight, true);
		const FMetaSoundNodeHandle HeavyPlayer = G.WavePlayer(TEXT("Stereo"), WindHeavy, true);

		const FMetaSoundBuilderNodeOutputHandle Dry = G.OneMinus(Rain);
		const FMetaSoundBuilderNodeOutputHandle BirdLevel = G.Ease(G.MulF(Day, Dry), 1.0f);
		const FMetaSoundBuilderNodeOutputHandle InsectLevel = G.Ease(G.MulF(Night, Dry), 1.0f);
		const FMetaSoundBuilderNodeOutputHandle LightLevel = G.Ease(Wind, 0.5f);
		const FMetaSoundBuilderNodeOutputHandle HeavyLevel = G.Ease(G.MulF(Wind, Wind), 0.5f);
		const FMetaSoundBuilderNodeOutputHandle CutoffHz = G.Cutoff(Muffle, MinCutoff);

		const TCHAR* Pins[2] = { TEXT("Out Left"), TEXT("Out Right") };
		const FMetaSoundBuilderNodeOutputHandle Side[2] = { Left, Right };
		for (int32 c = 0; c < 2; ++c)
		{
			FMetaSoundBuilderNodeOutputHandle Mix = G.Gain(G.Out(Pan, Pins[c]), BirdLevel);
			Mix = G.Sum(Mix, G.Gain(G.Out(InsectPlayer, Pins[c]), InsectLevel));
			Mix = G.Sum(Mix, G.Gain(G.Out(LightPlayer, Pins[c]), LightLevel));
			Mix = G.Sum(Mix, G.Gain(G.Out(HeavyPlayer, Pins[c]), HeavyLevel));
			G.Connect(G.Gain(G.LowPass(Mix, CutoffHz), G.Ease(Side[c], 0.3f)), G.AudioOut[c]);
		}
		return G.Finish(bForce);
	}

	static UDreamAmbiencePreset* EnsureForestPreset(UObject* Graph, bool bForce)
	{
		static const TCHAR* Name = TEXT("DAP_DreamForest");
		const FString PackageName = FString(AudioPath) / Name;
		UDreamAmbiencePreset* Preset = LoadObject<UDreamAmbiencePreset>(nullptr, *(PackageName + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Preset && !bForce)
		{
			return Preset;
		}
		if (!Preset)
		{
			Preset = NewObject<UDreamAmbiencePreset>(CreatePackage(*PackageName), Name, RF_Public | RF_Standalone);
		}
		Preset->Sound = Cast<USoundBase>(Graph);
		Preset->Inputs.Reset();
		// Empty slots: the project fills them (Project Settings > Dream Weather Audio > Sounds > Ambience).
		for (const TPair<const TCHAR*, bool>& Input : { TPair<const TCHAR*, bool>(TEXT("Birds"), true), TPair<const TCHAR*, bool>(TEXT("Insects"), false),
			TPair<const TCHAR*, bool>(TEXT("TreeWindLight"), false), TPair<const TCHAR*, bool>(TEXT("TreeWindHeavy"), false) })
		{
			FDreamAmbienceInput& New = Preset->Inputs.AddDefaulted_GetRef();
			New.Input = Input.Key;
			New.bArray = Input.Value;
		}
		Preset->bOutdoor = true;
		return SaveAsset(Preset) ? Preset : nullptr;
	}

	static bool EnsureSoundClass(const TCHAR* Name)
	{
		const FString PackageName = FString(AudioPath) / Name;
		if (LoadObject<USoundClass>(nullptr, *(PackageName + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return true;
		}
		USoundClass* Class = NewObject<USoundClass>(CreatePackage(*PackageName), Name, RF_Public | RF_Standalone);
		return SaveAsset(Class);
	}

	bool EnsureAudioAssets(bool bForce)
	{
		bool bOk = EnsureSoundClass(TEXT("SC_DreamWeather"));
		bOk &= EnsureSoundClass(TEXT("SC_DreamOutdoor"));
		bOk &= BuildVoiceGraph(bForce);
		UObject* Forest = BuildForestGraph(bForce);
		bOk &= Forest != nullptr;
		bOk &= Forest && EnsureForestPreset(Forest, bForce) != nullptr;
		return bOk;
	}
}

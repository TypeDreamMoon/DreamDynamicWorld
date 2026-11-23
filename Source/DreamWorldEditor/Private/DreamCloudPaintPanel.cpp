// Copyright DreamMoon. All Rights Reserved.
//
// The Dream Cloud Paint tab: the level's cloud painting seen from above, north up, painted with the mouse.

#include "DreamWorldEditorUI.h"
#include "DreamWorldEditorFactories.h"
#include "DreamWeatherCloudPaint.h"

#include "AssetToolsModule.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/Selection.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "EngineUtils.h"
#include "LevelEditorViewport.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DreamWorldEditor"

const FName DreamWorld::EditorUI::CloudPaintTabName(TEXT("DreamCloudPaint"));

namespace DreamCloudPaintPanelImpl
{
	using namespace DreamWorld::EditorUI;

	enum class EMode : uint8 { Clouds, ClearSky, Erase };

	/** The level's cloud painting actor, or null. */
	static ADreamCloudPaint* FindPaintActor()
	{
		UWorld* World = EditedWorld();
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<ADreamCloudPaint> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	/** The preview's colour of a texel: the weather's own a dark slate, the painting from a clear blue to overcast grey. */
	static FColor PreviewColor(uint8 Cover, uint8 Amount)
	{
		const FLinearColor Own(0.05f, 0.065f, 0.085f);
		const FLinearColor Clear(0.16f, 0.32f, 0.5f);
		const FLinearColor Overcast(0.78f, 0.79f, 0.8f);
		const FLinearColor Painted = FMath::Lerp(Clear, Overcast, Cover / 255.0f);
		return FMath::Lerp(Own, Painted, Amount / 255.0f).ToFColor(true);
	}

	class SPanel;

	class SCanvas : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SCanvas) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, SPanel* InPanel) { Panel = InPanel; }

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(512.0, 512.0); }
		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Elements, int32 LayerId,
			const FWidgetStyle& Style, bool bParentEnabled) const override;
		virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
		virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
		virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
		virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
		virtual void OnMouseCaptureLost(const FCaptureLostEvent& Event) override;

		/** The square the painting is drawn in, and a point of it in the painting's UV (north up: V across, U up). */
		static void Square(const FGeometry& Geometry, FVector2f& OutMin, float& OutSide);
		static FVector2D ToUV(const FGeometry& Geometry, const FVector2f& Local);
		static FVector2f ToLocal(const FGeometry& Geometry, const FVector2D& UV);

	private:
		void Stroke(const FGeometry& Geometry, const FPointerEvent& Event, bool bFirst);

		SPanel* Panel = nullptr;
		bool bPainting = false;
		FVector2D LastUV = FVector2D::ZeroVector;
	};

	class SPanel : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SPanel) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&)
		{
			ChildSlot
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(6.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text_Lambda([this] { return TargetText(); }).AutoWrapText(true)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("AddPaint", "Add to Level"))
						.ToolTipText(LOCTEXT("AddPaintTip", "Places a Dream Cloud Paint over the view (40 km square) with a new painting in /Game/DreamWorld/CloudPaint."))
						.Visibility_Lambda([] { return FindPaintActor() ? EVisibility::Collapsed : EVisibility::Visible; })
						.OnClicked_Lambda([this] { AddToLevel(); return FReply::Handled(); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("NewPainting", "New Painting"))
						.ToolTipText(LOCTEXT("NewPaintingTip", "Gives the level's Dream Cloud Paint a new, blank painting."))
						.Visibility_Lambda([] { const ADreamCloudPaint* A = FindPaintActor(); return A && !A->CloudPaint->Paint ? EVisibility::Visible : EVisibility::Collapsed; })
						.OnClicked_Lambda([this] { if (ADreamCloudPaint* A = FindPaintActor()) { GivePainting(*A); } return FReply::Handled(); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("SelectPaint", "Select"))
						.Visibility_Lambda([] { return FindPaintActor() ? EVisibility::Visible : EVisibility::Collapsed; })
						.OnClicked_Lambda([] { if (ADreamCloudPaint* A = FindPaintActor()) { GEditor->SelectNone(false, true); GEditor->SelectActor(A, true, true); } return FReply::Handled(); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(6.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ ModeButton(EMode::Clouds, LOCTEXT("ModeClouds", "Clouds")) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ ModeButton(EMode::ClearSky, LOCTEXT("ModeClear", "Clear Sky")) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ ModeButton(EMode::Erase, LOCTEXT("ModeErase", "Erase")) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.0f, 0.0f, 4.0f, 0.0f)[ SNew(STextBlock).Text(LOCTEXT("Radius", "Radius km")) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(70.0f)
						[
							SNew(SSpinBox<float>).MinValue(0.1f).MaxValue(50.0f).Delta(0.1f).Value_Lambda([this] { return RadiusKm; }).OnValueChanged_Lambda([this](float V) { RadiusKm = V; })
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.0f, 0.0f, 4.0f, 0.0f)[ SNew(STextBlock).Text(LOCTEXT("Strength", "Strength")) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(70.0f)
						[
							SNew(SSpinBox<float>).MinValue(0.02f).MaxValue(1.0f).Delta(0.02f).Value_Lambda([this] { return Strength; }).OnValueChanged_Lambda([this](float V) { Strength = V; })
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(6.0f, 4.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SButton).Text(LOCTEXT("FillNone", "Clear the Painting")).OnClicked_Lambda([this] { Fill(0.0f, 0.0f); return FReply::Handled(); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SButton).Text(LOCTEXT("FillOvercast", "Overcast Everywhere")).OnClicked_Lambda([this] { Fill(1.0f, 1.0f); return FReply::Handled(); })
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton).Text(LOCTEXT("FillClear", "Clear Sky Everywhere")).OnClicked_Lambda([this] { Fill(0.0f, 1.0f); return FReply::Handled(); })
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f).Padding(6.0f)
				[
					SNew(SCanvas, this)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(6.0f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(LOCTEXT("Help", "North up, east right; the dot is the viewport. Drag to paint, Shift-drag to erase, Ctrl-drag for clear sky, the wheel sizes the brush. Where it is painted the painting has the last word on the cloud cover (over the weather zones); rain follows the cover. Undo works per stroke."))
				]
			];
		}

		UDreamCloudPaintComponent* Target() const
		{
			ADreamCloudPaint* Actor = FindPaintActor();
			return Actor ? Actor->CloudPaint.Get() : nullptr;
		}

		UDreamCloudPainting* Painting() const
		{
			const UDreamCloudPaintComponent* C = Target();
			return C ? C->Paint.Get() : nullptr;
		}

		FText TargetText() const
		{
			const ADreamCloudPaint* Actor = FindPaintActor();
			if (!Actor)
			{
				return LOCTEXT("NoPaint", "The level has no Dream Cloud Paint.");
			}
			const UDreamCloudPainting* P = Actor->CloudPaint->Paint;
			return FText::Format(LOCTEXT("PaintTarget", "{0}: {1}, {2} km square"), FText::FromString(Actor->GetActorLabel()),
				P ? FText::FromString(P->GetPathName()) : LOCTEXT("NoPainting", "no painting"), FText::AsNumber(Actor->CloudPaint->Size / 100000.0));
		}

		TSharedRef<SWidget> ModeButton(EMode InMode, const FText& Label)
		{
			return SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked_Lambda([this, InMode] { return Mode == InMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, InMode](ECheckBoxState) { Mode = InMode; })
				[
					SNew(STextBlock).Text(Label).Margin(FMargin(8.0f, 2.0f))
				];
		}

		UDreamCloudPainting* CreatePainting(const FString& BaseName) const
		{
			IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			FString Package, Name;
			Tools.CreateUniqueAssetName(TEXT("/Game/DreamWorld/CloudPaint/") + BaseName, TEXT(""), Package, Name);
			return Cast<UDreamCloudPainting>(Tools.CreateAsset(Name, FPackageName::GetLongPackagePath(Package), UDreamCloudPainting::StaticClass(), NewObject<UDreamCloudPaintFactory>()));
		}

		void GivePainting(ADreamCloudPaint& Actor) const
		{
			UWorld* World = Actor.GetWorld();
			const FString Base = TEXT("CP_") + (World ? World->GetMapName() : FString(TEXT("Level")));
			if (UDreamCloudPainting* Paint = CreatePainting(Base))
			{
				Actor.Modify();
				Actor.CloudPaint->Modify();
				Actor.CloudPaint->Paint = Paint;
				Actor.CloudPaint->ReregisterComponent();
			}
		}

		void AddToLevel()
		{
			UWorld* World = EditedWorld();
			if (!World)
			{
				return;
			}
			FVector At = FVector::ZeroVector;
			if (GCurrentLevelEditingViewportClient)
			{
				At = GCurrentLevelEditingViewportClient->GetViewLocation();
				At.Z = 0.0;
			}
			const FScopedTransaction Transaction(LOCTEXT("AddCloudPaint", "Add Dream Cloud Paint"));
			ADreamCloudPaint* Actor = World->SpawnActor<ADreamCloudPaint>(At, FRotator::ZeroRotator);
			if (Actor)
			{
				Actor->SetActorLabel(TEXT("CloudPaint"));
				GivePainting(*Actor);
				GEditor->SelectNone(false, true);
				GEditor->SelectActor(Actor, true, true);
			}
		}

		void Fill(float Cover, float Amount)
		{
			if (UDreamCloudPainting* Paint = Painting())
			{
				const FScopedTransaction Transaction(LOCTEXT("FillCloudPaint", "Fill Cloud Painting"));
				Paint->Modify();
				Paint->Fill(Cover, Amount);
				Paint->MarkPackageDirty();
				GEditor->RedrawLevelEditingViewports();
			}
		}

		/** The preview texture, brought up to the painting's revision. */
		const FSlateBrush* PreviewBrush()
		{
			UDreamCloudPainting* Paint = Painting();
			if (!Paint || !Paint->IsValidPainting())
			{
				return nullptr;
			}
			const int32 N = Paint->Resolution;
			if (!Preview.IsValid() || Preview->GetSizeX() != N)
			{
				Preview.Reset(UTexture2D::CreateTransient(N, N, PF_B8G8R8A8));
				if (!Preview.IsValid())
				{
					return nullptr;
				}
				Preview->SRGB = true;
				Preview->Filter = TF_Bilinear;
				Preview->UpdateResource();
				PreviewRevision = 0;
				Brush = FSlateBrush();
				Brush.SetResourceObject(Preview.Get());
				Brush.ImageSize = FVector2D(N, N);
			}
			if (PreviewRevision != Paint->GetRevision())
			{
				// North up, east right: the screen's x is the painting's V, its y the painting's U from the top.
				FColor* Pixels = static_cast<FColor*>(FMemory::Malloc(N * N * sizeof(FColor)));
				for (int32 SY = 0; SY < N; ++SY)
				{
					for (int32 SX = 0; SX < N; ++SX)
					{
						const int32 I = SX * N + (N - 1 - SY);
						Pixels[SY * N + SX] = PreviewColor(Paint->Cover[I], Paint->Amount[I]);
					}
				}
				FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, N, N);
				Preview->UpdateTextureRegions(0, 1, Region, uint32(N * sizeof(FColor)), sizeof(FColor), reinterpret_cast<uint8*>(Pixels),
					[](uint8* Data, const FUpdateTextureRegion2D* Regions)
					{
						FMemory::Free(Data);
						delete Regions;
					});
				PreviewRevision = Paint->GetRevision();
			}
			return &Brush;
		}

		EMode Mode = EMode::Clouds;
		float RadiusKm = 2.0f;
		float Strength = 0.35f;

	private:
		TStrongObjectPtr<UTexture2D> Preview;
		uint32 PreviewRevision = 0;
		FSlateBrush Brush;
	};

	void SCanvas::Square(const FGeometry& Geometry, FVector2f& OutMin, float& OutSide)
	{
		const FVector2f Size(Geometry.GetLocalSize());
		OutSide = FMath::Min(Size.X, Size.Y);
		OutMin = (Size - FVector2f(OutSide)) * 0.5f;
	}

	FVector2D SCanvas::ToUV(const FGeometry& Geometry, const FVector2f& Local)
	{
		FVector2f Min;
		float Side;
		Square(Geometry, Min, Side);
		const FVector2f S = (Local - Min) / FMath::Max(Side, 1.0f);
		return FVector2D(1.0 - S.Y, S.X);
	}

	FVector2f SCanvas::ToLocal(const FGeometry& Geometry, const FVector2D& UV)
	{
		FVector2f Min;
		float Side;
		Square(Geometry, Min, Side);
		return Min + FVector2f(float(UV.Y), float(1.0 - UV.X)) * Side;
	}

	int32 SCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Elements, int32 LayerId,
		const FWidgetStyle& Style, bool bParentEnabled) const
	{
		FVector2f Min;
		float Side;
		Square(Geometry, Min, Side);
		const FPaintGeometry Area = Geometry.ToPaintGeometry(FVector2f(Side), FSlateLayoutTransform(Min));
		if (const FSlateBrush* Brush = Panel->PreviewBrush())
		{
			FSlateDrawElement::MakeBox(Elements, LayerId, Area, Brush, ESlateDrawEffect::None, FLinearColor::White);
		}
		else
		{
			FSlateDrawElement::MakeBox(Elements, LayerId, Area, FAppStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None, FLinearColor(0.03f, 0.03f, 0.035f));
		}
		const UDreamCloudPaintComponent* Target = Panel->Target();
		if (Target && Target->Size > 0.0)
		{
			// A line every 5 km.
			const FLinearColor Grid(1.0f, 1.0f, 1.0f, 0.07f);
			const double Step = 500000.0 / Target->Size;
			for (double T = Step; T < 1.0; T += Step)
			{
				FSlateDrawElement::MakeLines(Elements, LayerId + 1, Geometry.ToPaintGeometry(), TArray<FVector2f>{ Min + FVector2f(float(T) * Side, 0.0f), Min + FVector2f(float(T) * Side, Side) },
					ESlateDrawEffect::None, Grid);
				FSlateDrawElement::MakeLines(Elements, LayerId + 1, Geometry.ToPaintGeometry(), TArray<FVector2f>{ Min + FVector2f(0.0f, float(T) * Side), Min + FVector2f(Side, float(T) * Side) },
					ESlateDrawEffect::None, Grid);
			}
			// The viewport: a dot and its heading.
			if (GCurrentLevelEditingViewportClient)
			{
				const FVector2D UV = Target->WorldToUV(GCurrentLevelEditingViewportClient->GetViewLocation());
				const FVector Forward = GCurrentLevelEditingViewportClient->GetViewRotation().Vector();
				const FVector2D Ahead = Target->WorldToUV(GCurrentLevelEditingViewportClient->GetViewLocation() + Forward.GetSafeNormal2D() * (Target->Size * 0.04));
				const FVector2f At = ToLocal(Geometry, UV);
				const FLinearColor Mark(0.95f, 0.75f, 0.35f);
				FSlateDrawElement::MakeLines(Elements, LayerId + 2, Geometry.ToPaintGeometry(), TArray<FVector2f>{ At, ToLocal(Geometry, Ahead) }, ESlateDrawEffect::None, Mark, true, 2.0f);
				FSlateDrawElement::MakeBox(Elements, LayerId + 2, Geometry.ToPaintGeometry(FVector2f(8.0f), FSlateLayoutTransform(At - FVector2f(4.0f))),
					FAppStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None, Mark);
			}
			// The brush's size where the mouse is.
			if (IsHovered())
			{
				const FVector2f Mouse = Geometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
				const float R = float(Panel->RadiusKm * 100000.0 / Target->Size) * Side;
				TArray<FVector2f> Ring;
				for (int32 i = 0; i <= 48; ++i)
				{
					const float A = i * 2.0f * UE_PI / 48.0f;
					Ring.Add(Mouse + FVector2f(FMath::Cos(A), FMath::Sin(A)) * R);
				}
				FSlateDrawElement::MakeLines(Elements, LayerId + 2, Geometry.ToPaintGeometry(), Ring, ESlateDrawEffect::None, FLinearColor(1.0f, 1.0f, 1.0f, 0.6f), true, 1.0f);
			}
		}
		FSlateDrawElement::MakeText(Elements, LayerId + 3, Geometry.ToPaintGeometry(FVector2f(20.0f, 20.0f), FSlateLayoutTransform(Min + FVector2f(Side * 0.5f - 5.0f, 4.0f))),
			FString(TEXT("N")), FAppStyle::GetFontStyle("NormalFontBold"), ESlateDrawEffect::None, FLinearColor(1.0f, 1.0f, 1.0f, 0.7f));
		return LayerId + 4;
	}

	void SCanvas::Stroke(const FGeometry& Geometry, const FPointerEvent& Event, bool bFirst)
	{
		UDreamCloudPaintComponent* Target = Panel->Target();
		UDreamCloudPainting* Paint = Target ? Target->Paint.Get() : nullptr;
		if (!Paint)
		{
			return;
		}
		const FVector2D UV = ToUV(Geometry, Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
		const double Radius = Panel->RadiusKm * 100000.0 / FMath::Max(Target->Size, 1.0);
		EMode Mode = Panel->Mode;
		if (Event.IsShiftDown())
		{
			Mode = EMode::Erase;
		}
		else if (Event.IsControlDown())
		{
			Mode = EMode::ClearSky;
		}
		// Dabs a quarter of the radius apart along the drag, so a fast stroke stays continuous.
		const FVector2D From = bFirst ? UV : LastUV;
		const int32 Dabs = bFirst ? 1 : FMath::Clamp(FMath::CeilToInt(FVector2D::Distance(From, UV) / FMath::Max(Radius * 0.25, 1.0e-4)), 1, 64);
		bool bChanged = false;
		for (int32 i = 1; i <= Dabs; ++i)
		{
			const FVector2D At = bFirst ? UV : FMath::Lerp(From, UV, double(i) / Dabs);
			bChanged |= Paint->PaintAt(At, float(Radius), Mode == EMode::Clouds ? 1.0f : 0.0f, Panel->Strength * (bFirst ? 1.0f : 0.5f), Mode == EMode::Erase);
		}
		LastUV = UV;
		if (bChanged)
		{
			Paint->MarkPackageDirty();
			GEditor->RedrawLevelEditingViewports();
		}
	}

	FReply SCanvas::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
	{
		UDreamCloudPainting* Paint = Panel->Painting();
		if (Event.GetEffectingButton() != EKeys::LeftMouseButton || !Paint)
		{
			return FReply::Unhandled();
		}
		bPainting = true;
		GEditor->BeginTransaction(LOCTEXT("PaintClouds", "Paint Cloud Cover"));
		Paint->Modify();
		Stroke(Geometry, Event, true);
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	FReply SCanvas::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
	{
		if (bPainting && HasMouseCapture())
		{
			Stroke(Geometry, Event, false);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	FReply SCanvas::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
	{
		if (bPainting && Event.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			bPainting = false;
			GEditor->EndTransaction();
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Unhandled();
	}

	void SCanvas::OnMouseCaptureLost(const FCaptureLostEvent& Event)
	{
		if (bPainting)
		{
			bPainting = false;
			GEditor->EndTransaction();
		}
	}

	FReply SCanvas::OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
	{
		Panel->RadiusKm = FMath::Clamp(Panel->RadiusKm * (Event.GetWheelDelta() > 0.0f ? 1.15f : 1.0f / 1.15f), 0.1f, 50.0f);
		return FReply::Handled();
	}
}

TSharedRef<SWidget> DreamWorld::EditorUI::MakeCloudPaintPanel()
{
	return SNew(DreamCloudPaintPanelImpl::SPanel);
}

TSharedRef<SDockTab> DreamWorld::EditorUI::SpawnCloudPaintTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("CloudPaintTabLabel", "Dream Cloud Paint"))
		[
			SNew(DreamCloudPaintPanelImpl::SPanel)
		];
}

#undef LOCTEXT_NAMESPACE

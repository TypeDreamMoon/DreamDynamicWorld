// Copyright DreamMoon. All Rights Reserved.
//
// DreamWorld.Editor.Snapshot [Dir]: pictures of the editor tools (the toolbar's Dream World menu, the cloud painting
// tab), drawn off screen into PNGs, for the documentation and for checking them without a window.

#include "DreamWorldEditorUI.h"

#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "ToolMenus.h"

namespace DreamWorldEditorSnapshotImpl
{
	static const FName MenuName(TEXT("DreamWorld.Menu"));

	static bool Save(const TSharedRef<SWidget>& Widget, const FVector2D& Size, const FString& Path)
	{
		FWidgetRenderer Renderer(true, true);
		UTextureRenderTarget2D* Target = FWidgetRenderer::CreateTargetFor(Size, TF_Bilinear, true);
		if (!Target)
		{
			return false;
		}
		Target->ClearColor = FLinearColor(0.02f, 0.02f, 0.024f, 1.0f);
		// Twice: the first lays it out (and lets textures it makes reach the GPU).
		for (int32 i = 0; i < 3; ++i)
		{
			Renderer.DrawWidget(Target, Widget, Size, 0.016f);
			FlushRenderingCommands();
		}
		TArray<FColor> Pixels;
		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != int32(Size.X) * int32(Size.Y))
		{
			return false;
		}
		for (FColor& P : Pixels)
		{
			P.A = 255;
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(int32(Size.X), int32(Size.Y), TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		return FFileHelper::SaveArrayToFile(Png, *Path);
	}

	static void Snapshot(const TArray<FString>& Args)
	{
		const FString Dir = Args.Num() > 0 ? Args[0] : FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("DreamWorldEditor"));
		IFileManager::Get().MakeDirectory(*Dir, true);
		UToolMenus* Menus = UToolMenus::Get();
		if (Menus && !Menus->IsMenuRegistered(MenuName))
		{
			UToolMenu* Menu = Menus->RegisterMenu(MenuName);
			Menu->AddDynamicSection("DreamWorld", FNewToolMenuDelegate::CreateStatic(&DreamWorld::EditorUI::FillMenu));
		}
		const bool bMenu = Menus && Save(Menus->GenerateWidget(MenuName, FToolMenuContext()), FVector2D(300.0, 400.0), FPaths::Combine(Dir, TEXT("DreamWorldMenu.png")));
		const bool bPaint = Save(DreamWorld::EditorUI::MakeCloudPaintPanel(), FVector2D(640.0, 820.0), FPaths::Combine(Dir, TEXT("DreamCloudPaint.png")));
		UE_LOG(LogTemp, Display, TEXT("DreamWorld.Editor.Snapshot: menu %s, cloud paint tab %s, in %s"), bMenu ? TEXT("saved") : TEXT("FAILED"), bPaint ? TEXT("saved") : TEXT("FAILED"), *Dir);
	}

	static FAutoConsoleCommand Command(TEXT("DreamWorld.Editor.Snapshot"),
		TEXT("Draws the editor tools (the toolbar's Dream World menu, the cloud painting tab) off screen into PNGs: DreamWorld.Editor.Snapshot [Dir] (default Saved/Screenshots/DreamWorldEditor)."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&Snapshot));
}

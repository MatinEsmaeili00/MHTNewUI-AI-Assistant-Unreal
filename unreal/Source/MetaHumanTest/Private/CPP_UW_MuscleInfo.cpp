#include "CPP_UW_MuscleInfo.h"

#include "AnatomyPickerComp.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

void UCPP_UW_MuscleInfo::NativeConstruct()
{
	Super::NativeConstruct();

	TooltipBox->SetVisibility(ESlateVisibility::Collapsed);
	PopupBox->SetVisibility(ESlateVisibility::Collapsed);
	if (PopupCloseBtn)
	{
		PopupCloseBtn->OnClicked.AddUniqueDynamic(this, &UCPP_UW_MuscleInfo::HandleCloseClicked);
	}

	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &UCPP_UW_MuscleInfo::HandlePossessedPawnChanged);
		BindToPawn(PC->GetPawn());
	}
}

void UCPP_UW_MuscleInfo::NativeDestruct()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->OnPossessedPawnChanged.RemoveDynamic(this, &UCPP_UW_MuscleInfo::HandlePossessedPawnChanged);
	}
	BindToPawn(nullptr);
	Super::NativeDestruct();
}

void UCPP_UW_MuscleInfo::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	BindToPawn(NewPawn);
}

void UCPP_UW_MuscleInfo::BindToPawn(APawn* Pawn)
{
	for (UAnatomyPickerComp* Old : Pickers)
	{
		if (IsValid(Old))
		{
			Old->OnHoverChanged.RemoveDynamic(this, &UCPP_UW_MuscleInfo::HandleHoverChanged);
			Old->OnSelectionChanged.RemoveDynamic(this, &UCPP_UW_MuscleInfo::HandleSelectionChanged);
		}
	}
	Pickers.Reset();
	Picker = nullptr;

	TArray<UAnatomyPickerComp*> Found;
	if (Pawn)
	{
		Pawn->GetComponents<UAnatomyPickerComp>(Found);
	}
	TooltipBox->SetVisibility(ESlateVisibility::Collapsed);
	PopupBox->SetVisibility(ESlateVisibility::Collapsed);

	for (UAnatomyPickerComp* New : Found)
	{
		New->OnHoverChanged.AddUniqueDynamic(this, &UCPP_UW_MuscleInfo::HandleHoverChanged);
		New->OnSelectionChanged.AddUniqueDynamic(this, &UCPP_UW_MuscleInfo::HandleSelectionChanged);
		Pickers.Add(New);
	}
	Picker = Pickers.Num() > 0 ? Pickers[0].Get() : nullptr;
}

UAnatomyPickerComp* UCPP_UW_MuscleInfo::FindReporter(int32 PartId, bool bSelection) const
{
	if (PartId <= 0)
	{
		return nullptr;
	}
	for (UAnatomyPickerComp* Candidate : Pickers)
	{
		if (IsValid(Candidate) && (bSelection ? Candidate->GetSelectedMuscle() : Candidate->GetHoveredMuscle()) == PartId)
		{
			return Candidate;
		}
	}
	return nullptr;
}

void UCPP_UW_MuscleInfo::SetPopupLine(UTextBlock* Line, const FText& Format, const FText& Value)
{
	if (!Line)
	{
		return;
	}
	Line->SetText(FText::Format(Format, Value));
	Line->SetVisibility(Value.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void UCPP_UW_MuscleInfo::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (Pickers.Num() == 0 || !IsValid(Pickers[0]))
	{
		RebindCooldown -= InDeltaTime;
		if (RebindCooldown <= 0.f)
		{
			RebindCooldown = 0.5f;
			BindToPawn(GetOwningPlayerPawn());
		}
		return;
	}

	// Keep the tooltip next to the mouse.
	if (TooltipBox->GetVisibility() != ESlateVisibility::Collapsed)
	{
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(TooltipBox->Slot))
		{
			CanvasSlot->SetPosition(UWidgetLayoutLibrary::GetMousePositionOnViewport(this) + TooltipOffset);
		}
	}
}

void UCPP_UW_MuscleInfo::HandleHoverChanged(int32 MuscleId)
{
	// Several pickers report here (muscles, skeleton): show the one that is hovering something.
	FMuscleInfo Info;
	UAnatomyPickerComp* Reporter = FindReporter(MuscleId, /*bSelection*/ false);
	if (Reporter && Reporter->GetMuscleInfo(MuscleId, Info))
	{
		Picker = Reporter;
		MuscleNameText->SetText(FText::Format(NSLOCTEXT("MuscleInfo", "Tooltip", "{0} ({1})"), Info.Name, Info.Side));
		TooltipBox->SetVisibility(ESlateVisibility::HitTestInvisible);
		return;
	}
	// A picker stopped hovering: only hide the tooltip if no other picker is hovering something.
	for (UAnatomyPickerComp* Other : Pickers)
	{
		if (IsValid(Other) && Other->GetHoveredMuscle() > 0)
		{
			return;
		}
	}
	TooltipBox->SetVisibility(ESlateVisibility::Collapsed);
}

void UCPP_UW_MuscleInfo::HandleSelectionChanged(int32 MuscleId)
{
	FMuscleInfo Info;
	UAnatomyPickerComp* Reporter = FindReporter(MuscleId, /*bSelection*/ true);
	if (Reporter && Reporter->GetMuscleInfo(MuscleId, Info))
	{
		Picker = Reporter;
		// One popup at a time: a new selection in one layer clears the other layers' selection.
		for (UAnatomyPickerComp* Other : Pickers)
		{
			if (IsValid(Other) && Other != Reporter && Other->GetSelectedMuscle() > 0)
			{
				Other->ClearSelection();
			}
		}
		PopupTitle->SetText(FText::Format(NSLOCTEXT("MuscleInfo", "Title", "{0} - {1}"), Info.Name, Info.Side));
		PopupRegion->SetText(Info.Region);
		SetPopupLine(PopupFunction, NSLOCTEXT("MuscleInfo", "Function", "What it does: {0}"), Info.Function);
		SetPopupLine(PopupRunning, NSLOCTEXT("MuscleInfo", "Running", "When running: {0}"), Info.Running);
		SetPopupLine(PopupInjury, NSLOCTEXT("MuscleInfo", "Injury", "Watch out for: {0}"), Info.Injury);
		// Off by default: the assistant's chat box shows the description (see the header).
		PopupBox->SetVisibility(bShowPopupOnSelect ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		return;
	}
	for (UAnatomyPickerComp* Other : Pickers)
	{
		if (IsValid(Other) && Other->GetSelectedMuscle() > 0)
		{
			return; // another layer still has a selection
		}
	}
	PopupBox->SetVisibility(ESlateVisibility::Collapsed);
}

void UCPP_UW_MuscleInfo::HandleCloseClicked()
{
	for (UAnatomyPickerComp* Each : Pickers)
	{
		if (IsValid(Each))
		{
			Each->ClearSelection(); // fires HandleSelectionChanged(0)
		}
	}
	PopupBox->SetVisibility(ESlateVisibility::Collapsed);
}

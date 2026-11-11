// Muscle info widget (WB_MuscleInfo): a small name tooltip that follows the mouse while a muscle (or bone) is
// hovered. It listens to every UAnatomyPickerComp on the possessed pawn (one per pickable layer: muscles, skeleton)
// and shows whichever one reported last.
//
// A CLICK no longer opens this widget's info popup: the description of the clicked muscle / bone is written into the
// assistant's chat box instead (UIHPCAssistantComp, which listens to the same pickers). Tick "Show Popup On Select"
// to get the old popup back. Either way this widget keeps one selection at a time across the layers.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CPP_UW_MuscleInfo.generated.h"

class UAnatomyPickerComp;
class UButton;
class UTextBlock;
class UWidget;

UCLASS()
class METAHUMANTEST_API UCPP_UW_MuscleInfo : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The picker the tooltip / popup is currently showing (the last one that reported a hover or selection). */
	UPROPERTY(BlueprintReadOnly, Category="Muscle Info")
	TObjectPtr<UAnatomyPickerComp> Picker = nullptr;

	/** Every picker on the possessed pawn (muscles, skeleton, ...). */
	UPROPERTY(BlueprintReadOnly, Category="Muscle Info")
	TArray<TObjectPtr<UAnatomyPickerComp>> Pickers;

	/** Show the info popup when a muscle / bone is clicked. Off: the description goes to the assistant's chat box
	 *  (UIHPCAssistantComp) and the popup stays hidden. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Muscle Info")
	bool bShowPopupOnSelect = false;

	/** Tooltip offset from the mouse cursor (viewport units). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Muscle Info")
	FVector2D TooltipOffset = FVector2D(18.0, 14.0);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Tooltip: a panel (in a Canvas Panel slot) with the muscle name.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UWidget> TooltipBox;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> MuscleNameText;

	// Popup
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UWidget> PopupBox;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> PopupTitle;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> PopupRegion;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> PopupFunction;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> PopupRunning;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> PopupInjury;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> PopupCloseBtn;

	UFUNCTION() void HandleHoverChanged(int32 MuscleId);
	UFUNCTION() void HandleSelectionChanged(int32 MuscleId);
	UFUNCTION() void HandleCloseClicked();
	UFUNCTION() void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	void BindToPawn(APawn* Pawn);

	/** The picker whose hovered (bSelection = false) or selected (true) part is PartId, or null. */
	UAnatomyPickerComp* FindReporter(int32 PartId, bool bSelection) const;

	/** Fill and show / hide one popup line ("What it does: ..."): hidden when the atlas has no text for it. */
	void SetPopupLine(UTextBlock* Line, const FText& Format, const FText& Value);

private:
	float RebindCooldown = 0.f;
};

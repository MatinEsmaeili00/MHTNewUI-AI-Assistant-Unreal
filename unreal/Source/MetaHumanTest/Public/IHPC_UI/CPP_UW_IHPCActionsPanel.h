// CPP_UW_IHPCActionsPanel.h
// -----------------------------------------------------------------------------
// Content of the "Action" side panel (side bar tab: running man). Figma page "FINAL FESTIVAL", frame 19:
// a column of cards - Idle, Running, Jogging ("Jugging" in Figma), Jumping - one per action.
// The project's existing actions (Tennis Forehand, Soccer Warmup) follow below them; the list scrolls.
//
// Clicking a card plays that action on the possessed pawn's UAnimationTimelineComp (the same component and the same
// per-pawn "Action Clips" the old Actions menu used):
//   * a card whose clip the pawn has      -> PlayAction(<ActionName>); clicking the playing card again = back to Idle
//   * the Idle card (bIsDefault)          -> the pawn's "Idle" clip if it has one, else StopAction (its own
//                                            walk / jog / run animation, at the speed it had before)
//   * no clip but LocomotionSpeed >= 0    -> StopAction + the pawn's built-in walk / jog / run at that speed
//                                            (ACPP_AbbyCharacter::Speed; Jogging = 0.62 = pure jog)
//   * otherwise                           -> the card is greyed out on this pawn
// The playing action's card has the green outline. Everything re-checks when the controller possesses another pawn.
//
// Widget Blueprint: /Game/UserInterface/IHPC/Widgets/WBP_IHPC_ActionsPanel
//   Needs a Panel "CardList" (a ScrollBox / VerticalBox the cards are added to).
// -----------------------------------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "IHPC_UI/IHPCTypes.h"
#include "CPP_UW_IHPCActionsPanel.generated.h"

class APawn;
class UPanelWidget;
class UTexture2D;
class UAnimationTimelineComp;
class UCPP_UW_IHPCActionCard;

/** An action card was clicked. ActionName = the action now playing (None = back to the default animation). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FIHPCActionSelected, FName, PlayingAction, bool, bStartedAction);

UCLASS(Abstract)
class METAHUMANTEST_API UCPP_UW_IHPCActionsPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	UCPP_UW_IHPCActionsPanel(const FObjectInitializer& ObjectInitializer);

	/** The cards, top to bottom. Defaults = Figma's four + the project's Tennis Forehand / Soccer Warmup. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IHPC|Actions")
	TArray<FIHPCActionInfo> Actions;

	/** Widget Blueprint used for each card (WBP_IHPC_ActionCard). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IHPC|Setup")
	TSubclassOf<UCPP_UW_IHPCActionCard> CardClass;

	/** Picture for cards whose action has no Picture of its own (T_IHPC_ActionCard). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IHPC|Setup")
	TObjectPtr<UTexture2D> DefaultCardPicture;

	/** Gap between cards (Figma: 25 px, cards 172 px apart). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IHPC|Layout")
	float CardSpacing = 25.f;

	/** Fired after a card click changed what the avatar is doing. */
	UPROPERTY(BlueprintAssignable, Category = "IHPC|Events")
	FIHPCActionSelected OnActionSelected;

	/** Same as clicking the card whose ActionName matches (None = the Idle card). Returns true if an action clip
	 *  is now playing. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|Actions")
	bool SelectAction(FName ActionName);

	/** Same as clicking card number CardIndex. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|Actions")
	bool SelectCard(int32 CardIndex);

	/** Action clip playing on the possessed pawn, or None while it uses its default animation. */
	UFUNCTION(BlueprintPure, Category = "IHPC|Actions")
	FName GetPlayingAction() const;

	/** The action names the possessed pawn can do right now (the cards that are not greyed out), top to bottom.
	 *  The assistant sends these to the AI server so "make her jump" only picks something that exists. */
	UFUNCTION(BlueprintPure, Category = "IHPC|Actions")
	TArray<FName> GetAvailableActions() const;

	/** The action whose card is highlighted: the playing clip, or the Idle / walk / jog / run card that was picked
	 *  last while no clip plays. None if there are no cards. */
	UFUNCTION(BlueprintPure, Category = "IHPC|Actions")
	FName GetHighlightedAction() const;

	/** Re-reads the possessed pawn: which cards are available and which one is highlighted. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|Actions")
	void RefreshCards();

	/** Replace all cards. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|Actions")
	void SetActions(const TArray<FIHPCActionInfo>& NewActions);

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UPanelWidget> CardList;

private:
	UFUNCTION() void HandleCardClicked(UCPP_UW_IHPCActionCard* Card);
	UFUNCTION() void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	/** Deletes and re-creates the cards from Actions. */
	void RebuildCards();

	UAnimationTimelineComp* GetAvatarTimeline() const;

	/** Can the possessed pawn do card Index? */
	bool IsCardAvailable(int32 Index, const UAnimationTimelineComp* AvatarTimeline) const;

	/** Index of the Idle card (bIsDefault), or INDEX_NONE. */
	int32 GetDefaultCardIndex() const;

	/** Sets the pawn's walk / jog / run speed (remembers its own speed first so Idle can put it back). */
	void SetLocomotionSpeed(float Speed);
	void RestoreLocomotionSpeed();

	UPROPERTY()
	TArray<TObjectPtr<UCPP_UW_IHPCActionCard>> Cards;

	/** Highlighted card while no action clip plays (the Idle card, or a card that set the locomotion speed). */
	int32 LocomotionCardIndex = INDEX_NONE;

	/** The pawn whose speed a card changed, and the speed it had before. */
	TWeakObjectPtr<APawn> SpeedChangedPawn;
	float OriginalPawnSpeed = 0.f;
};

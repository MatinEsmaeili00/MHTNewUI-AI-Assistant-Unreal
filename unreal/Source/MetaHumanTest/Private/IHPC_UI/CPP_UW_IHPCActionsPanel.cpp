// CPP_UW_IHPCActionsPanel.cpp - the Action side panel (cards -> UAnimationTimelineComp actions). See header.

#include "IHPC_UI/CPP_UW_IHPCActionsPanel.h"

#include "IHPC_UI/CPP_UW_IHPCActionCard.h"
#include "IHPC_UI/IHPCUILibrary.h"

#include "AnimationTimelineComp.h"
#include "../../CPP_AbbyCharacter.h"     // module root (not on the include path); pawn locomotion Speed
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

#define LOCTEXT_NAMESPACE "IHPCActionsPanel"

UCPP_UW_IHPCActionsPanel::UCPP_UW_IHPCActionsPanel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Figma frame 19 ("Idle", "Running", "Jugging", "Jumping") + the actions the project already had clips for.
	// Action names = keys of each pawn's AnimationTimelineComp > Action Clips.
	Actions = {
		FIHPCActionInfo(LOCTEXT("Idle", "Idle"), TEXT("Idle"), /*bIsDefault*/ true),
		FIHPCActionInfo(LOCTEXT("Running", "Running"), TEXT("Running"), false, /*LocomotionSpeed*/ 1.f),
		FIHPCActionInfo(LOCTEXT("Jogging", "Jogging"), TEXT("Jogging"), false, /*LocomotionSpeed*/ 0.62f),
		FIHPCActionInfo(LOCTEXT("Jumping", "Jumping"), TEXT("Jumping")),
		FIHPCActionInfo(LOCTEXT("TennisForehand", "Tennis Forehand"), TEXT("TennisForehand")),
		FIHPCActionInfo(LOCTEXT("SoccerWarmup", "Soccer Warmup"), TEXT("SoccerWarmup")),
	};
}

void UCPP_UW_IHPCActionsPanel::NativePreConstruct()
{
	Super::NativePreConstruct();
	RebuildCards();   // also in the designer, so the column can be previewed
}

void UCPP_UW_IHPCActionsPanel::NativeConstruct()
{
	Super::NativeConstruct();
	RebuildCards();
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &UCPP_UW_IHPCActionsPanel::HandlePossessedPawnChanged);
	}
	RefreshCards();
}

void UCPP_UW_IHPCActionsPanel::NativeDestruct()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->OnPossessedPawnChanged.RemoveDynamic(this, &UCPP_UW_IHPCActionsPanel::HandlePossessedPawnChanged);
	}
	Super::NativeDestruct();
}

void UCPP_UW_IHPCActionsPanel::SetActions(const TArray<FIHPCActionInfo>& NewActions)
{
	Actions = NewActions;
	LocomotionCardIndex = INDEX_NONE;
	RebuildCards();
	RefreshCards();
}

void UCPP_UW_IHPCActionsPanel::RebuildCards()
{
	if (!CardList || !CardClass)
	{
		return;
	}
	CardList->ClearChildren();
	Cards.Reset();

	for (int32 i = 0; i < Actions.Num(); ++i)
	{
		UCPP_UW_IHPCActionCard* Card = CreateWidget<UCPP_UW_IHPCActionCard>(this, CardClass);
		if (!Card)
		{
			continue;
		}
		FIHPCActionInfo Info = Actions[i];
		if (!Info.Picture)
		{
			Info.Picture = DefaultCardPicture;
		}
		Card->SetInfo(Info);
		Card->CardIndex = i;
		Card->OnCardClicked.AddUniqueDynamic(this, &UCPP_UW_IHPCActionsPanel::HandleCardClicked);

		UPanelSlot* CardSlot = CardList->AddChild(Card);
		const FMargin CardPadding(0.f, 0.f, 0.f, i < Actions.Num() - 1 ? CardSpacing : 0.f);
		if (UScrollBoxSlot* ScrollSlot = Cast<UScrollBoxSlot>(CardSlot))
		{
			ScrollSlot->SetPadding(CardPadding);
			ScrollSlot->SetHorizontalAlignment(HAlign_Left);
		}
		else if (UVerticalBoxSlot* BoxSlot = Cast<UVerticalBoxSlot>(CardSlot))
		{
			BoxSlot->SetPadding(CardPadding);
			BoxSlot->SetHorizontalAlignment(HAlign_Left);
		}
		Cards.Add(Card);
	}
}

UAnimationTimelineComp* UCPP_UW_IHPCActionsPanel::GetAvatarTimeline() const
{
	return UIHPCUILibrary::FindAvatarTimeline(this);
}

FName UCPP_UW_IHPCActionsPanel::GetPlayingAction() const
{
	const UAnimationTimelineComp* AvatarTimeline = GetAvatarTimeline();
	return AvatarTimeline ? AvatarTimeline->GetCurrentAction() : NAME_None;
}

TArray<FName> UCPP_UW_IHPCActionsPanel::GetAvailableActions() const
{
	TArray<FName> Names;
	const UAnimationTimelineComp* AvatarTimeline = GetAvatarTimeline();
	for (int32 Index = 0; Index < Actions.Num(); ++Index)
	{
		if (IsCardAvailable(Index, AvatarTimeline))
		{
			Names.Add(Actions[Index].ActionName);
		}
	}
	return Names;
}

FName UCPP_UW_IHPCActionsPanel::GetHighlightedAction() const
{
	const FName Playing = GetPlayingAction();
	if (!Playing.IsNone())
	{
		return Playing;
	}
	const int32 Index = Actions.IsValidIndex(LocomotionCardIndex) ? LocomotionCardIndex : GetDefaultCardIndex();
	return Actions.IsValidIndex(Index) ? Actions[Index].ActionName : NAME_None;
}

int32 UCPP_UW_IHPCActionsPanel::GetDefaultCardIndex() const
{
	return Actions.IndexOfByPredicate([](const FIHPCActionInfo& Info) { return Info.bIsDefault; });
}

bool UCPP_UW_IHPCActionsPanel::IsCardAvailable(int32 Index, const UAnimationTimelineComp* AvatarTimeline) const
{
	if (!Actions.IsValidIndex(Index) || !AvatarTimeline)
	{
		return false;
	}
	const FIHPCActionInfo& Info = Actions[Index];
	if (Info.bIsDefault || AvatarTimeline->HasAction(Info.ActionName))
	{
		return true;
	}
	return Info.LocomotionSpeed >= 0.f && Cast<ACPP_AbbyCharacter>(AvatarTimeline->GetOwner()) != nullptr;
}

bool UCPP_UW_IHPCActionsPanel::SelectAction(FName ActionName)
{
	int32 Index = INDEX_NONE;
	if (!ActionName.IsNone())
	{
		Index = Actions.IndexOfByPredicate([ActionName](const FIHPCActionInfo& Info) { return Info.ActionName == ActionName; });
	}
	if (Index == INDEX_NONE)
	{
		Index = GetDefaultCardIndex();
	}
	return SelectCard(Index);
}

bool UCPP_UW_IHPCActionsPanel::SelectCard(int32 CardIndex)
{
	UAnimationTimelineComp* AvatarTimeline = GetAvatarTimeline();
	if (!AvatarTimeline)
	{
		UE_LOG(LogTemp, Warning, TEXT("[IHPC] Actions: the possessed pawn has no AnimationTimelineComp, nothing can play."));
		return false;
	}

	bool bStarted = false;
	const int32 DefaultIndex = GetDefaultCardIndex();

	if (!Actions.IsValidIndex(CardIndex))
	{
		// No such card (or no Idle card): just go back to the default animation.
		AvatarTimeline->StopAction();
		RestoreLocomotionSpeed();
		LocomotionCardIndex = DefaultIndex;
	}
	else
	{
		const FIHPCActionInfo& Info = Actions[CardIndex];
		const bool bHasClip = AvatarTimeline->HasAction(Info.ActionName);

		if (bHasClip && AvatarTimeline->GetCurrentAction() == Info.ActionName && !Info.bIsDefault)
		{
			// Second click on the playing card: back to Idle.
			AvatarTimeline->StopAction();
			RestoreLocomotionSpeed();
			LocomotionCardIndex = DefaultIndex;
		}
		else if (bHasClip)
		{
			bStarted = AvatarTimeline->PlayAction(Info.ActionName);
			if (!bStarted)
			{
				UE_LOG(LogTemp, Warning, TEXT("[IHPC] Actions: '%s' could not play on %s (clip does not fit the mesh?)."),
					*Info.ActionName.ToString(), *GetNameSafe(AvatarTimeline->GetOwner()));
			}
		}
		else if (Info.bIsDefault)
		{
			// Idle without an Idle clip: the pawn's own animation at its own speed.
			AvatarTimeline->StopAction();
			RestoreLocomotionSpeed();
			LocomotionCardIndex = CardIndex;
		}
		else if (Info.LocomotionSpeed >= 0.f && Cast<ACPP_AbbyCharacter>(AvatarTimeline->GetOwner()))
		{
			// No clip: use the pawn's built-in walk / jog / run.
			AvatarTimeline->StopAction();
			SetLocomotionSpeed(Info.LocomotionSpeed);
			LocomotionCardIndex = CardIndex;
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[IHPC] Actions: %s has no clip for '%s'."), *GetNameSafe(AvatarTimeline->GetOwner()), *Info.ActionName.ToString());
		}
	}

	RefreshCards();
	OnActionSelected.Broadcast(AvatarTimeline->GetCurrentAction(), bStarted);
	return bStarted;
}

void UCPP_UW_IHPCActionsPanel::SetLocomotionSpeed(float Speed)
{
	APawn* Pawn = GetOwningPlayerPawn();
	ACPP_AbbyCharacter* Abby = Cast<ACPP_AbbyCharacter>(Pawn);
	if (!Abby)
	{
		return;
	}
	if (SpeedChangedPawn.Get() != Pawn)
	{
		SpeedChangedPawn = Pawn;
		OriginalPawnSpeed = Abby->Speed;   // what the speed slider / the pawn had before a card changed it
	}
	Abby->Speed = FMath::Clamp(Speed, 0.f, 1.f);
}

void UCPP_UW_IHPCActionsPanel::RestoreLocomotionSpeed()
{
	if (ACPP_AbbyCharacter* Abby = Cast<ACPP_AbbyCharacter>(SpeedChangedPawn.Get()))
	{
		Abby->Speed = OriginalPawnSpeed;
	}
	SpeedChangedPawn.Reset();
}

void UCPP_UW_IHPCActionsPanel::RefreshCards()
{
	const UAnimationTimelineComp* AvatarTimeline = GetAvatarTimeline();
	const FName Playing = AvatarTimeline ? AvatarTimeline->GetCurrentAction() : NAME_None;

	// While no clip plays, the Idle card (or the walk / jog / run card that was picked) is the highlighted one.
	if (!Actions.IsValidIndex(LocomotionCardIndex))
	{
		LocomotionCardIndex = GetDefaultCardIndex();
	}

	for (int32 i = 0; i < Cards.Num(); ++i)
	{
		UCPP_UW_IHPCActionCard* Card = Cards[i];
		if (!Card || !Actions.IsValidIndex(i))
		{
			continue;
		}
		Card->SetAvailable(IsCardAvailable(i, AvatarTimeline));
		const bool bHighlight = Playing.IsNone() ? (i == LocomotionCardIndex) : (Actions[i].ActionName == Playing);
		Card->SetPlaying(AvatarTimeline && bHighlight);
	}
}

void UCPP_UW_IHPCActionsPanel::HandleCardClicked(UCPP_UW_IHPCActionCard* Card)
{
	if (Card)
	{
		SelectCard(Card->CardIndex);
	}
}

void UCPP_UW_IHPCActionsPanel::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	// A different character: its speed is its own again, and the highlight follows what it is doing.
	RestoreLocomotionSpeed();
	LocomotionCardIndex = GetDefaultCardIndex();
	RefreshCards();
}

#undef LOCTEXT_NAMESPACE

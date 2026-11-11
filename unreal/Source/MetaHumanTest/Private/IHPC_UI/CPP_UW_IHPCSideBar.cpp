// CPP_UW_IHPCSideBar.cpp - the side bar (BOT, layer buttons, tabs, chevron). See header.

#include "IHPC_UI/CPP_UW_IHPCSideBar.h"

#include "IHPC_UI/CPP_UW_IHPCChatPanel.h"
#include "IHPC_UI/CPP_UW_IHPCLayerSwitcher.h"
#include "IHPC_UI/CPP_UW_IHPCSideTab.h"
#include "IHPC_UI/IHPCUILibrary.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Styling/SlateBrush.h"
#include "TimerManager.h"

// The animated BOT logo (Plugins/HoloAvatar): the UMG widget and the simulation behind it.
#include "HoloAvatarImage.h"
#include "HoloAvatarSimulation.h"

#define LOCTEXT_NAMESPACE "IHPCSideBar"

UCPP_UW_IHPCSideBar::UCPP_UW_IHPCSideBar(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The animated BOT logo is a round hologram that merges with whatever is behind it: the plugin's see-through
	// panel, made a circle (Edge Roundness 1), with an edge that dissolves over a wide band instead of ending in a
	// rim. Colours, head motion and blinks keep the plugin's defaults. The numbers were picked by eye in
	// Play-In-Editor over the level's bright sky.
	FHoloAvatarLook& Closed = BotAvatarSettings.Look;
	Closed.EdgeRoundness = 1.f;        // a circle
	Closed.EdgeSoftness = 0.42f;       // the outer 42 % of the radius fades out
	Closed.BackgroundOpacity = 0.55f;  // the scene shows through the panel
	Closed.ScanlineStrength = 0.f;     // scanlines shimmer when the whole logo is about 100 px
	Closed.Flicker = 0.15f;

	// Chat open: the same hologram turned up - softer edge, stronger glow, bright dust drifting over the face.
	BotChatOpenLook = Closed;
	BotChatOpenLook.EdgeSoftness = 0.55f;
	BotChatOpenLook.BackgroundOpacity = 0.6f;
	BotChatOpenLook.GlowStrength = 1.6f;
	BotChatOpenLook.ParticleBrightness = 2.6f;

	// Held for push-to-talk: the same hologram in red (only the colours and the glow are used from this look).
	BotVoiceLook = BotChatOpenLook;
	BotVoiceLook.BackgroundColor = FLinearColor(0.050f, 0.004f, 0.004f);
	BotVoiceLook.FillColor = FLinearColor(0.32f, 0.030f, 0.025f);
	BotVoiceLook.OutlineColor = FLinearColor(1.45f, 0.24f, 0.16f);
	BotVoiceLook.GlowColor = FLinearColor(0.95f, 0.07f, 0.04f);
	BotVoiceLook.ParticleColor = FLinearColor(1.0f, 0.22f, 0.12f);
	BotVoiceLook.GridColor = FLinearColor(0.17f, 0.02f, 0.02f);
	BotVoiceLook.GlowStrength = 2.1f;
	BotVoiceLook.ParticleBrightness = 3.f;

	// 120 motes all the time; only their brightness changes with the chat (changing the count restarts the dust).
	BotAvatarSettings.Performance.ParticleCountOverride = 120;
	// 240 px = exactly 4 pixels per cell of the 60 x 60 hologram grid. A size that is not a whole number of pixels
	// per cell (what "match the widget" gives for a logo this small) shows a faint lattice across the picture.
	BotAvatarSettings.Performance.ResolutionOverride = 240;
}

// The look between two looks: every number and colour is interpolated.
static FHoloAvatarLook LerpBotLook(const FHoloAvatarLook& A, const FHoloAvatarLook& B, float T)
{
	FHoloAvatarLook Out = A;
	Out.BackgroundColor = FMath::Lerp(A.BackgroundColor, B.BackgroundColor, T);
	Out.BackgroundOpacity = FMath::Lerp(A.BackgroundOpacity, B.BackgroundOpacity, T);
	Out.EdgeSoftness = FMath::Lerp(A.EdgeSoftness, B.EdgeSoftness, T);
	Out.EdgeRoundness = FMath::Lerp(A.EdgeRoundness, B.EdgeRoundness, T);
	Out.FillColor = FMath::Lerp(A.FillColor, B.FillColor, T);
	Out.OutlineColor = FMath::Lerp(A.OutlineColor, B.OutlineColor, T);
	Out.GlowColor = FMath::Lerp(A.GlowColor, B.GlowColor, T);
	Out.ParticleColor = FMath::Lerp(A.ParticleColor, B.ParticleColor, T);
	Out.GridColor = FMath::Lerp(A.GridColor, B.GridColor, T);
	Out.GlowStrength = FMath::Lerp(A.GlowStrength, B.GlowStrength, T);
	Out.GridIntensity = FMath::Lerp(A.GridIntensity, B.GridIntensity, T);
	Out.ParticleBrightness = FMath::Lerp(A.ParticleBrightness, B.ParticleBrightness, T);
	Out.CellGap = FMath::Lerp(A.CellGap, B.CellGap, T);
	Out.EmissiveOpacity = FMath::Lerp(A.EmissiveOpacity, B.EmissiveOpacity, T);
	Out.ScanlineStrength = FMath::Lerp(A.ScanlineStrength, B.ScanlineStrength, T);
	Out.Flicker = FMath::Lerp(A.Flicker, B.Flicker, T);
	return Out;
}

TArray<UCPP_UW_IHPCSideTab*> UCPP_UW_IHPCSideBar::GetTabs() const
{
	return { Tab_Actions, Tab_PoseEstimation, Tab_Graphs, Tab_Timeline, Tab_SavedVideos, Tab_Settings };
}

UCPP_UW_IHPCSideTab* UCPP_UW_IHPCSideBar::GetTab(EIHPCSideTab Tab) const
{
	switch (Tab)
	{
	case EIHPCSideTab::Actions:        return Tab_Actions;
	case EIHPCSideTab::PoseEstimation: return Tab_PoseEstimation;
	case EIHPCSideTab::Graphs:         return Tab_Graphs;
	case EIHPCSideTab::Timeline:       return Tab_Timeline;
	case EIHPCSideTab::SavedVideos:    return Tab_SavedVideos;
	case EIHPCSideTab::Settings:       return Tab_Settings;
	default:                           return nullptr;
	}
}

void UCPP_UW_IHPCSideBar::NativePreConstruct()
{
	Super::NativePreConstruct();

	// Each tab knows which tab it is, so the designer cannot mix them up.
	const TPair<UCPP_UW_IHPCSideTab*, EIHPCSideTab> Ids[] = {
		{ Tab_Actions, EIHPCSideTab::Actions }, { Tab_PoseEstimation, EIHPCSideTab::PoseEstimation },
		{ Tab_Graphs, EIHPCSideTab::Graphs }, { Tab_Timeline, EIHPCSideTab::Timeline },
		{ Tab_SavedVideos, EIHPCSideTab::SavedVideos }, { Tab_Settings, EIHPCSideTab::Settings } };
	for (const auto& Id : Ids)
	{
		if (Id.Key)
		{
			Id.Key->TabId = Id.Value;
		}
	}

	ApplyStyles();
	bExpanded = bStartExpanded;
	RefreshExpanded();
	bChatOpen = bStartChatOpen;
	if (ChatPanel)
	{
		ChatPanel->SetVisibility(bChatOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UCPP_UW_IHPCSideBar::NativeConstruct()
{
	Super::NativeConstruct();

	// Empty parts of the side bar's canvas must not swallow clicks meant for the 3D view.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	for (UCPP_UW_IHPCSideTab* Tab : GetTabs())
	{
		if (Tab)
		{
			Tab->OnTabClicked.AddUniqueDynamic(this, &UCPP_UW_IHPCSideBar::HandleTabClicked);
		}
	}
	if (ExpandButton)
	{
		ExpandButton->OnClicked.AddUniqueDynamic(this, &UCPP_UW_IHPCSideBar::HandleExpandClicked);
	}
	if (BotButton)
	{
		BotButton->OnClicked.AddUniqueDynamic(this, &UCPP_UW_IHPCSideBar::HandleBotClicked);
		BotButton->OnHovered.AddUniqueDynamic(this, &UCPP_UW_IHPCSideBar::HandleBotHovered);
		BotButton->OnPressed.AddUniqueDynamic(this, &UCPP_UW_IHPCSideBar::HandleBotPressed);
		BotButton->OnReleased.AddUniqueDynamic(this, &UCPP_UW_IHPCSideBar::HandleBotReleased);
		BotButton->SetToolTipText(LOCTEXT("BotTooltip", "Assistant"));
	}
	if (ChatPanel)
	{
		// The animated logo follows the conversation: thinks after a message is sent, talks during the answer.
		ChatPanel->OnMessageSent.AddUniqueDynamic(this, &UCPP_UW_IHPCSideBar::HandleChatMessageSent);
		ChatPanel->OnBotMessage.AddUniqueDynamic(this, &UCPP_UW_IHPCSideBar::HandleChatBotMessage);
	}

	CreateBotAvatar();   // before ApplyStyles: with the animated logo in place the button draws no picture
	ApplyStyles();
	SetExpanded(bStartExpanded);
	SetChatOpen(bStartChatOpen);
	SetSelectedTab(SelectedTab);

	// Start in the right look straight away (no fade when the game starts with the chat already open).
	BotLookBlend = bChatOpen ? 1.f : 0.f;
	ApplyBotLook();
}

void UCPP_UW_IHPCSideBar::NativeDestruct()
{
	EndBotHold();   // never leave the microphone recording when the HUD goes away
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BotHoldTimer);
	}
	Super::NativeDestruct();
}

void UCPP_UW_IHPCSideBar::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateBotLook(InDeltaTime);

	// The pointer slid off the logo while holding it: the button reports no release in that case until the mouse
	// button comes up somewhere else, and not at all on some touch screens. Stop when the press is really gone.
	if (bBotHolding && BotButton && !BotButton->IsPressed())
	{
		EndBotHold();
	}
}

void UCPP_UW_IHPCSideBar::ApplyStyles()
{
	if (BotAvatar && BotButton)
	{
		// The animated logo is the button's content, so the button itself draws nothing in any state (the still
		// picture would otherwise show around the hologram's soft rim). It is still the thing that is clicked.
		const FSlateNoResource Nothing;
		FButtonStyle Style = BotButton->GetStyle();
		Style.SetNormal(Nothing);
		Style.SetHovered(Nothing);
		Style.SetPressed(Nothing);
		Style.SetDisabled(Nothing);
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		BotButton->SetStyle(Style);
		BotButton->SetBackgroundColor(FLinearColor::White);
	}
	else
	{
		UIHPCUILibrary::ApplyPictureButtonStyle(BotButton, BotTexture, BotButtonSize, 1.1f, 0.85f);
	}
	UIHPCUILibrary::ApplyPictureButtonStyle(ExpandButton, ExpandButtonTexture, ExpandButtonSize);
}

// ---------------------------------------------------------------------------
// The animated BOT logo (Plugins/HoloAvatar)
// ---------------------------------------------------------------------------

void UCPP_UW_IHPCSideBar::CreateBotAvatar()
{
	// Made once, at Play only: the designer keeps showing the still picture (BotTexture).
	if (BotAvatar || !bAnimatedBot || !BotButton || !WidgetTree || IsDesignTime())
	{
		return;
	}

	// BotButton already holds something from the Widget Blueprint: a Holo Avatar put there by hand is used as it
	// is (its own look and size are left alone); anything else is left alone and the logo stays a picture.
	if (UWidget* Existing = BotButton->GetContent())
	{
		BotAvatar = Cast<UHoloAvatarImage>(Existing);
		bBotAvatarIsOurs = false;
		return;
	}

	UHoloAvatarImage* Avatar = WidgetTree->ConstructWidget<UHoloAvatarImage>(UHoloAvatarImage::StaticClass(), TEXT("BotAvatar"));
	Avatar->Settings = BotAvatarSettings;
	Avatar->DesiredSize = BotButtonSize;
	// Clicks go to the button underneath, not to the picture of the hologram.
	Avatar->SetVisibility(ESlateVisibility::HitTestInvisible);

	// A Button pads and centres its content by default; the logo must fill the whole 69 x 69 button.
	if (UButtonSlot* AvatarSlot = Cast<UButtonSlot>(BotButton->SetContent(Avatar)))
	{
		AvatarSlot->SetPadding(FMargin(0.f));
		AvatarSlot->SetHorizontalAlignment(HAlign_Fill);
		AvatarSlot->SetVerticalAlignment(VAlign_Fill);
	}

	// The hologram is drawn larger than the button (BotHologramScale), so its faded edge reaches over the chat
	// window's corner and the top of the pill. Draw it in front of them: behind, the chat window would cut it off
	// in a straight line. It does not take their clicks - only the 69 px button underneath is clickable.
	if (UCanvasPanelSlot* ButtonSlot = Cast<UCanvasPanelSlot>(BotButton->Slot))
	{
		ButtonSlot->SetZOrder(1);
	}

	BotAvatar = Avatar;
	bBotAvatarIsOurs = true;
}

void UCPP_UW_IHPCSideBar::UpdateBotLook(float DeltaTime)
{
	if (!BotAvatar || !bBotAvatarIsOurs)
	{
		return;
	}
	const float Target = bChatOpen ? 1.f : 0.f;
	const float VoiceTarget = bBotVoiceActive ? 1.f : 0.f;
	if (BotLookBlend == Target && BotVoiceBlend == VoiceTarget)
	{
		return;   // nothing to do most frames
	}
	BotLookBlend = BotLookBlendTime > 0.f
		? FMath::FInterpConstantTo(BotLookBlend, Target, DeltaTime, 1.f / BotLookBlendTime)
		: Target;
	BotVoiceBlend = BotVoiceBlendTime > 0.f
		? FMath::FInterpConstantTo(BotVoiceBlend, VoiceTarget, DeltaTime, 1.f / BotVoiceBlendTime)
		: VoiceTarget;
	ApplyBotLook();
}

void UCPP_UW_IHPCSideBar::ApplyBotLook()
{
	if (!BotAvatar || !bBotAvatarIsOurs)
	{
		return;
	}
	const float T = FMath::SmoothStep(0.f, 1.f, BotLookBlend);   // ease in and out

	FHoloAvatarSettings Settings = BotAvatarSettings;
	Settings.Look = LerpBotLook(BotAvatarSettings.Look, BotChatOpenLook, T);
	float Scale = FMath::Lerp(BotHologramScale, BotChatOpenHologramScale, T);

	// Push-to-talk: the colours and the glow go to red on top of whichever look the chat gives; the shape stays.
	if (BotVoiceBlend > 0.f)
	{
		const float V = FMath::SmoothStep(0.f, 1.f, BotVoiceBlend);
		FHoloAvatarLook Red = Settings.Look;
		Red.BackgroundColor = BotVoiceLook.BackgroundColor;
		Red.FillColor = BotVoiceLook.FillColor;
		Red.OutlineColor = BotVoiceLook.OutlineColor;
		Red.GlowColor = BotVoiceLook.GlowColor;
		Red.ParticleColor = BotVoiceLook.ParticleColor;
		Red.GridColor = BotVoiceLook.GridColor;
		Red.GlowStrength = BotVoiceLook.GlowStrength;
		Red.ParticleBrightness = BotVoiceLook.ParticleBrightness;
		Settings.Look = LerpBotLook(Settings.Look, Red, V);
		Scale = FMath::Lerp(Scale, BotVoiceHologramScale, V);
	}
	BotAvatar->ApplySettings(Settings);

	// Only the picture is scaled (a render transform): layout and the clickable area stay the 69 px button.
	BotAvatar->SetRenderScale(FVector2D(Scale));
}

// ---------------------------------------------------------------------------
// Push-to-talk: hold the BOT logo
// ---------------------------------------------------------------------------

void UCPP_UW_IHPCSideBar::SetBotVoiceActive(bool bActive)
{
	if (bBotVoiceActive == bActive)
	{
		return;
	}
	bBotVoiceActive = bActive;

	if (BotButton && (!BotAvatar || !bBotAvatarIsOurs))
	{
		// The still picture, or a Holo Avatar placed by hand in the Widget Blueprint (its own look is left alone):
		// there is no look to recolour, so the button's content is tinted instead.
		BotButton->SetColorAndOpacity(bActive ? BotVoiceTint : FLinearColor::White);
	}
	if (bActive)
	{
		SetBotState(EHoloAvatarState::Listening);   // head still, ripples answer the microphone level
	}
	else
	{
		SetBotInputLevel(0.f);
	}
}

void UCPP_UW_IHPCSideBar::SetBotInputLevel(float Level)
{
	if (BotAvatar)
	{
		if (UHoloAvatarSimulation* Simulation = BotAvatar->GetSimulation())
		{
			Simulation->SetInputLevel(FMath::Clamp(Level, 0.f, 1.f));
		}
	}
}

void UCPP_UW_IHPCSideBar::SimulateBotHold(float Seconds)
{
	UWorld* World = GetWorld();
	if (!BotButton || !World || !FSlateApplication::IsInitialized())
	{
		return;
	}
	// The middle of the button on the screen. The events go through Slate's normal routing, so the button gets
	// OnPressed / OnReleased (/ OnClicked) exactly as from a real pointer.
	const FGeometry& Geometry = BotButton->GetCachedGeometry();
	const FVector2D Where = Geometry.GetAbsolutePosition() + Geometry.GetAbsoluteSize() * 0.5;
	FSlateApplication& Slate = FSlateApplication::Get();

	const FPointerEvent Move(0, Where, Where, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState());
	Slate.ProcessMouseMoveEvent(Move);
	const FPointerEvent Down(0, Where, Where, TSet<FKey>({ EKeys::LeftMouseButton }), EKeys::LeftMouseButton, 0.f, FModifierKeysState());
	Slate.ProcessMouseButtonDownEvent(nullptr, Down);

	World->GetTimerManager().SetTimer(SimulatedReleaseTimer, FTimerDelegate::CreateWeakLambda(this, [Where]()
	{
		if (FSlateApplication::IsInitialized())
		{
			const FPointerEvent Up(0, Where, Where, TSet<FKey>(), EKeys::LeftMouseButton, 0.f, FModifierKeysState());
			FSlateApplication::Get().ProcessMouseButtonUpEvent(Up);
		}
	}), FMath::Max(Seconds, 0.01f), /*bLoop*/ false);
}

void UCPP_UW_IHPCSideBar::HandleBotHoldTimer()
{
	if (!bBotPressed || bBotHolding)
	{
		return;
	}
	bBotHolding = true;
	bBotSwallowClick = true;   // letting go now is the end of the recording, not a click
	SetBotVoiceActive(true);
	OnBotVoiceHoldStarted.Broadcast();
}

void UCPP_UW_IHPCSideBar::EndBotHold()
{
	bBotPressed = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BotHoldTimer);
	}
	if (!bBotHolding)
	{
		return;
	}
	bBotHolding = false;
	SetBotVoiceActive(false);
	OnBotVoiceHoldEnded.Broadcast();
}

void UCPP_UW_IHPCSideBar::SetBotState(EHoloAvatarState NewState, float Duration)
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(BotStateTimer);
	}
	if (!BotAvatar)
	{
		return;
	}
	BotAvatar->SetState(NewState);
	if (Duration > 0.f && World)
	{
		World->GetTimerManager().SetTimer(BotStateTimer, FTimerDelegate::CreateUObject(this, &UCPP_UW_IHPCSideBar::RestBotState), Duration, /*bLoop*/ false);
	}
}

void UCPP_UW_IHPCSideBar::RestBotState()
{
	SetBotState(bChatOpen ? EHoloAvatarState::Listening : EHoloAvatarState::Idle);
}

void UCPP_UW_IHPCSideBar::SetBotSpeechLevel(float Level)
{
	if (BotAvatar)
	{
		BotAvatar->SetSpeechLevel(Level);
	}
}

void UCPP_UW_IHPCSideBar::HandleChatMessageSent(const FText& Text)
{
	if (bBotFollowsChat)
	{
		// Nothing answers yet (no assistant is connected), so Thinking ends by itself after BotThinkingTimeout.
		SetBotState(EHoloAvatarState::Thinking, BotThinkingTimeout);
	}
}

void UCPP_UW_IHPCSideBar::HandleChatBotMessage(const FText& Text)
{
	if (bBotFollowsChat)
	{
		// Talk for about as long as the answer takes to read. A real voice can drive the mouth with
		// SetBotSpeechLevel; without it the plugin makes up syllables.
		const float Seconds = FMath::Clamp(Text.ToString().Len() * BotSpeakingSecondsPerCharacter, 1.5f, 8.f);
		SetBotState(EHoloAvatarState::Speaking, Seconds);
	}
}

void UCPP_UW_IHPCSideBar::HandleBotHovered()
{
	if (bBotReactsToPointer && BotAvatar)
	{
		if (UHoloAvatarSimulation* Simulation = BotAvatar->GetSimulation())
		{
			Simulation->AddRipple(FVector2D(0.5, 0.5), 1.f, 6.f);   // a ring of light from the middle of the logo
		}
	}
}

void UCPP_UW_IHPCSideBar::HandleBotPressed()
{
	if (bBotReactsToPointer && BotAvatar && BotButton)
	{
		BotButton->SetColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f, 1.f));   // same dimming as the picture had
	}

	// A new press: it is a click until it has lasted BotHoldSeconds, then it is push-to-talk.
	bBotPressed = true;
	bBotHolding = false;
	bBotSwallowClick = false;
	if (BotHoldSeconds > 0.f)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(BotHoldTimer, FTimerDelegate::CreateUObject(this, &UCPP_UW_IHPCSideBar::HandleBotHoldTimer), BotHoldSeconds, /*bLoop*/ false);
		}
	}
}

void UCPP_UW_IHPCSideBar::HandleBotReleased()
{
	if (BotAvatar && BotButton)
	{
		BotButton->SetColorAndOpacity(FLinearColor::White);
	}
	EndBotHold();   // the button fires OnReleased before OnClicked, so bBotSwallowClick is still set for the click
}

void UCPP_UW_IHPCSideBar::SetExpanded(bool bInExpanded)
{
	const bool bChanged = bExpanded != bInExpanded;
	bExpanded = bInExpanded;
	RefreshExpanded();
	if (bChanged)
	{
		OnExpandedChanged.Broadcast();
	}
}

void UCPP_UW_IHPCSideBar::RefreshExpanded()
{
	if (ExtraTabs)
	{
		ExtraTabs->SetVisibility(bExpanded ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (ExpandIcon)
	{
		if (UTexture2D* Texture = bExpanded ? CollapseTexture.Get() : ExpandTexture.Get())
		{
			ExpandIcon->SetBrushFromTexture(Texture, /*bMatchSize*/ false);
		}
	}
	if (ExpandButton)
	{
		ExpandButton->SetToolTipText(bExpanded ? LOCTEXT("Collapse", "Hide tabs") : LOCTEXT("Expand", "Show all tabs"));
		if (UVerticalBoxSlot* ChevronSlot = Cast<UVerticalBoxSlot>(ExpandButton->Slot))
		{
			FMargin ChevronPadding = ChevronSlot->GetPadding();
			ChevronPadding.Top = bExpanded ? ExpandedChevronGap : CollapsedChevronGap;
			ChevronSlot->SetPadding(ChevronPadding);
		}
	}
}

void UCPP_UW_IHPCSideBar::SetSelectedTab(EIHPCSideTab Tab)
{
	SelectedTab = Tab;
	for (UCPP_UW_IHPCSideTab* SideTab : GetTabs())
	{
		if (SideTab)
		{
			SideTab->SetSelected(Tab != EIHPCSideTab::None && SideTab->TabId == Tab);
		}
	}
}

void UCPP_UW_IHPCSideBar::SetChatOpen(bool bOpen)
{
	bChatOpen = bOpen;
	if (ChatPanel)
	{
		// SelfHitTestInvisible: the window's empty margins pass clicks through, its buttons / text box still work.
		ChatPanel->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (bOpen)
		{
			ChatPanel->FocusInput();
		}
	}
	if (bBotFollowsChat)
	{
		RestBotState();   // the logo listens while the window is open and idles when it is closed
	}
}

void UCPP_UW_IHPCSideBar::HandleTabClicked(EIHPCSideTab Tab)
{
	OnTabClicked.Broadcast(Tab);
}

void UCPP_UW_IHPCSideBar::HandleExpandClicked()
{
	ToggleExpanded();
}

void UCPP_UW_IHPCSideBar::HandleBotClicked()
{
	if (bBotSwallowClick)
	{
		bBotSwallowClick = false;   // this "click" was the end of a push-to-talk hold
		return;
	}
	ToggleChat();
}

#undef LOCTEXT_NAMESPACE

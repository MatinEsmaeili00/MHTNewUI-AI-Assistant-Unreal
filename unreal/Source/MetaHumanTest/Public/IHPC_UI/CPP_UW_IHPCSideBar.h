// CPP_UW_IHPCSideBar.h
// -----------------------------------------------------------------------------
// The SIDE BAR on the left of the screen (Figma page "FINAL FESTIVAL"). It replaced the old drop-down nav bar:
// the nav bar's tabs are now extra round tabs in this bar.
//
//    (BOT)          <- assistant avatar: opens / closes the chat window (Figma component "BOT").
//                      It is ANIMATED: the Holo Avatar plugin (Plugins/HoloAvatar) draws it with compute shaders -
//                      a see-through hologram whose edge dissolves into whatever is behind it. It blinks, turns
//                      its head, listens while the chat is open, thinks after a message is sent and talks while
//                      the assistant answers. Opening the chat also turns it up: bigger, brighter, more dust.
//                      Untick "Animated Bot" to get the still picture back.
//                      A CLICK opens / closes the chat. HOLDING it (Bot Hold Seconds) is push-to-talk: the logo
//                      turns red, OnBotVoiceHoldStarted fires (the assistant starts recording the microphone),
//                      and letting go fires OnBotVoiceHoldEnded (the recording is sent). A hold is not a click.
//   +-----+         <- pill "Rectangle 24": rgba(12,39,61,0.59), 69 wide, fully rounded
//   | (=) |   Body      \
//   | (m) |   Muscles    > layer switcher (WBP_IHPC_LayerSwitcher, same C++ as before - drives the AnatomyView)
//   | (s) |   Skeleton  /
//   |  -  |  ---------------------------------------------------- ExtraTabs (hidden while collapsed)
//   | (a) |   Action
//   |  -  |
//   | (p) |   Pose Estimation
//   | (g) |   Graphs
//   |  -  |
//   | (t) |   Timeline
//   | (v) |   Saved Videos
//   | (o) |   Settings
//   |     |  ----------------------------------------------------
//   | (^) |   chevron: down = expand (frame 15), up = collapse (frame 16)
//   +-----+
//
// Collapsed (Figma frames 15 / 17): only the BOT, the three layer buttons and the down chevron (pill 272 tall).
// Expanded (frames 16 / 18 / 19-22): all tabs (pill 689 tall). The pill shrinks / grows by itself (auto size).
// Clicking a tab fires OnTabClicked; the HUD opens / closes that tab's panel and calls SetSelectedTab.
//
// Widget Blueprint: /Game/UserInterface/IHPC/Widgets/WBP_IHPC_SideBar (lives inside WBP_IHPC_HUD as "SideBar").
// -----------------------------------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/TimerHandle.h"
#include "HoloAvatarTypes.h"   // FHoloAvatarSettings, EHoloAvatarState (Plugins/HoloAvatar)
#include "IHPC_UI/IHPCTypes.h"
#include "CPP_UW_IHPCSideBar.generated.h"

class UButton;
class UImage;
class UTexture2D;
class UHoloAvatarImage;
class UCPP_UW_IHPCSideTab;
class UCPP_UW_IHPCLayerSwitcher;
class UCPP_UW_IHPCChatPanel;

UCLASS(Abstract)
class METAHUMANTEST_API UCPP_UW_IHPCSideBar : public UUserWidget
{
	GENERATED_BODY()

public:
	UCPP_UW_IHPCSideBar(const FObjectInitializer& ObjectInitializer);

	// ---------------------------------------------------------------- settings

	/** Start with every tab showing (Figma frame 16) instead of collapsed (frame 15). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IHPC|Behaviour")
	bool bStartExpanded = false;

	/** Open the chat window when the game starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IHPC|Behaviour")
	bool bStartChatOpen = false;

	/** Assistant avatar picture (Figma "ihpc ui fe-05 1", drawn as a 69 px circle). Shown in the designer, and at
	 *  Play when Animated Bot is off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	TObjectPtr<UTexture2D> BotTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	FVector2D BotButtonSize = FVector2D(69.f, 69.f);

	/** Empty frosted glass circle behind the chevron (Icons/Button.png). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	TObjectPtr<UTexture2D> ExpandButtonTexture;

	/** Figma "Group 30": 57 x 57. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	FVector2D ExpandButtonSize = FVector2D(57.f, 57.f);

	/** White chevron pointing down, shown while collapsed (click = expand). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	TObjectPtr<UTexture2D> ExpandTexture;

	/** White chevron pointing up, shown while expanded (click = collapse). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	TObjectPtr<UTexture2D> CollapseTexture;

	/** Space above the chevron (Figma: 32 px below Settings when expanded, 27 px below Skeleton when collapsed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Layout")
	float ExpandedChevronGap = 32.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Layout")
	float CollapsedChevronGap = 27.f;

	// ---------------------------------------------------------------- animated BOT logo (Plugins/HoloAvatar)

	/** Draw the BOT logo with the Holo Avatar compute shaders (it blinks, moves and talks) instead of the still
	 *  picture. The animated logo is made when the game starts; the designer keeps showing BotTexture. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IHPC|Bot")
	bool bAnimatedBot = true;

	/** Look, motion and cost of the animated logo while the chat is CLOSED. The defaults (set in the constructor)
	 *  make it a round hologram that merges with the background: Edge Roundness 1, a wide dissolving edge, a
	 *  see-through panel and quiet dust. Motion and Performance here are used all the time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IHPC|Bot", meta = (EditCondition = "bAnimatedBot"))
	FHoloAvatarSettings BotAvatarSettings;

	/** The logo's look while the chat is OPEN: the same hologram turned up - a softer edge, brighter glow and
	 *  bright dust drifting over it. The logo fades between the two looks over Bot Look Blend Time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IHPC|Bot", meta = (EditCondition = "bAnimatedBot"))
	FHoloAvatarLook BotChatOpenLook;

	/** The hologram is drawn this many times larger than the 69 px button, centred on it, so its dissolving edge
	 *  and its dust have room around the face. Only the picture grows: the clickable area stays the button. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Bot", meta = (ClampMin = "0.5", ClampMax = "4", EditCondition = "bAnimatedBot"))
	float BotHologramScale = 1.35f;

	/** The same while the chat is open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Bot", meta = (ClampMin = "0.5", ClampMax = "4", EditCondition = "bAnimatedBot"))
	float BotChatOpenHologramScale = 1.6f;

	/** Seconds the logo takes to change between its closed and open look. 0 = at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Bot", meta = (ClampMin = "0", EditCondition = "bAnimatedBot"))
	float BotLookBlendTime = 0.6f;

	/** A ring of light crosses the logo when the pointer moves onto it, and it dims while pressed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IHPC|Bot", meta = (EditCondition = "bAnimatedBot"))
	bool bBotReactsToPointer = true;

	/** The logo follows the chat by itself: Listening while the window is open, Thinking after the user sends a
	 *  message, Speaking while an answer is on screen, Idle when the window is closed. Turn off to drive it only
	 *  with SetBotState. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IHPC|Bot", meta = (EditCondition = "bAnimatedBot"))
	bool bBotFollowsChat = true;

	/** How long the logo keeps Thinking when nothing answers (no assistant is connected yet), seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Bot", meta = (ClampMin = "0", EditCondition = "bAnimatedBot"))
	float BotThinkingTimeout = 4.f;

	/** The logo talks this long per character of an answer (clamped to 1.5 - 8 seconds), seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Bot", meta = (ClampMin = "0", EditCondition = "bAnimatedBot"))
	float BotSpeakingSecondsPerCharacter = 0.06f;

	// ---------------------------------------------------------------- push-to-talk (hold the BOT logo)

	/** Holding the BOT logo this long (seconds) starts push-to-talk; a shorter press is a click (chat open / close).
	 *  0 = no push-to-talk. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Bot|Voice", meta = (ClampMin = "0"))
	float BotHoldSeconds = 0.3f;

	/** The logo's look while it is held and the microphone records: the same hologram in red. Colours and glow are
	 *  taken from here; the shape (edge, opacity) stays what the chat-open look has. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IHPC|Bot|Voice", meta = (EditCondition = "bAnimatedBot"))
	FHoloAvatarLook BotVoiceLook;

	/** Size of the hologram while recording, times the 69 px button. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Bot|Voice", meta = (ClampMin = "0.5", ClampMax = "4", EditCondition = "bAnimatedBot"))
	float BotVoiceHologramScale = 1.75f;

	/** Seconds the logo takes to turn red / back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Bot|Voice", meta = (ClampMin = "0"))
	float BotVoiceBlendTime = 0.15f;

	/** Tint of the still picture while recording (used when Animated Bot is off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Bot|Voice")
	FLinearColor BotVoiceTint = FLinearColor(1.f, 0.22f, 0.18f, 1.f);

	// ---------------------------------------------------------------- events

	/** The BOT logo has been held for Bot Hold Seconds: start recording the microphone. The logo is already red. */
	UPROPERTY(BlueprintAssignable, Category = "IHPC|Events")
	FIHPCSimpleEvent OnBotVoiceHoldStarted;

	/** The held BOT logo was let go (or the pointer left it): stop recording and send. */
	UPROPERTY(BlueprintAssignable, Category = "IHPC|Events")
	FIHPCSimpleEvent OnBotVoiceHoldEnded;

	/** A tab was clicked (the HUD opens / closes its panel). */
	UPROPERTY(BlueprintAssignable, Category = "IHPC|Events")
	FIHPCSideTabEvent OnTabClicked;

	/** The bar was expanded / collapsed. */
	UPROPERTY(BlueprintAssignable, Category = "IHPC|Events")
	FIHPCSimpleEvent OnExpandedChanged;

	// ---------------------------------------------------------------- functions

	UFUNCTION(BlueprintCallable, Category = "IHPC|SideBar")
	void SetExpanded(bool bInExpanded);

	UFUNCTION(BlueprintCallable, Category = "IHPC|SideBar")
	void ToggleExpanded() { SetExpanded(!bExpanded); }

	UFUNCTION(BlueprintPure, Category = "IHPC|SideBar")
	bool IsExpanded() const { return bExpanded; }

	/** Shows Tab in its selected (dark blue) look and every other tab normal. None = no tab selected. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|SideBar")
	void SetSelectedTab(EIHPCSideTab Tab);

	/** The round tab button for Tab (null for None). Its Label / NormalTexture are used for the panel title. */
	UFUNCTION(BlueprintPure, Category = "IHPC|SideBar")
	UCPP_UW_IHPCSideTab* GetTab(EIHPCSideTab Tab) const;

	/** The Body / Muscles / Skeleton buttons at the top of the bar. */
	UFUNCTION(BlueprintPure, Category = "IHPC|SideBar")
	UCPP_UW_IHPCLayerSwitcher* GetLayerSwitcher() const { return LayerSwitcher; }

	UFUNCTION(BlueprintPure, Category = "IHPC|SideBar")
	UCPP_UW_IHPCChatPanel* GetChatPanel() const { return ChatPanel; }

	UFUNCTION(BlueprintCallable, Category = "IHPC|Chat")
	void SetChatOpen(bool bOpen);

	UFUNCTION(BlueprintCallable, Category = "IHPC|Chat")
	void ToggleChat() { SetChatOpen(!IsChatOpen()); }

	UFUNCTION(BlueprintPure, Category = "IHPC|Chat")
	bool IsChatOpen() const { return bChatOpen; }

	/** The animated logo (a Holo Avatar widget inside BotButton). Null in the designer, when Animated Bot is off, or
	 *  before the game starts. Use its GetSimulation() for Blink / Nod / Smile / SetSpeechLevel / AddRipple. */
	UFUNCTION(BlueprintPure, Category = "IHPC|Bot")
	UHoloAvatarImage* GetBotAvatar() const { return BotAvatar; }

	/** Puts the animated logo in a state (Idle / Listening / Thinking / Speaking). With Bot Follows Chat on, the
	 *  chat changes it again at its next event. Duration = seconds until it goes back to its resting state
	 *  (Listening while the chat is open, Idle otherwise); 0 = stay. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|Bot")
	void SetBotState(EHoloAvatarState NewState, float Duration = 0.f);

	/** Feed the assistant's voice loudness (0..1) every frame while it speaks: the logo's mouth follows it. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|Bot")
	void SetBotSpeechLevel(float Level);

	/** Turns the logo red (true) or back to normal (false). Holding the logo does this by itself; the assistant
	 *  calls it with false when the microphone could not be opened, or with true to record without a hold. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|Bot|Voice")
	void SetBotVoiceActive(bool bActive);

	UFUNCTION(BlueprintPure, Category = "IHPC|Bot|Voice")
	bool IsBotVoiceActive() const { return bBotVoiceActive; }

	/** Feed the microphone loudness (0..1) while recording: ripples on the red logo answer it. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|Bot|Voice")
	void SetBotInputLevel(float Level);

	/** TEST: presses the BOT logo, holds it for Seconds and lets go, by sending pointer events through Slate - the
	 *  same path a finger or the mouse takes (console: "Assistant.HoldLogo 3"). Under Bot Hold Seconds = a click. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|Bot|Voice")
	void SimulateBotHold(float Seconds = 2.f);

	TArray<UCPP_UW_IHPCSideTab*> GetTabs() const;

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** The assistant avatar (opens the chat window). */
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> BotButton;

	/** Everything between the layer buttons and the chevron (separators + the six tabs). Hidden while collapsed. */
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidget> ExtraTabs;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ExpandButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> ExpandIcon;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCPP_UW_IHPCSideTab> Tab_Actions;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCPP_UW_IHPCSideTab> Tab_PoseEstimation;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCPP_UW_IHPCSideTab> Tab_Graphs;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCPP_UW_IHPCSideTab> Tab_Timeline;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCPP_UW_IHPCSideTab> Tab_SavedVideos;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCPP_UW_IHPCSideTab> Tab_Settings;

	/** WBP_IHPC_LayerSwitcher, stacked vertically (it used to be its own widget at the bottom right). */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UCPP_UW_IHPCLayerSwitcher> LayerSwitcher;

	/** WBP_IHPC_ChatPanel, to the right of the BOT avatar. Hidden until the avatar is clicked. */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UCPP_UW_IHPCChatPanel> ChatPanel;

private:
	UFUNCTION() void HandleTabClicked(EIHPCSideTab Tab);
	UFUNCTION() void HandleExpandClicked();
	UFUNCTION() void HandleBotClicked();
	UFUNCTION() void HandleBotHovered();
	UFUNCTION() void HandleBotPressed();
	UFUNCTION() void HandleBotReleased();
	UFUNCTION() void HandleChatMessageSent(const FText& Text);
	UFUNCTION() void HandleChatBotMessage(const FText& Text);

	/** Makes the Holo Avatar widget inside BotButton (once, at Play). Does nothing if Animated Bot is off or the
	 *  button already holds something else. */
	void CreateBotAvatar();

	/** Listening while the chat is open, Idle otherwise. */
	void RestBotState();

	/** The logo was held long enough: push-to-talk starts. */
	void HandleBotHoldTimer();

	/** Ends a hold (button released, pointer left the button while pressed, widget removed). */
	void EndBotHold();

	/** Moves BotLookBlend toward 0 (chat closed) or 1 (chat open) and, while it is moving, applies the look. */
	void UpdateBotLook(float DeltaTime);

	/** Gives the logo the look and size for the current BotLookBlend. */
	void ApplyBotLook();

	/** Shows / hides the extra tabs, swaps the chevron and its tooltip, sets the gap above it. */
	void RefreshExpanded();

	void ApplyStyles();

	/** The animated logo: BotButton's content. Made by CreateBotAvatar; kept alive by the widget tree too. */
	UPROPERTY(Transient)
	TObjectPtr<UHoloAvatarImage> BotAvatar;

	/** Brings the logo back to its resting state after Thinking / Speaking. */
	FTimerHandle BotStateTimer;

	/** 0 = the closed look (BotAvatarSettings.Look), 1 = the chat-open look (BotChatOpenLook). */
	float BotLookBlend = 0.f;

	/** 0 = normal colours, 1 = the red recording look (BotVoiceLook). */
	float BotVoiceBlend = 0.f;

	/** Starts push-to-talk when the logo has been held for BotHoldSeconds. */
	FTimerHandle BotHoldTimer;

	/** Lets go of a SimulateBotHold press. */
	FTimerHandle SimulatedReleaseTimer;

	bool bBotPressed = false;       // the logo is being pressed right now
	bool bBotHolding = false;       // ... and has been for BotHoldSeconds: push-to-talk is on
	bool bBotSwallowClick = false;  // the click that ends a hold must not open / close the chat
	bool bBotVoiceActive = false;   // the logo is (turning) red

	/** False when the Holo Avatar was put in the Widget Blueprint by hand: its own look is then left alone. */
	bool bBotAvatarIsOurs = false;

	bool bExpanded = false;
	bool bChatOpen = false;
	EIHPCSideTab SelectedTab = EIHPCSideTab::None;
};

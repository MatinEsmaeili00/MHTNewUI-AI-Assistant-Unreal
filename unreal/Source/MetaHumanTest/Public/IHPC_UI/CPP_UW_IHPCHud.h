// CPP_UW_IHPCHud.h
// -----------------------------------------------------------------------------
// The full-screen IHPC HUD. Figma page "FINAL FESTIVAL" (file "Avatar Injury Prevention Program").
//
//   +------------------------------------------------------------------------------+
//   |                   AVATAR INJURY PREVENTION TECHNOLOGY                         |  title bar (#1D3C55, 112 px)
//   +------------------------------------------------------------------------------+
//   | (o) Graphs     (x) | (BOT) [chat]                                      ( @ ) |  <- reset view (only after
//   | [ Arms        v ]  | (=)                                                      |     the camera was moved)
//   | [ Legs        v ]  | (m)   layer buttons                                      |
//   |  ...               | (s)                                                      |
//   |                    | (a)   tabs (side bar, expanded)                          |
//   |  side panel        | ...                                                      |
//   |  (332 px)          | (^)                                                      |
//   |                    |        (|<) (||) (-) ===o===(x1.5)--- (+) (>) (>|)       |  timeline (compact)
//   +------------------------------------------------------------------------------+
//
//   * SIDE BAR (WBP_IHPC_SideBar): BOT avatar (chat), Body / Muscles / Skeleton layer buttons, the tabs, the chevron.
//     The tabs replace the old drop-down nav bar.
//   * Clicking a tab opens its SIDE PANEL on the left (Figma "Rectangle 31": #0E2334, 332 wide, below the title
//     bar); the side bar and the timeline slide right to make room (Figma frames 19-22). Clicking the tab again or the
//     panel's round X closes it. One panel at a time:
//         Action          -> ActionsPanel          (Idle / Running / Jogging / Jumping ... cards -> avatar actions)
//         Pose Estimation -> PoseEstimationContent (empty in the Widget Blueprint: at Play, UPoseEstimationComp puts
//                            the pose data in it - overlay switch, live graph, angles, Record - and follows the tab)
//         Graphs          -> GraphsPanel           (body regions; Movement Graph + Active Muscles, filters, export)
//         Timeline        -> TaggedPointsList      (tagged points / bookmarks / recordings; click = jump there)
//         Saved Videos    -> SavedVideosList       (video cards; click = jump there)
//         Settings        -> SettingsContent       (empty: ready for settings)
//     The panel title and icon come from the tab (its Label and normal picture).
//   * TIMELINE (WBP_IHPC_Timeline): centred at the bottom; compact (no Record / Bookmark) while a panel is open.
//     New recordings -> Saved Videos + Timeline panel + marker; bookmarks -> Timeline panel + marker.
//   * ZOOMED GRAPH: the graph card's zoom icon shows a large Movement Graph in the free part of the screen.
//   * RESET VIEW (Figma frame 17, round arrow at the top right): puts the camera back where it started.
//
// Clicks on empty parts of the HUD pass straight through to the 3D view, so the mouse orbit / pan / zoom keeps working.
//
// Widget Blueprint: /Game/UserInterface/IHPC/Widgets/WBP_IHPC_HUD
// Created by ACPP_PlayerController::BeginPlay (MainHUDWidgetClass, set in BP_PlayerController), drawn above the other
// widgets (Z-order 10). The layer switcher is now INSIDE the side bar, so BP_PlayerController no longer creates its own
// (Anatomy View Switcher Widget Class = None).
// -----------------------------------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "IHPC_UI/IHPCTypes.h"
#include "CPP_UW_IHPCHud.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UTexture2D;
class UCPP_UW_IHPCSideBar;
class UCPP_UW_IHPCActionsPanel;
class UCPP_UW_IHPCGraphsPanel;
class UCPP_UW_IHPCTaggedPointsList;
class UCPP_UW_IHPCVideosRow;
class UCPP_UW_IHPCTimeline;
class UCPP_UW_IHPCLineGraph;
class UCPP_UW_IHPCLayerSwitcher;
class UCPP_UW_IHPCChatPanel;

/** Old nav bar tab changed (kept for Blueprints that listen to OnTabChanged). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIHPCTabChanged, EIHPCNavTab, NewTab);

/** An action card started an action (ActionName) or the avatar went back to its default animation (None). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIHPCActionChanged, FName, ActionName);

UCLASS(Abstract)
class METAHUMANTEST_API UCPP_UW_IHPCHud : public UUserWidget
{
	GENERATED_BODY()

public:
	UCPP_UW_IHPCHud(const FObjectInitializer& ObjectInitializer);

	// ---------------------------------------------------------------- behaviour

	/** Panel open when the game starts (Figma frame 15: none). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IHPC|Behaviour")
	EIHPCSideTab StartTab = EIHPCSideTab::None;

	/** Close the Action panel after picking an action. Off by default: the side panel does not cover the avatar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Behaviour")
	bool bCloseActionsPanelOnSelect = false;

	/** Only show the reset-view button after the camera was orbited / panned / zoomed (Figma shows it in one frame). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Behaviour")
	bool bShowResetViewOnlyWhenMoved = true;

	/** Hide the timeline's Record / Bookmark buttons while a panel is open (Figma frames 19-22: 916 px bar). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Behaviour")
	bool bCompactTimelineWhenPanelOpen = true;

	/** Title of a bookmark made with a graph card's bookmark icon: {0} = body region, {1} = number. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Behaviour")
	FText GraphBookmarkTitleFormat;

	// ---------------------------------------------------------------- layout (Figma px)

	/** Width of the side panel (Figma: x 0 - 332). It slides in from the left by this much. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Layout")
	float SidePanelWidth = 332.f;

	/** How far the side bar moves right while a panel is open (Figma: x 24 -> 346). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Layout")
	float SideBarShiftWhenPanelOpen = 322.f;

	/** How far the timeline (and the zoomed graph) move right while a panel is open (Figma: bar centre 683 -> 860). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Layout")
	float ContentShiftWhenPanelOpen = 177.f;

	/** Seconds the panel takes to slide in / out (0 = instant). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Layout", meta = (ClampMin = "0"))
	float PanelSlideTime = 0.2f;

	// ---------------------------------------------------------------- style

	/** Empty frosted glass circle (Icons/Button.png) behind the panel's X and the zoomed graph's X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	TObjectPtr<UTexture2D> RoundButtonTexture;

	/** Size of the panel's close X button (Figma "ihpc ui (1)4ety-06 3": 42 x 42). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	FVector2D CloseButtonSize = FVector2D(42.f, 42.f);

	/** Reset view circle (Figma frame 17 "Rectangle 31": rgba(12,39,61,0.66), 68 x 68, fully round). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	FLinearColor ResetViewColor;

	// ---------------------------------------------------------------- events

	/** The open side panel changed (None = closed). */
	UPROPERTY(BlueprintAssignable, Category = "IHPC|Events")
	FIHPCSideTabEvent OnSideTabChanged;

	/** OLD nav bar event, still fired: Saved Videos, Settings, and HistoryTimeline (= the Action panel, which the
	 *  old 4th tab had become), None for the other panels. Prefer OnSideTabChanged. */
	UPROPERTY(BlueprintAssignable, Category = "IHPC|Events")
	FIHPCTabChanged OnTabChanged;

	UPROPERTY(BlueprintAssignable, Category = "IHPC|Events")
	FIHPCActionChanged OnActionChanged;

	/** The user sent a chat message to the assistant (BOT). The project's assistant (UIHPCAssistantComp on the player
	 *  controller) listens to the chat panel itself; this event is for anything else that wants to follow the chat. */
	UPROPERTY(BlueprintAssignable, Category = "IHPC|Events")
	FIHPCTextEvent OnChatMessageSent;

	// ---------------------------------------------------------------- side panels

	/** Opens Tab's panel (closing any other) and highlights its tab. None = close the panel. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|HUD")
	void SelectSideTab(EIHPCSideTab Tab);

	/** What a tab click does: opens Tab's panel, or closes it if it is already open. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|HUD")
	void ToggleSideTab(EIHPCSideTab Tab);

	UFUNCTION(BlueprintPure, Category = "IHPC|HUD")
	EIHPCSideTab GetSelectedSideTab() const { return SelectedTab; }

	/** Closes the open panel (what the round X does). */
	UFUNCTION(BlueprintCallable, Category = "IHPC|HUD")
	void ClosePanel() { SelectSideTab(EIHPCSideTab::None); }

	UFUNCTION(BlueprintPure, Category = "IHPC|HUD")
	bool IsAnyPanelOpen() const { return SelectedTab != EIHPCSideTab::None; }

	/** The content widget a tab shows inside the side panel (null for None / a missing widget). */
	UFUNCTION(BlueprintPure, Category = "IHPC|HUD")
	UWidget* GetPanelForSideTab(EIHPCSideTab Tab) const;

	/** Shows every tab (true) or only the layer buttons (false). */
	UFUNCTION(BlueprintCallable, Category = "IHPC|HUD")
	void SetSideBarExpanded(bool bExpanded);

	UFUNCTION(BlueprintPure, Category = "IHPC|HUD")
	bool IsSideBarExpanded() const;

	// ---------------------------------------------------------------- actions (Action panel)

	/** Same as clicking the card for ActionName (None = Idle): plays it on the possessed pawn, or goes back to the
	 *  default animation if it is already playing. Returns true if an action clip is now playing. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|Actions")
	bool SelectAction(FName ActionName);

	/** Action playing on the possessed pawn, or None while it uses its default animation. */
	UFUNCTION(BlueprintPure, Category = "IHPC|Actions")
	FName GetPlayingAction() const;

	/** Re-reads the possessed pawn's actions (available / highlighted cards). */
	UFUNCTION(BlueprintCallable, Category = "IHPC|Actions")
	void RefreshActionButtons();

	// ---------------------------------------------------------------- saved videos / tagged points

	/** A new clip: a card in Saved Videos, a row in the Timeline panel (the HUD also adds its timeline marker when
	 *  it comes from the Record button). */
	UFUNCTION(BlueprintCallable, Category = "IHPC|HUD")
	void AddSavedVideo(const FIHPCTaggedMoment& Moment);

	/** A row in the Timeline panel only (e.g. a bookmark). */
	UFUNCTION(BlueprintCallable, Category = "IHPC|HUD")
	void AddTaggedPoint(const FIHPCTaggedMoment& Moment);

	UFUNCTION(BlueprintPure, Category = "IHPC|HUD")
	TArray<FIHPCTaggedMoment> GetSavedVideos() const;

	// ---------------------------------------------------------------- camera / graphs

	/** Puts the camera back to its starting view (the round arrow button). */
	UFUNCTION(BlueprintCallable, Category = "IHPC|HUD")
	void ResetView();

	/** Shows / hides the large Movement Graph (the graph card's zoom icon). */
	UFUNCTION(BlueprintCallable, Category = "IHPC|HUD")
	void SetZoomedGraphVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "IHPC|HUD")
	bool IsZoomedGraphVisible() const { return bZoomedGraphVisible; }

	// ---------------------------------------------------------------- parts

	UFUNCTION(BlueprintPure, Category = "IHPC|HUD")
	UCPP_UW_IHPCSideBar* GetSideBar() const { return SideBar; }

	UFUNCTION(BlueprintPure, Category = "IHPC|HUD")
	UCPP_UW_IHPCTimeline* GetTimeline() const { return Timeline; }

	UFUNCTION(BlueprintPure, Category = "IHPC|HUD")
	UCPP_UW_IHPCLayerSwitcher* GetLayerSwitcher() const;

	UFUNCTION(BlueprintPure, Category = "IHPC|HUD")
	UCPP_UW_IHPCChatPanel* GetChatPanel() const;

	UFUNCTION(BlueprintPure, Category = "IHPC|HUD")
	UCPP_UW_IHPCActionsPanel* GetActionsPanel() const { return ActionsPanel; }

	// ---------------------------------------------------------------- OLD nav bar calls (still work)

	/** OLD: selects the side panel that replaced the nav tab (Saved Videos -> Saved Videos, History & Timeline ->
	 *  Action, Settings -> Settings; Profile / Progress no longer exist -> closes the panel). */
	UFUNCTION(BlueprintCallable, Category = "IHPC|HUD|Old Nav Bar")
	void SelectTab(EIHPCNavTab Tab);

	/** OLD: the open panel as a nav tab (see OnTabChanged). */
	UFUNCTION(BlueprintPure, Category = "IHPC|HUD|Old Nav Bar")
	EIHPCNavTab GetSelectedTab() const;

	/** OLD: the nav bar is gone; this expands / collapses the side bar's tabs instead. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|HUD|Old Nav Bar")
	void SetNavBarOpen(bool bOpen) { SetSideBarExpanded(bOpen); }

	UFUNCTION(BlueprintCallable, Category = "IHPC|HUD|Old Nav Bar")
	void ToggleNavBar() { SetSideBarExpanded(!IsSideBarExpanded()); }

	UFUNCTION(BlueprintPure, Category = "IHPC|HUD|Old Nav Bar")
	bool IsNavBarOpen() const { return IsSideBarExpanded(); }

	/** OLD: the analysis panel was split into the Graphs / Timeline / Saved Videos panels; this opens Saved Videos. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|HUD|Old Nav Bar")
	void SetAnalysisPanelOpen(bool bOpen) { SelectSideTab(bOpen ? EIHPCSideTab::SavedVideos : EIHPCSideTab::None); }

	static EIHPCSideTab SideTabFromNavTab(EIHPCNavTab Tab);
	static EIHPCNavTab NavTabFromSideTab(EIHPCSideTab Tab);

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ---- Widgets in the Widget Blueprint (names must match; all optional so the HUD still runs with parts missing)

	/** WBP_IHPC_SideBar (top left, below the title bar). */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UCPP_UW_IHPCSideBar> SideBar;

	/** The side panel frame (background, header, contents). Slides in from the left. */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SidePanel;

	/** Panel header: the tab's icon, its name, the round close X. */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> PanelIcon;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> PanelTitle;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> PanelCloseButton;

	/** The six panel contents (one shown at a time). */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UCPP_UW_IHPCActionsPanel> ActionsPanel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> PoseEstimationContent;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UCPP_UW_IHPCGraphsPanel> GraphsPanel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UCPP_UW_IHPCTaggedPointsList> TaggedPointsList;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UCPP_UW_IHPCVideosRow> SavedVideosList;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> SettingsContent;

	/** Playback / timeline bar at the bottom (Figma "ihpc ui (1)ee 1"). */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UCPP_UW_IHPCTimeline> Timeline;

	/** Round "reset view" button at the top right (Figma frame 17). */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ResetViewButton;

	/** Large Movement Graph (graph card zoom icon). */
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> ZoomedGraphPanel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UCPP_UW_IHPCLineGraph> ZoomedGraph;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ZoomedGraphTitle;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ZoomedGraphCloseButton;

private:
	UFUNCTION() void HandleSideTabClicked(EIHPCSideTab Tab);
	UFUNCTION() void HandlePanelCloseClicked();
	UFUNCTION() void HandleRecordingSaved(const FIHPCTaggedMoment& Moment);
	UFUNCTION() void HandleBookmarkAdded(const FIHPCTaggedMoment& Moment);
	UFUNCTION() void HandleActionSelected(FName PlayingAction, bool bStartedAction);
	UFUNCTION() void HandleZoomRequested();
	UFUNCTION() void HandleZoomedGraphCloseClicked();
	UFUNCTION() void HandleGraphBookmarkRequested(const FText& RegionName);
	UFUNCTION() void HandleChatMessageSent(const FText& Message);
	UFUNCTION() void HandleResetViewClicked();

	/** Shows the content of DisplayedTab, sets the panel title / icon. */
	void RefreshPanelContent();

	/** Moves the side panel, side bar, timeline and zoomed graph for a slide amount (0 = closed, 1 = open). */
	void ApplyPanelSlide(float Alpha);

	void ApplyStyles();

	/** Panel that is open (or None). */
	EIHPCSideTab SelectedTab = EIHPCSideTab::None;

	/** Panel whose content is shown - stays the closing panel while it slides out. */
	EIHPCSideTab DisplayedTab = EIHPCSideTab::None;

	float PanelSlideAlpha = 0.f;
	bool bPanelSlideApplied = false;

	bool bZoomedGraphVisible = false;
	int32 GraphBookmarkCount = 0;
};

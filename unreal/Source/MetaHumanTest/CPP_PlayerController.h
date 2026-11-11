// Player controller: owns input (Enhanced Input -> components on the possessed pawn), puts the UI on the
// viewport, and switches which pawn is possessed. It does not do the pawn's work itself: the widgets and
// input talk to components on whatever pawn is possessed, so a different pawn = different behaviour.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CPP_PlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
struct FInputActionValue;
class UCPP_MouseFunctionComp;
class UCPP_UW_AnatomyViewSwitcher;
class UCPP_UW_MuscleInfo;
class UAnatomyPickerComp;
class UCPP_UW_PawnSwitcher;
class UCPP_UW_SpeedSlider;
class UWCPP_AnimationTimeline;
class UCPP_UW_IHPCHud;
class UPoseEstimationComp;
class UIHPCAssistantComp;

UCLASS()
class METAHUMANTEST_API ACPP_PlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACPP_PlayerController();

	// ---------------- Pose estimation ----------------

	/** Sports2D-style pose estimation of the live view (overlay + data). Settings: select it in BP_PlayerController. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pose Estimation")
	TObjectPtr<UPoseEstimationComp> PoseEstimation;

	UFUNCTION(BlueprintPure, Category="Pose Estimation")
	UPoseEstimationComp* GetPoseEstimation() const { return PoseEstimation; }

	// ---------------- Assistant (the BOT chat) ----------------

	/** The AI assistant behind the BOT chat: sends what is typed (or said while the BOT logo is held) to the AI server
	 *  and runs the actions that come back - layers, timeline, camera, pose estimation, "show me the biceps".
	 *  It also writes the description of a clicked muscle / bone into the chat box.
	 *  Settings (server address...): select it in BP_PlayerController. See Assistant/IHPCAssistantComp.h. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Assistant")
	TObjectPtr<UIHPCAssistantComp> Assistant;

	UFUNCTION(BlueprintPure, Category="Assistant")
	UIHPCAssistantComp* GetAssistant() const { return Assistant; }

	// ---------------- Input ----------------

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputMappingContext> MouseManager;

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> MouseMove;

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> MouseZoom;

	// ---------------- Touch input (iPad / touch screens / Pixel Streaming touch events) ----------------
	// One finger drags = orbit, two fingers pinch = zoom. See HandleTouchInput.

	/** Degrees the camera turns when one finger is dragged across the full width of the screen. Does not depend on
	 *  the screen resolution, so a Retina iPad and a 1080p touch screen feel the same. Lower = slower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input|Touch", meta=(ClampMin="0.0", ClampMax="1080.0"))
	float TouchOrbitDegreesPerScreen = 180.f;

	/** Off: dragging down raises the camera (the avatar follows the finger, same as the mouse). On: the opposite. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input|Touch")
	bool bTouchInvertPitch = false;

	/** How far one finger has to move before the orbit starts, as a fraction of the screen's short side. Keeps a tap,
	 *  or the first finger of a pinch landing a moment before the second, from turning the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input|Touch", meta=(ClampMin="0.0", ClampMax="0.2"))
	float TouchDragStartScreenFraction = 0.01f;

	/** 1 = the avatar grows / shrinks with the distance between the two fingers. Higher = stronger zoom,
	 *  negative = reversed direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input|Touch", meta=(ClampMin="-4.0", ClampMax="4.0"))
	float TouchPinchZoomPower = 1.f;

	/** How fast the camera follows the pinch (interpolation speed, 1/s). Lower = smoother, 0 = no smoothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input|Touch", meta=(ClampMin="0.0"))
	float TouchPinchFollowSpeed = 14.f;

	/** Finger distances below this (viewport pixels) count as this, so two fingers almost touching can't make a huge zoom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input|Touch", meta=(ClampMin="1.0"))
	float TouchMinPinchDistance = 35.f;

	// ---------------- MetaHuman / placeholders ----------------

	// access the meta human // need to be attached on bp
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MetaHuman")
	TObjectPtr<AActor> TargetMetaHuman = nullptr;

	/** Filled in BeginPlay from the BP_PlayerPlaceHolderManagerA in the level: where switched-to pawns are spawned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MetaHuman")
	TArray<TObjectPtr<AActor>> PC_PlayerPlaceHolders;

	// ---------------- UI (set to the widget Blueprints in BP_PlayerController) ----------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UI")
	TSubclassOf<UCPP_UW_SpeedSlider> SpeedSliderWidget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UI")
	TSubclassOf<UWCPP_AnimationTimeline> TimelineWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UI")
	TSubclassOf<UCPP_UW_AnatomyViewSwitcher> AnatomyViewSwitcherWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UI")
	TSubclassOf<UCPP_UW_PawnSwitcher> PawnSwitcherWidgetClass;

	/** Muscle name tooltip + info popup (listens to the pawn's UAnatomyPickerComp). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UI")
	TSubclassOf<UCPP_UW_MuscleInfo> MuscleInfoWidgetClass;

	/** IHPC HUD from the Figma design (title bar, menu, analysis panel, timeline bar).
	 *  Set to WBP_IHPC_HUD in BP_PlayerController > Class Defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UI")
	TSubclassOf<UCPP_UW_IHPCHud> MainHUDWidgetClass;

	/** Draw order of the HUD: higher = on top. 10 keeps it above the other widgets (they use 0); the muscle info
	 *  tooltip / popup (20) stays above the HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UI")
	int32 MainHUDZOrder = 10;

	/** The HUD created in BeginPlay, e.g. GetMainHUD()->SelectTab(EIHPCNavTab::SavedVideos) from a key press. */
	UFUNCTION(BlueprintPure, Category="UI")
	UCPP_UW_IHPCHud* GetMainHUD() const { return MainHUDWidget; }

	/** A left click that moves less than this (pixels) selects the hovered muscle; more is a pan drag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input")
	float ClickMaxDragPixels = 6.f;

	// ---------------- Pawn switching (WB_PawnSwitcher) ----------------

	/** Pawns the Up / Down buttons cycle through. A class that isn't in the level yet is spawned at the matching placeholder. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pawn Switching")
	TArray<TSubclassOf<APawn>> SwitchablePawnClasses;

	/** Possess the next (+1) or previous (-1) pawn in SwitchablePawnClasses. */
	UFUNCTION(BlueprintCallable, Category="Pawn Switching")
	void SwitchPawn(int32 Direction);

	UFUNCTION(BlueprintCallable, Category="Pawn Switching")
	void SwitchToPawnIndex(int32 Index);

	/** Index in SwitchablePawnClasses of the possessed pawn. */
	UFUNCTION(BlueprintPure, Category="Pawn Switching")
	int32 GetCurrentPawnIndex() const { return CurrentPawnIndex; }

	/** Only the possessed pawn is shown: every other pawn in the level (the avatars you switched away from, or ones
	 *  placed in the map) is hidden, at start and on every switch. A hidden avatar keeps its state (camera, layer,
	 *  place in the animation) and comes back as it was. The static models of StaticModelFolders are hidden too. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pawn Switching")
	bool bShowOnlyPossessedPawn = true;

	/** Static Mesh Actors whose mesh is in one of these content folders are hidden with the other avatars: the
	 *  static skeleton and muscle models that stand in the Main level. Empty = leave the level's meshes alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pawn Switching")
	TArray<FString> StaticModelFolders = { TEXT("/Game/Skeleton3D/"), TEXT("/Game/3D_Models/") };

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void OnPossess(APawn* InPawn) override;

	/** Hides every pawn but the possessed one, and the static models (see bShowOnlyPossessedPawn). */
	void ShowOnlyPossessedPawn();

	void HandleMouseMove(const FInputActionValue& Value);
	void HandleMouseZoom(const FInputActionValue& Value);
	void HandleTouchInput(float DeltaTime);

public:
	/** Console: "DebugTouch <finger 0-9> <x 0-1> <y 0-1> <0 = down, 1 = move, 2 = up>". Sends a touch to this
	 *  controller the same way the viewport does (x, y are fractions of the screen), so the touch gestures can be
	 *  tested without a touch device. The finger stays down until the "up" command. */
	UFUNCTION(Exec)
	void DebugTouch(int32 Finger, float X, float Y, int32 Phase);

protected:

	/** Mouse orbit / pan / zoom is done by the possessed pawn's component (found fresh, so pawn switches just work). */
	UCPP_MouseFunctionComp* GetMouseFunctionComp() const;

	/** Hover / click on muscles is done by the possessed pawn's picker component (if it has one). */
	void UpdateMusclePicking();

private:
	template <typename WidgetT>
	WidgetT* AddWidget(TSubclassOf<WidgetT> WidgetClass, int32 ZOrder = 0);

	UPROPERTY()
	TObjectPtr<UCPP_UW_SpeedSlider> SpeedSliderWidgetInstance = nullptr;

	UPROPERTY()
	TObjectPtr<UWCPP_AnimationTimeline> TimelineWidget = nullptr;

	UPROPERTY()
	TObjectPtr<UCPP_UW_AnatomyViewSwitcher> AnatomyViewSwitcherWidget = nullptr;

	UPROPERTY()
	TObjectPtr<UCPP_UW_PawnSwitcher> PawnSwitcherWidget = nullptr;

	UPROPERTY()
	TObjectPtr<UCPP_UW_MuscleInfo> MuscleInfoWidget = nullptr;

	/** IHPC HUD instance (see MainHUDWidgetClass). */
	UPROPERTY()
	TObjectPtr<UCPP_UW_IHPCHud> MainHUDWidget = nullptr;

	FVector2D LeftPressPosition = FVector2D::ZeroVector;

	// Touch gesture state (HandleTouchInput).
	enum class ETouchGesture : uint8
	{
		None,     // no finger on the screen
		Pending,  // one finger down, not moved far enough to be a drag yet
		Orbit,    // one finger dragging
		Pinch,    // two fingers
		Blocked   // one finger left over after a pinch: ignored until every finger is lifted
	};
	ETouchGesture TouchGesture = ETouchGesture::None;
	/** Touch indices the current gesture follows (a changed finger starts the gesture again instead of jumping). */
	int32 TouchFingerA = INDEX_NONE;
	int32 TouchFingerB = INDEX_NONE;
	FVector2D TouchPressPosition = FVector2D::ZeroVector;
	FVector2D LastTouchPosition = FVector2D::ZeroVector;
	/** Finger distance and camera distance when the pinch started: the zoom is always computed from these two,
	 *  never accumulated frame by frame. */
	float PinchStartFingerDistance = 0.f;
	float PinchStartCameraDistance = 0.f;

	/** One instance per SwitchablePawnClasses entry, created on first use (kept alive so switching back is instant). */
	UPROPERTY()
	TArray<TObjectPtr<APawn>> SwitchablePawns;

	int32 CurrentPawnIndex = 0;
};

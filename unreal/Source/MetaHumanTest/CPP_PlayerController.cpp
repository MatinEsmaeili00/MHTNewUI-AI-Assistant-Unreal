#include "CPP_PlayerController.h"

#include "CPP_MouseFunctionComp.h"
#include "CPP_PlayerPlaceHolderManagerA.h"
#include "CPP_UW_AnatomyViewSwitcher.h"
#include "CPP_UW_MuscleInfo.h"
#include "AnatomyPickerComp.h"
#include "CPP_UW_PawnSwitcher.h"
#include "CPP_UW_SpeedSlider.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "WCPP_AnimationTimeline.h"
#include "IHPC_UI/CPP_UW_IHPCHud.h"
#include "PoseEstimation/PoseEstimationComp.h"
#include "Assistant/IHPCAssistantComp.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

// Touch gestures (HandleTouchInput). "Log LogIHPCTouch Verbose" in the console prints every gesture start.
DEFINE_LOG_CATEGORY_STATIC(LogIHPCTouch, Log, All);

ACPP_PlayerController::ACPP_PlayerController()
{
	// On the controller (not the pawn) so it keeps running when the possessed pawn changes.
	PoseEstimation = CreateDefaultSubobject<UPoseEstimationComp>(TEXT("PoseEstimation"));
	// Also on the controller: the chat, the server connection and the conversation outlive a pawn switch.
	Assistant = CreateDefaultSubobject<UIHPCAssistantComp>(TEXT("Assistant"));
}

void ACPP_PlayerController::BeginPlay()
{
	Super::BeginPlay(); // was missing: BP_PlayerController's own BeginPlay never ran

	// Touch screens (iPad) and Pixel Streaming frontends that forward touch events: see HandleTouchInput.
	bEnableTouchEvents = true;

	bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsys = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (MouseManager)
			{
				Subsys->AddMappingContext(MouseManager, /*Priority*/ 1);
			}
		}
	}

	// UI: the controller only creates the widgets; each widget finds its component on the possessed pawn
	// and re-binds by itself when the possessed pawn changes.
	SpeedSliderWidgetInstance = AddWidget(SpeedSliderWidget);
	TimelineWidget = AddWidget(TimelineWidgetClass);
	AnatomyViewSwitcherWidget = AddWidget(AnatomyViewSwitcherWidgetClass);
	PawnSwitcherWidget = AddWidget(PawnSwitcherWidgetClass);
	MuscleInfoWidget = AddWidget(MuscleInfoWidgetClass, /*ZOrder*/ 20);
	// IHPC HUD (title bar, menu, analysis panel, timeline bar), drawn above the other widgets.
	MainHUDWidget = AddWidget(MainHUDWidgetClass, MainHUDZOrder);

	// Placeholders (was looking for APlayerCameraManager, so the manager was never found).
	if (ACPP_PlayerPlaceHolderManagerA* PlaceHolderManager = Cast<ACPP_PlayerPlaceHolderManagerA>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ACPP_PlayerPlaceHolderManagerA::StaticClass())))
	{
		PC_PlayerPlaceHolders.Reset();
		for (AActor* PlaceHolder : PlaceHolderManager->PlayerPlaceHolders)
		{
			PC_PlayerPlaceHolders.Add(PlaceHolder);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("No PlayerPlaceholderManager found in level!"));
	}

	// The pawn the game mode gave us is the first switchable pawn of its class.
	SwitchablePawns.SetNum(SwitchablePawnClasses.Num());
	if (APawn* Current = GetPawn())
	{
		const int32 Index = SwitchablePawnClasses.IndexOfByPredicate([Current](const TSubclassOf<APawn>& Class) { return Class && Current->IsA(Class); });
		if (Index != INDEX_NONE)
		{
			SwitchablePawns[Index] = Current;
			CurrentPawnIndex = Index;
		}
	}

	ShowOnlyPossessedPawn(); // again, for pawns that were not in the world yet when the first pawn was possessed
}

template <typename WidgetT>
WidgetT* ACPP_PlayerController::AddWidget(TSubclassOf<WidgetT> WidgetClass, int32 ZOrder)
{
	if (!WidgetClass)
	{
		return nullptr;
	}
	WidgetT* Widget = CreateWidget<WidgetT>(this, WidgetClass);
	if (Widget)
	{
		Widget->AddToViewport(ZOrder);
	}
	return Widget;
}

void ACPP_PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (MouseMove)
		{
			EIC->BindAction(MouseMove, ETriggerEvent::Triggered, this, &ACPP_PlayerController::HandleMouseMove);
		}
		if (MouseZoom)
		{
			EIC->BindAction(MouseZoom, ETriggerEvent::Triggered, this, &ACPP_PlayerController::HandleMouseZoom);
		}
	}
}

void ACPP_PlayerController::HandleMouseMove(const FInputActionValue& Value)
{
	const FVector2D MouseDelta = Value.Get<FVector2D>();

	if (UCPP_MouseFunctionComp* MouseComp = GetMouseFunctionComp())
	{
		if (IsInputKeyDown(EKeys::RightMouseButton))
		{
			MouseComp->OrbitFromMouse(MouseDelta); // rotate / orbit
		}
		else if (IsInputKeyDown(EKeys::LeftMouseButton))
		{
			MouseComp->PanFromMouse(MouseDelta); // pan / relocate camera
		}
	}
}

void ACPP_PlayerController::HandleMouseZoom(const FInputActionValue& Value)
{
	if (UCPP_MouseFunctionComp* MouseComp = GetMouseFunctionComp())
	{
		MouseComp->ZoomCamera(Value.Get<float>());
	}
}

void ACPP_PlayerController::HandleTouchInput(float DeltaTime)
{
	// The first two fingers on the screen (positions in viewport pixels).
	FVector2D Fingers[2] = { FVector2D::ZeroVector, FVector2D::ZeroVector };
	int32 FingerIndices[2] = { INDEX_NONE, INDEX_NONE };
	int32 NumFingers = 0;
	// A finger that landed or lifted this frame. Read from the press / release events, not from the pressed state:
	// the state alone misses a lift and a new press that both happen in one frame.
	bool bFingerLanded = false;
	bool bFingerLifted = false;
	for (int32 Index = ETouchIndex::Touch1; Index <= ETouchIndex::Touch10; ++Index)
	{
		bFingerLanded |= WasInputKeyJustPressed(EKeys::TouchKeys[Index]);
		bFingerLifted |= WasInputKeyJustReleased(EKeys::TouchKeys[Index]);

		float X = 0.f, Y = 0.f;
		bool bPressed = false;
		GetInputTouchState(static_cast<ETouchIndex::Type>(Index), X, Y, bPressed);
		if (bPressed && NumFingers < 2)
		{
			Fingers[NumFingers] = FVector2D(X, Y);
			FingerIndices[NumFingers] = Index;
			++NumFingers;
		}
	}

	UCPP_MouseFunctionComp* MouseComp = GetMouseFunctionComp();
	if (!MouseComp || NumFingers == 0)
	{
		if (TouchGesture != ETouchGesture::None)
		{
			UE_LOG(LogIHPCTouch, Verbose, TEXT("Touch: all fingers lifted, camera at %.0f"), MouseComp ? MouseComp->CurrentCameraDistance : 0.f);
		}
		TouchGesture = ETouchGesture::None; // lifting every finger ends any gesture
		return;
	}

	int32 ViewportX = 0, ViewportY = 0;
	GetViewportSize(ViewportX, ViewportY);
	if (ViewportX <= 0 || ViewportY <= 0)
	{
		return;
	}

	const bool bSameFingers = FingerIndices[0] == TouchFingerA && FingerIndices[1] == TouchFingerB;
	TouchFingerA = FingerIndices[0];
	TouchFingerB = FingerIndices[1];

	// Either of these = the fingers are not the ones the gesture was following: start it again, move nothing.
	const bool bRestart = bFingerLanded || !bSameFingers;

	// ---- Two fingers: pinch to zoom ----
	if (NumFingers == 2)
	{
		const float FingerDistance = FMath::Max<float>(FVector2D::Distance(Fingers[0], Fingers[1]), TouchMinPinchDistance);

		if (TouchGesture != ETouchGesture::Pinch || bRestart || bFingerLifted)
		{
			// Start: remember where the fingers and the camera are. Nothing moves on this frame.
			TouchGesture = ETouchGesture::Pinch;
			PinchStartFingerDistance = FingerDistance;
			PinchStartCameraDistance = MouseComp->CurrentCameraDistance;
			UE_LOG(LogIHPCTouch, Verbose, TEXT("Touch: pinch starts, fingers %d+%d %.0f px apart, camera at %.0f"),
				FingerIndices[0], FingerIndices[1], FingerDistance, PinchStartCameraDistance);
			return;
		}

		// Camera distance for this finger distance, always from the start of the pinch. Fingers twice as far apart
		// = camera twice as close (TouchPinchZoomPower 1), so the avatar scales with the fingers. Because nothing
		// is added up frame by frame, finger jitter can't pile up and a fast pinch doesn't lose any of its travel.
		const float WantedDistance = PinchStartCameraDistance * FMath::Pow(PinchStartFingerDistance / FingerDistance, TouchPinchZoomPower);
		const float TargetDistance = FMath::Clamp(WantedDistance, MouseComp->MinZoom, MouseComp->MaxZoom);
		if (!FMath::IsNearlyEqual(WantedDistance, TargetDistance))
		{
			// At the zoom limit: start again from here, so pinching back the other way reacts at once.
			PinchStartFingerDistance = FingerDistance;
			PinchStartCameraDistance = TargetDistance;
		}

		// Ease towards it: this takes out what is left of the sensor noise (the flicker).
		MouseComp->SetCameraDistance(FMath::FInterpTo(MouseComp->CurrentCameraDistance, TargetDistance, DeltaTime, TouchPinchFollowSpeed));
		return;
	}

	// ---- One finger ----
	if (TouchGesture == ETouchGesture::None || bFingerLanded)
	{
		// A new touch. Not an orbit yet: see Pending below.
		TouchGesture = ETouchGesture::Pending;
		TouchPressPosition = Fingers[0];
		UE_LOG(LogIHPCTouch, Verbose, TEXT("Touch: finger %d down at %s"), FingerIndices[0], *Fingers[0].ToString());
	}
	else if (TouchGesture == ETouchGesture::Pinch || TouchGesture == ETouchGesture::Blocked)
	{
		// The finger left over from a pinch must not turn the camera.
		TouchGesture = ETouchGesture::Blocked;
	}
	else if (bRestart)
	{
		UE_LOG(LogIHPCTouch, Verbose, TEXT("Touch: finger %d takes over at %s"), FingerIndices[0], *Fingers[0].ToString());
		TouchGesture = ETouchGesture::Pending;
		TouchPressPosition = Fingers[0];
	}
	else if (TouchGesture == ETouchGesture::Pending)
	{
		// Not an orbit until the finger really drags (a tap, or the first finger of a pinch, stays still).
		if (FVector2D::Distance(Fingers[0], TouchPressPosition) >= TouchDragStartScreenFraction * FMath::Min(ViewportX, ViewportY))
		{
			TouchGesture = ETouchGesture::Orbit; // starts from this position, so the camera doesn't jump
			UE_LOG(LogIHPCTouch, Verbose, TEXT("Touch: orbit starts at %s"), *Fingers[0].ToString());
		}
	}
	else // Orbit
	{
		// Screen pixels -> degrees through the screen width (a mouse delta is far smaller than a touch delta on a
		// Retina screen, which is why feeding touch pixels to OrbitFromMouse was much too fast).
		const FVector2D Degrees = (Fingers[0] - LastTouchPosition) * (TouchOrbitDegreesPerScreen / ViewportX);
		// Touch Y grows downwards, the mouse delta's Y grows upwards: flipped so both orbit the same way.
		MouseComp->OrbitByDegrees(Degrees.X, bTouchInvertPitch ? Degrees.Y : -Degrees.Y);
	}
	LastTouchPosition = Fingers[0];
}

void ACPP_PlayerController::DebugTouch(int32 Finger, float X, float Y, int32 Phase)
{
	int32 ViewportX = 0, ViewportY = 0;
	GetViewportSize(ViewportX, ViewportY);
	if (Finger < 0 || Finger >= EKeys::NUM_TOUCH_KEYS || Phase < 0 || Phase > 2)
	{
		return;
	}
	const ETouchType::Type Types[] = { ETouchType::Began, ETouchType::Moved, ETouchType::Ended };
	const FTouchId TouchId(FInputDeviceId::CreateFromInternalId(0), static_cast<ETouchIndex::Type>(Finger));
	InputTouch(TouchId, Types[Phase], FVector2D(X * ViewportX, Y * ViewportY), 1.f, FPlatformTime::Cycles64());
}

void ACPP_PlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	HandleTouchInput(DeltaTime);
	UpdateMusclePicking();
}

void ACPP_PlayerController::UpdateMusclePicking()
{
	// One picker per pickable layer (muscles, bones...). Each one only picks while its own layer is the one
	// you're looking at (UAnatomyPickerComp::IsPickingActive), so at most one of them reacts at a time.
	TArray<UAnatomyPickerComp*> Pickers;
	if (APawn* CurrentPawn = GetPawn())
	{
		CurrentPawn->GetComponents<UAnatomyPickerComp>(Pickers);
	}
	if (Pickers.Num() == 0)
	{
		return;
	}

	// Hover: the ray from the camera through the mouse (not while orbiting with the right button).
	FVector RayOrigin, RayDirection;
	if (!IsInputKeyDown(EKeys::RightMouseButton) && DeprojectMousePositionToWorld(RayOrigin, RayDirection))
	{
		for (UAnatomyPickerComp* Picker : Pickers)
		{
			Picker->UpdateHover(RayOrigin, RayDirection); // an inactive picker clears its own hover
		}
	}

	// Click (press + release without dragging, since a left drag pans the camera) selects.
	FVector2D Mouse;
	if (GetMousePosition(Mouse.X, Mouse.Y))
	{
		if (WasInputKeyJustPressed(EKeys::LeftMouseButton))
		{
			LeftPressPosition = Mouse;
		}
		else if (WasInputKeyJustReleased(EKeys::LeftMouseButton) && FVector2D::Distance(Mouse, LeftPressPosition) <= ClickMaxDragPixels)
		{
			for (UAnatomyPickerComp* Picker : Pickers)
			{
				if (Picker->IsPickingActive())
				{
					Picker->SelectHovered();
				}
			}
		}
	}
}

void ACPP_PlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	ShowOnlyPossessedPawn(); // the first pawn from the game mode, and every switch
}

void ACPP_PlayerController::ShowOnlyPossessedPawn()
{
	const APawn* Current = GetPawn();
	if (!bShowOnlyPossessedPawn || !Current)
	{
		return;
	}
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		It->SetActorHiddenInGame(*It != Current);
	}

	// Static models standing in the level (not pawns, so the loop above doesn't see them).
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		const UStaticMeshComponent* MeshComponent = It->GetStaticMeshComponent();
		const UStaticMesh* Mesh = MeshComponent ? MeshComponent->GetStaticMesh() : nullptr;
		if (!Mesh || It->IsHidden())
		{
			continue;
		}
		const FString MeshPath = Mesh->GetPathName();
		if (StaticModelFolders.ContainsByPredicate([&MeshPath](const FString& Folder) { return !Folder.IsEmpty() && MeshPath.StartsWith(Folder); }))
		{
			It->SetActorHiddenInGame(true);
		}
	}
}

UCPP_MouseFunctionComp* ACPP_PlayerController::GetMouseFunctionComp() const
{
	const APawn* CurrentPawn = GetPawn();
	return CurrentPawn ? CurrentPawn->FindComponentByClass<UCPP_MouseFunctionComp>() : nullptr;
}

void ACPP_PlayerController::SwitchPawn(int32 Direction)
{
	const int32 Num = SwitchablePawnClasses.Num();
	if (Num > 0)
	{
		SwitchToPawnIndex(((CurrentPawnIndex + Direction) % Num + Num) % Num);
	}
}

void ACPP_PlayerController::SwitchToPawnIndex(int32 Index)
{
	if (!SwitchablePawnClasses.IsValidIndex(Index) || !SwitchablePawnClasses[Index])
	{
		return;
	}
	SwitchablePawns.SetNum(SwitchablePawnClasses.Num());

	APawn* Target = SwitchablePawns[Index];
	if (!IsValid(Target))
	{
		// Spawn at the matching placeholder, or next to the current pawn if there are none.
		FTransform SpawnTransform = GetPawn() ? GetPawn()->GetActorTransform() : FTransform::Identity;
		if (PC_PlayerPlaceHolders.Num() > 0)
		{
			if (const AActor* PlaceHolder = PC_PlayerPlaceHolders[Index % PC_PlayerPlaceHolders.Num()])
			{
				SpawnTransform = PlaceHolder->GetActorTransform();
			}
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Target = GetWorld()->SpawnActor<APawn>(SwitchablePawnClasses[Index], SpawnTransform, Params);
		SwitchablePawns[Index] = Target;
	}

	if (Target && Target != GetPawn())
	{
		// Possess fires OnPossessedPawnChanged -> every widget re-binds to the new pawn's components.
		Possess(Target);
		CurrentPawnIndex = Index;
	}
}

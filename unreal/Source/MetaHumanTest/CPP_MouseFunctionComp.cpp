#include "CPP_MouseFunctionComp.h"

#include "GameFramework/Actor.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

UCPP_MouseFunctionComp::UCPP_MouseFunctionComp()
{
	PrimaryComponentTick.bCanEverTick = false; // input-driven, nothing to do per frame
}

void UCPP_MouseFunctionComp::BeginPlay()
{
	Super::BeginPlay();
	SyncFromCurrentSpringArm();

	if (!SpringArmComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: CPP_MouseFunctionComp found no Spring Arm Component"), *GetNameSafe(GetOwner()));
	}
}

void UCPP_MouseFunctionComp::OrbitFromMouse(const FVector2D& MouseDelta)
{
	OrbitByDegrees(MouseDelta.X * YawSpeedDegPerPx, MouseDelta.Y * PitchSpeedDegPerPx);
}

void UCPP_MouseFunctionComp::OrbitByDegrees(float YawDegrees, float PitchDegrees)
{
	if (!SpringArmComponent)
	{
		return;
	}

	RememberStartViewIfNeeded();
	CancelViewMove();   // the user takes over from a scripted move

	CurrentArmRot.Yaw += YawDegrees;
	CurrentArmRot.Pitch = FMath::Clamp(CurrentArmRot.Pitch + PitchDegrees, MinPitch, MaxPitch);

	SpringArmComponent->SetRelativeRotation(CurrentArmRot);
}

void UCPP_MouseFunctionComp::ZoomCamera(float AxisValue)
{
	if (FMath::IsNearlyZero(AxisValue))
	{
		return;
	}

	// Positive AxisValue -> zoom in (shorter arm), negative -> zoom out (longer arm)
	SetCameraDistance(CurrentCameraDistance - AxisValue * ZoomSpeed);
}

void UCPP_MouseFunctionComp::SetCameraDistance(float NewDistance)
{
	if (!SpringArmComponent)
	{
		return;
	}

	RememberStartViewIfNeeded();
	CancelViewMove();   // the user takes over from a scripted move

	CurrentCameraDistance = FMath::Clamp(NewDistance, MinZoom, MaxZoom);
	SpringArmComponent->TargetArmLength = CurrentCameraDistance;
}

void UCPP_MouseFunctionComp::PanFromMouse(const FVector2D& MouseDelta)
{
	if (!SpringArmComponent)
	{
		return;
	}

	RememberStartViewIfNeeded();
	CancelViewMove();   // the user takes over from a scripted move

	// Use the camera rotation so the pan feels screen-space
	const FRotator CamRot = SpringArmComponent->GetComponentRotation();
	const FVector Right = FRotationMatrix(CamRot).GetScaledAxis(EAxis::Y);
	const FVector Up = FRotationMatrix(CamRot).GetScaledAxis(EAxis::Z);

	const FVector WorldDelta = (Right * MouseDelta.X + Up * MouseDelta.Y) * PanSpeed;
	SpringArmComponent->SetRelativeLocation(SpringArmComponent->GetRelativeLocation() + WorldDelta);
}

void UCPP_MouseFunctionComp::SyncFromCurrentSpringArm()
{
	if (!SpringArmComponent && GetOwner())
	{
		SpringArmComponent = GetOwner()->FindComponentByClass<USpringArmComponent>();
	}
	if (!SpringArmComponent)
	{
		return;
	}

	CurrentCameraDistance = SpringArmComponent->TargetArmLength;
	CurrentArmRot = SpringArmComponent->GetRelativeRotation();
}

void UCPP_MouseFunctionComp::RememberStartViewIfNeeded()
{
	if (bHasStartView || !SpringArmComponent)
	{
		return;
	}
	// Captured just before the first user move (not in BeginPlay), so anything that places the camera at start-up
	// (pawn Blueprint, level script) is part of the "start view".
	bHasStartView = true;
	StartArmRotation = SpringArmComponent->GetRelativeRotation();
	StartArmLocation = SpringArmComponent->GetRelativeLocation();
	StartArmLength = SpringArmComponent->TargetArmLength;
}

bool UCPP_MouseFunctionComp::IsViewChanged() const
{
	if (!bHasStartView || !SpringArmComponent)
	{
		return false;
	}
	return !SpringArmComponent->GetRelativeRotation().Equals(StartArmRotation, 0.5f)
		|| !SpringArmComponent->GetRelativeLocation().Equals(StartArmLocation, 0.5f)
		|| !FMath::IsNearlyEqual(SpringArmComponent->TargetArmLength, StartArmLength, 0.5f);
}

void UCPP_MouseFunctionComp::ResetView()
{
	if (!bHasStartView || !SpringArmComponent)
	{
		return;
	}
	CancelViewMove();
	UE_LOG(LogTemp, Log, TEXT("%s: camera reset to its start view (ResetView)"), *GetNameSafe(GetOwner()));
	SpringArmComponent->SetRelativeRotation(StartArmRotation);
	SpringArmComponent->SetRelativeLocation(StartArmLocation);
	SpringArmComponent->TargetArmLength = StartArmLength;
	SyncFromCurrentSpringArm();   // so the next orbit / zoom continues from the reset view
}

// ---------------------------------------------------------------------------
// Scripted camera moves
// ---------------------------------------------------------------------------

void UCPP_MouseFunctionComp::MoveViewTo(FVector WorldLookDirection, bool bSetTarget, FVector WorldTarget, float Distance, float Seconds)
{
	if (!SpringArmComponent)
	{
		return;
	}
	RememberStartViewIfNeeded();

	const USceneComponent* Parent = SpringArmComponent->GetAttachParent();

	// The spring arm's forward axis is the way the camera looks, so the look direction is its rotation.
	FRotator EndRotation = SpringArmComponent->GetRelativeRotation();
	if (!WorldLookDirection.IsNearlyZero())
	{
		const FQuat WorldRotation = WorldLookDirection.GetSafeNormal().ToOrientationQuat();
		EndRotation = (Parent && !SpringArmComponent->IsUsingAbsoluteRotation())
			? Parent->GetComponentTransform().InverseTransformRotation(WorldRotation).Rotator()
			: WorldRotation.Rotator();
		EndRotation.Pitch = FMath::Clamp(FRotator::NormalizeAxis(EndRotation.Pitch), MinPitch, MaxPitch);
		EndRotation.Roll = 0.f;
	}

	FVector EndLocation = SpringArmComponent->GetRelativeLocation();
	if (bSetTarget)
	{
		EndLocation = (Parent && !SpringArmComponent->IsUsingAbsoluteLocation())
			? Parent->GetComponentTransform().InverseTransformPosition(WorldTarget)
			: WorldTarget;
	}

	const float EndLength = Distance > 0.f ? FMath::Clamp(Distance, MinZoom, MaxZoom) : SpringArmComponent->TargetArmLength;
	BeginViewMove(EndRotation, EndLocation, EndLength, Seconds);
}

void UCPP_MouseFunctionComp::GlideToDistance(float NewDistance, float Seconds)
{
	if (!SpringArmComponent)
	{
		return;
	}
	RememberStartViewIfNeeded();
	// Keeps the rotation / target of a move that is already running (e.g. "show the biceps and zoom in").
	BeginViewMove(bViewMoving ? MoveEndRotation : SpringArmComponent->GetRelativeRotation(),
		bViewMoving ? MoveEndLocation : SpringArmComponent->GetRelativeLocation(),
		FMath::Clamp(NewDistance, MinZoom, MaxZoom), Seconds);
}

void UCPP_MouseFunctionComp::GlideOrbitBy(float YawDegrees, float PitchDegrees, float Seconds)
{
	if (!SpringArmComponent)
	{
		return;
	}
	RememberStartViewIfNeeded();
	FRotator EndRotation = bViewMoving ? MoveEndRotation : SpringArmComponent->GetRelativeRotation();
	EndRotation.Yaw += YawDegrees;
	EndRotation.Pitch = FMath::Clamp(EndRotation.Pitch + PitchDegrees, MinPitch, MaxPitch);
	BeginViewMove(EndRotation, bViewMoving ? MoveEndLocation : SpringArmComponent->GetRelativeLocation(),
		bViewMoving ? MoveEndLength : SpringArmComponent->TargetArmLength, Seconds);
}

void UCPP_MouseFunctionComp::GlideToStartView(float Seconds)
{
	if (!bHasStartView || !SpringArmComponent)
	{
		return;
	}
	BeginViewMove(StartArmRotation, StartArmLocation, StartArmLength, Seconds);
}

void UCPP_MouseFunctionComp::BeginViewMove(const FRotator& EndRotation, const FVector& EndLocation, float EndLength, float Seconds)
{
	UWorld* World = GetWorld();
	if (!SpringArmComponent || !World)
	{
		return;
	}
	MoveBeginRotation = SpringArmComponent->GetRelativeRotation();
	MoveBeginLocation = SpringArmComponent->GetRelativeLocation();
	MoveBeginLength = SpringArmComponent->TargetArmLength;
	MoveEndRotation = EndRotation;
	MoveEndLocation = EndLocation;
	MoveEndLength = EndLength;
	MoveSeconds = Seconds;
	MoveAlpha = 0.f;
	MoveStartTime = World->GetRealTimeSeconds();
	bViewMoving = true;

	if (Seconds <= 0.f)
	{
		MoveAlpha = 1.f;
		StepViewMove();   // jump there
		return;
	}
	// A timer instead of a component tick: this component does not tick (it is input-driven), and Blueprint pawns
	// made before this was added keep their saved tick settings.
	World->GetTimerManager().SetTimer(ViewMoveTimer, this, &UCPP_MouseFunctionComp::StepViewMove, 1.f / 120.f, /*bLoop*/ true);
}

void UCPP_MouseFunctionComp::StepViewMove()
{
	UWorld* World = GetWorld();
	if (!bViewMoving || !SpringArmComponent || !World)
	{
		CancelViewMove();
		return;
	}
	if (MoveSeconds > 0.f && MoveAlpha < 1.f)
	{
		// From the clock, not added up per call: a timer faster than the frame rate fires several times a frame.
		MoveAlpha = FMath::Clamp(static_cast<float>((World->GetRealTimeSeconds() - MoveStartTime) / MoveSeconds), 0.f, 1.f);
	}
	const float Eased = FMath::InterpEaseInOut(0.f, 1.f, MoveAlpha, 2.f);

	// The short way round for the rotation (179 -> -179 is 2 degrees, not 358).
	const FRotator Delta = (MoveEndRotation - MoveBeginRotation).GetNormalized();
	SpringArmComponent->SetRelativeRotation(MoveBeginRotation + Delta * Eased);
	SpringArmComponent->SetRelativeLocation(FMath::Lerp(MoveBeginLocation, MoveEndLocation, Eased));
	SpringArmComponent->TargetArmLength = FMath::Lerp(MoveBeginLength, MoveEndLength, Eased);
	SyncFromCurrentSpringArm();   // so a mouse orbit / zoom continues from here

	if (MoveAlpha >= 1.f)
	{
		bViewMoving = false;
		World->GetTimerManager().ClearTimer(ViewMoveTimer);
	}
}

void UCPP_MouseFunctionComp::CancelViewMove()
{
	if (!bViewMoving)
	{
		return;
	}
	bViewMoving = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ViewMoveTimer);
	}
	SyncFromCurrentSpringArm();
}

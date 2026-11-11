// Mouse camera control on the pawn: orbit (RMB), pan (LMB) and zoom (wheel) the pawn's spring arm.
// The player controller only routes the input here.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "CPP_MouseFunctionComp.generated.h"

class USpringArmComponent;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class METAHUMANTEST_API UCPP_MouseFunctionComp : public UActorComponent
{
	GENERATED_BODY()

public:
	UCPP_MouseFunctionComp();

	/** Rotate the camera around the character by a mouse delta (pixels). Callable from Blueprint / Python
	 *  (scripted camera moves, screenshots), the controller calls it while the right button is held. */
	UFUNCTION(BlueprintCallable, Category="Mouse")
	void OrbitFromMouse(const FVector2D& MouseDelta);

	/** Rotate the camera around the character by angles in degrees (touch drag, scripted moves). Pitch is clamped
	 *  to MinPitch / MaxPitch. */
	UFUNCTION(BlueprintCallable, Category="Mouse")
	void OrbitByDegrees(float YawDegrees, float PitchDegrees);

	/** Move the camera closer (+) / farther (-) by scroll steps. */
	UFUNCTION(BlueprintCallable, Category="Mouse")
	void ZoomCamera(float AxisValue);

	/** Put the camera at this distance (spring arm length), clamped to MinZoom / MaxZoom (touch pinch). */
	UFUNCTION(BlueprintCallable, Category="Mouse")
	void SetCameraDistance(float NewDistance);

	/** Slide the camera target by a mouse delta (pixels). */
	UFUNCTION(BlueprintCallable, Category="Mouse")
	void PanFromMouse(const FVector2D& MouseDelta);

	/** Re-read the spring arm's current rotation / length (call after moving it from somewhere else). */
	UFUNCTION(BlueprintCallable)
	void SyncFromCurrentSpringArm();

	/** Put the camera back where it was before the first orbit / pan / zoom (the IHPC HUD's round "reset view"
	 *  button, Figma FINAL FESTIVAL frame 17). Does nothing if the view was never moved. */
	UFUNCTION(BlueprintCallable, Category="Mouse")
	void ResetView();

	/** True once the user has orbited / panned / zoomed away from the starting view. */
	UFUNCTION(BlueprintPure, Category="Mouse")
	bool IsViewChanged() const;

	// ---------------- Scripted camera moves (the assistant: "show me the biceps", "look from behind") ----------------

	/** Glide the camera to a new view over Seconds (0 = at once).
	 *  WorldLookDirection: the way the camera should look, in world space (from the camera towards the avatar);
	 *                      a zero vector keeps the current direction.
	 *  bSetTarget / WorldTarget: also move the point the camera orbits to this world position (false = keep it).
	 *  Distance: camera distance in cm, clamped to MinZoom / MaxZoom (<= 0 = keep it).
	 *  Any mouse / touch orbit, pan or zoom during the glide stops it where it is. */
	UFUNCTION(BlueprintCallable, Category="Mouse")
	void MoveViewTo(FVector WorldLookDirection, bool bSetTarget, FVector WorldTarget, float Distance, float Seconds = 0.6f);

	/** Glide the camera closer / farther (clamped to MinZoom / MaxZoom). */
	UFUNCTION(BlueprintCallable, Category="Mouse")
	void GlideToDistance(float NewDistance, float Seconds = 0.35f);

	/** Glide the camera around the avatar by these angles (degrees; pitch is clamped to MinPitch / MaxPitch). */
	UFUNCTION(BlueprintCallable, Category="Mouse")
	void GlideOrbitBy(float YawDegrees, float PitchDegrees, float Seconds = 0.5f);

	/** Glide back to the start view (same as ResetView, but not a jump). */
	UFUNCTION(BlueprintCallable, Category="Mouse")
	void GlideToStartView(float Seconds = 0.6f);

	/** True while a scripted move is running. */
	UFUNCTION(BlueprintPure, Category="Mouse")
	bool IsViewMoving() const { return bViewMoving; }

	/** Where the camera is heading (or is, when no scripted move runs), cm. */
	UFUNCTION(BlueprintPure, Category="Mouse")
	float GetTargetCameraDistance() const { return bViewMoving ? MoveEndLength : CurrentCameraDistance; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mouse")
	TObjectPtr<USpringArmComponent> SpringArmComponent = nullptr;

	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Mouse Speed")
	float YawSpeedDegPerPx = 1.f;

	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Mouse Speed")
	float PitchSpeedDegPerPx = 1.f;

	/** Orbit pitch limits, so the camera can't flip over the top or under the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mouse Speed")
	float MinPitch = -80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mouse Speed")
	float MaxPitch = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MetaHuman")
	TArray<TObjectPtr<AActor>> PlayerPlaceHolders;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pan")
	float PanSpeed = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zoom")
	float ZoomSpeed = 50.f;  // how fast we zoom per scroll step

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zoom")
	float MinZoom = 50.f;   // closest

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zoom")
	float MaxZoom = 1200.f;  // farthest

	/** Current spring arm length we're using */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Zoom")
	float CurrentCameraDistance = 0.f;

	FRotator CurrentArmRot;

protected:
	virtual void BeginPlay() override;

private:
	/** Remembers the spring arm's view the first time the user moves it (the view ResetView goes back to). */
	void RememberStartViewIfNeeded();

	bool bHasStartView = false;
	FRotator StartArmRotation = FRotator::ZeroRotator;
	FVector StartArmLocation = FVector::ZeroVector;
	float StartArmLength = 0.f;

	// Scripted move (MoveViewTo and friends): from the Move Begin values to the Move End values, on a timer.
	void BeginViewMove(const FRotator& EndRotation, const FVector& EndLocation, float EndLength, float Seconds);
	void StepViewMove();
	/** Stops a scripted move where it is (called by every user-driven orbit / pan / zoom). */
	void CancelViewMove();

	bool bViewMoving = false;
	float MoveAlpha = 0.f;
	float MoveSeconds = 0.f;
	double MoveStartTime = 0.0;
	FRotator MoveBeginRotation = FRotator::ZeroRotator, MoveEndRotation = FRotator::ZeroRotator;
	FVector MoveBeginLocation = FVector::ZeroVector, MoveEndLocation = FVector::ZeroVector;
	float MoveBeginLength = 0.f, MoveEndLength = 0.f;
	FTimerHandle ViewMoveTimer;
};

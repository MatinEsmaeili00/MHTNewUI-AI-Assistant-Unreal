// IHPCAssistantActions.cpp - what the assistant can DO: one function per action type of docs/PROTOCOL.md.
// Every function returns whether it worked and, if not, one sentence for the user. Nothing here talks to the
// server; "Assistant.Do <type> key=value ..." in the console runs the same code.

#include "Assistant/IHPCAssistantComp.h"

#include "../../CPP_MouseFunctionComp.h"    // module root (not on the include path)
#include "../../CPP_PlayerController.h"
#include "AnatomyPickerComp.h"
#include "AnatomyViewComp.h"
#include "AnimationTimelineComp.h"
#include "IHPC_UI/CPP_UW_IHPCActionsPanel.h"
#include "IHPC_UI/CPP_UW_IHPCHud.h"
#include "IHPC_UI/CPP_UW_IHPCLayerSwitcher.h"
#include "IHPC_UI/CPP_UW_IHPCSideBar.h"
#include "IHPC_UI/CPP_UW_IHPCTimeline.h"
#include "IHPC_UI/IHPCUILibrary.h"
#include "MuscleAtlas.h"
#include "PoseEstimation/PoseEstimationComp.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "GameFramework/Pawn.h"

namespace
{
	FIHPCActionResult Ok(const TCHAR* Type)
	{
		FIHPCActionResult Result;
		Result.Type = Type;
		Result.bOk = true;
		return Result;
	}

	FIHPCActionResult Fail(const TCHAR* Type, const FString& Message)
	{
		FIHPCActionResult Result;
		Result.Type = Type;
		Result.bOk = false;
		Result.Message = Message;
		return Result;
	}

	// The answer comes from a language model: a number may arrive as "2", a flag as "true". Read them all the same.

	FString GetText(const FJsonObject& Object, const TCHAR* Field)
	{
		FString Value;
		Object.TryGetStringField(Field, Value);
		return Value.TrimStartAndEnd();
	}

	bool GetNumber(const FJsonObject& Object, const TCHAR* Field, double& OutValue)
	{
		if (Object.TryGetNumberField(Field, OutValue))
		{
			return true;
		}
		FString Text;
		if (Object.TryGetStringField(Field, Text) && Text.IsNumeric())
		{
			OutValue = FCString::Atod(*Text);
			return true;
		}
		return false;
	}

	double GetNumberOr(const FJsonObject& Object, const TCHAR* Field, double Default)
	{
		double Value = Default;
		GetNumber(Object, Field, Value);
		return Value;
	}

	bool GetFlag(const FJsonObject& Object, const TCHAR* Field, bool& OutValue)
	{
		if (Object.TryGetBoolField(Field, OutValue))
		{
			return true;
		}
		FString Text;
		if (Object.TryGetStringField(Field, Text))
		{
			OutValue = Text.ToBool();
			return true;
		}
		double Number = 0.0;
		if (Object.TryGetNumberField(Field, Number))
		{
			OutValue = Number != 0.0;
			return true;
		}
		return false;
	}

	FString FormatSpeed(float Speed)
	{
		return FString::Printf(TEXT("x%s"), *FString::SanitizeFloat(Speed, 0));
	}

	const TCHAR* Degree = TEXT("°");
}

FIHPCActionResult UIHPCAssistantComp::RunAction(const TSharedPtr<FJsonObject>& Action)
{
	if (!Action.IsValid())
	{
		return Fail(TEXT("?"), TEXT("That action was not readable."));
	}
	const FString Type = GetText(*Action, TEXT("type")).ToLower();

	FIHPCActionResult Result;
	if (Type == TEXT("set_layer"))            { Result = DoSetLayer(*Action); }
	else if (Type == TEXT("timeline"))        { Result = DoTimeline(*Action); }
	else if (Type == TEXT("camera"))          { Result = DoCamera(*Action); }
	else if (Type == TEXT("pose_estimation")) { Result = DoPoseEstimation(*Action); }
	else if (Type == TEXT("read_pose_data"))  { Result = DoReadPoseData(*Action); }
	else if (Type == TEXT("show_part"))       { Result = DoShowPart(*Action); }
	else if (Type == TEXT("clear_selection")) { Result = DoClearSelection(*Action); }
	else if (Type == TEXT("play_action"))     { Result = DoPlayAction(*Action); }
	else if (Type == TEXT("open_panel"))      { Result = DoOpenPanel(*Action); }
	else if (Type == TEXT("switch_avatar"))   { Result = DoSwitchAvatar(*Action); }
	else
	{
		// A newer server may know actions this build does not: skip them, do the rest.
		Result = Fail(*Type, FString::Printf(TEXT("This version of the app does not know the action \"%s\"."), *Type));
	}

	UE_LOG(LogIHPCAssistant, Log, TEXT("Action %s -> %s%s%s"), *Type, Result.bOk ? TEXT("ok") : TEXT("FAILED"),
		Result.Message.IsEmpty() ? TEXT("") : TEXT(": "), *Result.Message);
	return Result;
}

UCPP_MouseFunctionComp* UIHPCAssistantComp::GetCamera() const
{
	const APawn* Avatar = GetAvatar();
	return Avatar ? Avatar->FindComponentByClass<UCPP_MouseFunctionComp>() : nullptr;
}

// ---------------------------------------------------------------------------
// set_layer
// ---------------------------------------------------------------------------

FIHPCActionResult UIHPCAssistantComp::DoSetLayer(const FJsonObject& Action)
{
	const TCHAR* Type = TEXT("set_layer");
	const FString Layer = GetText(Action, TEXT("layer")).ToLower();

	EIHPCAnatomyLayer Wanted;
	if (Layer == TEXT("body") || Layer == TEXT("skin"))                                   { Wanted = EIHPCAnatomyLayer::Body; }
	else if (Layer == TEXT("muscles") || Layer == TEXT("muscle"))                         { Wanted = EIHPCAnatomyLayer::Muscles; }
	else if (Layer == TEXT("skeleton") || Layer == TEXT("bones") || Layer == TEXT("bone")) { Wanted = EIHPCAnatomyLayer::Skeleton; }
	else
	{
		return Fail(Type, FString::Printf(TEXT("I don't know a layer called \"%s\". The layers are body, muscles and skeleton."), *Layer));
	}

	// The side bar's three buttons do the work (and show which layer is on).
	UCPP_UW_IHPCLayerSwitcher* Layers = GetHud() ? GetHud()->GetLayerSwitcher() : nullptr;
	if (!Layers || !Layers->CanDriveAvatarLayers())
	{
		return Fail(Type, TEXT("This avatar has no anatomy layers."));
	}
	Layers->SelectLayer(Wanted);
	return Ok(Type);
}

// ---------------------------------------------------------------------------
// timeline
// ---------------------------------------------------------------------------

FIHPCActionResult UIHPCAssistantComp::DoTimeline(const FJsonObject& Action)
{
	const TCHAR* Type = TEXT("timeline");
	const FString Op = GetText(Action, TEXT("op")).ToLower();

	UAnimationTimelineComp* Animation = UIHPCUILibrary::FindAvatarTimeline(this);
	if (!Animation)
	{
		return Fail(Type, TEXT("This avatar has no animation to control."));
	}
	// The bar at the bottom of the screen: going through it keeps its speed label and its markers right.
	UCPP_UW_IHPCTimeline* Bar = GetHud() ? GetHud()->GetTimeline() : nullptr;

	if (Op == TEXT("play") || Op == TEXT("resume"))
	{
		UIHPCUILibrary::PlayAvatar(this, /*bRestartIfAtEnd*/ true);
		return Ok(Type);
	}
	if (Op == TEXT("pause") || Op == TEXT("stop"))
	{
		Animation->Pause();
		return Ok(Type);
	}
	if (Op == TEXT("toggle"))
	{
		if (Animation->IsPlaying())
		{
			Animation->Pause();
		}
		else
		{
			UIHPCUILibrary::PlayAvatar(this, true);
		}
		return Ok(Type);
	}
	if (Op == TEXT("restart"))
	{
		UIHPCUILibrary::SeekAvatarTimeline(this, 0.f, /*bPauseAfterSeek*/ false);
		return Ok(Type);
	}
	if (Op == TEXT("faster") || Op == TEXT("slower"))
	{
		const bool bFaster = Op == TEXT("faster");
		const int32 Steps = FMath::Clamp(FMath::RoundToInt32(GetNumberOr(Action, TEXT("value"), 1.0)), 1, 10);
		const float Before = Animation->PlaySpeed;
		if (Bar)
		{
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				bFaster ? Bar->SpeedUp() : Bar->SlowDown();
			}
		}
		else
		{
			UIHPCUILibrary::SetAvatarPlaySpeed(this, FMath::Clamp(Before * FMath::Pow(bFaster ? 1.5f : 1.f / 1.5f, Steps), 0.1f, 4.f));
		}
		if (FMath::IsNearlyEqual(Animation->PlaySpeed, Before, 0.001f))
		{
			return Fail(Type, FString::Printf(TEXT("It is already at the %s speed (%s)."), bFaster ? TEXT("fastest") : TEXT("slowest"), *FormatSpeed(Before)));
		}
		return Ok(Type);
	}
	if (Op == TEXT("set_speed"))
	{
		double Wanted = 0.0;
		if (!GetNumber(Action, TEXT("value"), Wanted) || Wanted <= 0.0)
		{
			return Fail(Type, TEXT("I need a speed, for example 0.5 or 2."));
		}
		if (Bar && Bar->SpeedSteps.Num() > 0)
		{
			// The bar steps through fixed speeds: take the nearest one.
			int32 Best = 0;
			for (int32 Index = 1; Index < Bar->SpeedSteps.Num(); ++Index)
			{
				if (FMath::Abs(Bar->SpeedSteps[Index] - Wanted) < FMath::Abs(Bar->SpeedSteps[Best] - Wanted))
				{
					Best = Index;
				}
			}
			Bar->SetSpeedIndex(Best);
		}
		else
		{
			UIHPCUILibrary::SetAvatarPlaySpeed(this, static_cast<float>(FMath::Clamp(Wanted, 0.1, 4.0)));
		}
		return Ok(Type);
	}
	if (Op == TEXT("seek"))
	{
		double Where = 0.0;
		if (!GetNumber(Action, TEXT("value"), Where))
		{
			return Fail(Type, TEXT("I need a position between 0 and 1."));
		}
		if (Where > 1.0 && Where <= 100.0)
		{
			Where /= 100.0;   // "50" was meant as a percentage
		}
		// Not quite 1: a looping animation wraps, so exactly 1 is the start again and "go to the end" would rewind.
		UIHPCUILibrary::SeekAvatarTimeline(this, static_cast<float>(FMath::Clamp(Where, 0.0, 0.999)), /*bPauseAfterSeek*/ false);
		return Ok(Type);
	}
	if (Op == TEXT("step"))
	{
		const double Seconds = GetNumberOr(Action, TEXT("value"), 1.0 / 30.0);
		Animation->StepSeconds(static_cast<float>(FMath::Clamp(Seconds, -10.0, 10.0)));
		return Ok(Type);
	}

	// The rest only exists on the bar.
	if (!Bar)
	{
		return Fail(Type, TEXT("The timeline bar is not on screen."));
	}
	if (Op == TEXT("next_marker"))
	{
		Bar->FastForwardToNextMarker();
		return Ok(Type);
	}
	if (Op == TEXT("bookmark"))
	{
		Bar->AddBookmark();
		return Ok(Type);
	}
	if (Op == TEXT("record_start"))
	{
		if (Bar->IsRecording())
		{
			return Fail(Type, TEXT("It is already recording."));
		}
		Bar->ToggleRecording();
		return Ok(Type);
	}
	if (Op == TEXT("record_stop"))
	{
		if (!Bar->IsRecording())
		{
			return Fail(Type, TEXT("Nothing is being recorded."));
		}
		Bar->ToggleRecording();
		return Ok(Type);
	}
	return Fail(Type, FString::Printf(TEXT("I don't know the timeline command \"%s\"."), *Op));
}

// ---------------------------------------------------------------------------
// camera
// ---------------------------------------------------------------------------

namespace
{
	const FName BoneLeftArm(TEXT("upperarm_l")), BoneRightArm(TEXT("upperarm_r")), BonePelvis(TEXT("pelvis"));

	/** The avatar's body mesh: the skeletal mesh with the MetaHuman arm bones, "Body" if there are several. */
	const USkeletalMeshComponent* FindBodyMesh(const APawn* Avatar)
	{
		const USkeletalMeshComponent* Body = nullptr;
		if (Avatar)
		{
			TArray<USkeletalMeshComponent*> Meshes;
			Avatar->GetComponents<USkeletalMeshComponent>(Meshes);
			for (const USkeletalMeshComponent* Mesh : Meshes)
			{
				if (Mesh && Mesh->GetBoneIndex(BoneLeftArm) != INDEX_NONE && Mesh->GetBoneIndex(BoneRightArm) != INDEX_NONE)
				{
					// The mesh that runs the animation (not a Leader Pose follower), "Body" on the MetaHumans.
					if (!Body || Mesh->GetFName() == TEXT("Body"))
					{
						Body = Mesh;
					}
				}
			}
		}
		return Body;
	}

	/**
	 * Which side to look at a muscle or bone from (a horizontal direction pointing from the part towards the camera).
	 *
	 * A muscle lies on one side of the limb it belongs to: the biceps in front of the upper arm, the hamstrings behind
	 * the thigh, the pectoral in front of the trunk. So: find the nearest limb (a line between two joints) and look
	 * from the side of it the part is on. A part on the inside of a limb (the adductors on the inner thigh) would put
	 * the camera on the far side of the body with the other leg in the way; those are looked at from the front or the
	 * back instead. A part that sits on the limb's own line (the bones) has no side: front, turned a little towards
	 * the side of the body it is on.
	 */
	FVector PickViewSide(const USkeletalMeshComponent* Body, const FVector& Centre, const FVector& Origin, const FVector& Forward, const FVector& Right)
	{
		// Which side of the body the part is on (zero near the middle: the spine, the breastbone).
		const float Across = FVector::DotProduct(Centre - Origin, Right);
		const FVector Outside = FMath::Abs(Across) > 4.f ? Right * FMath::Sign(Across) : FVector::ZeroVector;
		const FVector FrontQuarter = (Forward + Outside * 0.6f).GetSafeNormal2D();
		if (!Body)
		{
			return FrontQuarter;
		}

		// The limbs, as lines between joints (MetaHuman bone names).
		static const TCHAR* Segments[][2] = {
			{ TEXT("pelvis"), TEXT("neck_01") }, { TEXT("neck_01"), TEXT("head") },
			{ TEXT("upperarm_l"), TEXT("lowerarm_l") }, { TEXT("lowerarm_l"), TEXT("hand_l") },
			{ TEXT("upperarm_r"), TEXT("lowerarm_r") }, { TEXT("lowerarm_r"), TEXT("hand_r") },
			{ TEXT("thigh_l"), TEXT("calf_l") }, { TEXT("calf_l"), TEXT("foot_l") }, { TEXT("foot_l"), TEXT("ball_l") },
			{ TEXT("thigh_r"), TEXT("calf_r") }, { TEXT("calf_r"), TEXT("foot_r") }, { TEXT("foot_r"), TEXT("ball_r") },
		};
		FVector Offset = FVector::ZeroVector;   // from the nearest limb line to the part
		float BestDistance2 = TNumericLimits<float>::Max();
		for (const auto& Segment : Segments)
		{
			const FName From(Segment[0]), To(Segment[1]);
			if (Body->GetBoneIndex(From) == INDEX_NONE || Body->GetBoneIndex(To) == INDEX_NONE)
			{
				continue;
			}
			const FVector OnLine = FMath::ClosestPointOnSegment(Centre, Body->GetBoneLocation(From), Body->GetBoneLocation(To));
			const float Distance2 = FVector::DistSquared(Centre, OnLine);
			if (Distance2 < BestDistance2)
			{
				BestDistance2 = Distance2;
				Offset = Centre - OnLine;
			}
		}
		if (Offset.Size2D() < 1.5f)
		{
			return FrontQuarter;   // on the line itself: a bone
		}

		const FVector Side = Offset.GetSafeNormal2D();
		if (!Outside.IsZero() && FVector::DotProduct(Side, Outside) < -0.35f)
		{
			// It faces the middle of the body: from the front or the back, whichever it leans to.
			const float Sign = FVector::DotProduct(Side, Forward) < -0.3f ? -1.f : 1.f;
			return (Forward * Sign + Outside * 0.35f).GetSafeNormal2D();
		}
		return Side;
	}
}

bool UIHPCAssistantComp::GetAvatarFrame(FVector& OutOrigin, FVector& OutForward, FVector& OutRight) const
{
	const APawn* Avatar = GetAvatar();
	if (!Avatar)
	{
		return false;
	}
	OutOrigin = Avatar->GetActorLocation();
	OutForward = Avatar->GetActorForwardVector();
	OutRight = Avatar->GetActorRightVector();

	// The body mesh knows better than the actor: the avatars are plain pawns whose mesh may face any way. The
	// two upper arms give the avatar's right, and right x up = forward.
	const FName LeftArm = BoneLeftArm, RightArm = BoneRightArm, Pelvis = BonePelvis;
	const USkeletalMeshComponent* Body = FindBodyMesh(Avatar);
	if (Body)
	{
		const FVector Across = (Body->GetBoneLocation(RightArm) - Body->GetBoneLocation(LeftArm)).GetSafeNormal2D();
		if (!Across.IsNearlyZero())
		{
			OutRight = Across;
			OutForward = FVector::CrossProduct(Across, FVector::UpVector).GetSafeNormal();
		}
		if (Body->GetBoneIndex(Pelvis) != INDEX_NONE)
		{
			OutOrigin = Body->GetBoneLocation(Pelvis);
		}
	}
	return true;
}

FIHPCActionResult UIHPCAssistantComp::DoCamera(const FJsonObject& Action)
{
	const TCHAR* Type = TEXT("camera");
	const FString Op = GetText(Action, TEXT("op")).ToLower();

	UCPP_MouseFunctionComp* Camera = GetCamera();
	if (!Camera)
	{
		return Fail(Type, TEXT("This avatar's camera can't be moved."));
	}
	const float Distance = Camera->GetTargetCameraDistance();

	if (Op == TEXT("zoom_in") || Op == TEXT("zoom_out"))
	{
		const bool bIn = Op == TEXT("zoom_in");
		const int32 Steps = FMath::Clamp(FMath::RoundToInt32(GetNumberOr(Action, TEXT("value"), 1.0)), 1, 12);
		// One step: a quarter closer, or the same step back out.
		const float Wanted = FMath::Clamp(Distance * FMath::Pow(bIn ? 0.75f : 1.f / 0.75f, Steps), Camera->MinZoom, Camera->MaxZoom);
		if (FMath::IsNearlyEqual(Wanted, Distance, 0.5f))
		{
			return Fail(Type, bIn ? TEXT("The camera is already as close as it goes.") : TEXT("The camera is already as far away as it goes."));
		}
		Camera->GlideToDistance(Wanted, CameraMoveSeconds * 0.6f);
		return Ok(Type);
	}
	if (Op == TEXT("zoom_to"))
	{
		double Amount = 0.0;
		if (!GetNumber(Action, TEXT("value"), Amount))
		{
			return Fail(Type, TEXT("I need a zoom amount between 0 (far) and 1 (close)."));
		}
		Camera->GlideToDistance(FMath::Lerp(Camera->MaxZoom, Camera->MinZoom, static_cast<float>(FMath::Clamp(Amount, 0.0, 1.0))), CameraMoveSeconds);
		return Ok(Type);
	}
	if (Op == TEXT("orbit"))
	{
		const float Yaw = static_cast<float>(FMath::Clamp(GetNumberOr(Action, TEXT("yaw"), 0.0), -360.0, 360.0));
		const float Pitch = static_cast<float>(FMath::Clamp(GetNumberOr(Action, TEXT("pitch"), 0.0), -90.0, 90.0));
		if (FMath::IsNearlyZero(Yaw) && FMath::IsNearlyZero(Pitch))
		{
			return Fail(Type, TEXT("I need to know how far to turn the camera."));
		}
		// The protocol counts the camera's own movement (+yaw = it moves to its right, +pitch = up); the spring arm
		// turns the other way for that.
		Camera->GlideOrbitBy(-Yaw, -Pitch, CameraMoveSeconds);
		return Ok(Type);
	}
	if (Op == TEXT("view"))
	{
		const FString Side = GetText(Action, TEXT("value")).ToLower();
		FVector Origin, Forward, Right;
		if (!GetAvatarFrame(Origin, Forward, Right))
		{
			return Fail(Type, TEXT("There is no avatar to look at."));
		}
		// The way the camera looks: from that side towards the avatar, a little downwards.
		FVector Look;
		if (Side == TEXT("front"))                               { Look = -Forward; }
		else if (Side == TEXT("back") || Side == TEXT("behind")) { Look = Forward; }
		else if (Side == TEXT("left"))                           { Look = Right; }
		else if (Side == TEXT("right"))                          { Look = -Right; }
		else if (Side == TEXT("top") || Side == TEXT("above"))   { Look = -Forward * 0.15f - FVector::UpVector; }
		else
		{
			return Fail(Type, FString::Printf(TEXT("I don't know the view \"%s\". Try front, back, left, right or top."), *Side));
		}
		if (Side != TEXT("top") && Side != TEXT("above"))
		{
			Look.Z = -0.1f;
		}
		Camera->MoveViewTo(Look, /*bSetTarget*/ false, FVector::ZeroVector, /*Distance*/ 0.f, CameraMoveSeconds);
		return Ok(Type);
	}
	if (Op == TEXT("reset"))
	{
		Camera->GlideToStartView(CameraMoveSeconds);   // does nothing if the view was never moved: that is fine
		return Ok(Type);
	}
	return Fail(Type, FString::Printf(TEXT("I don't know the camera command \"%s\"."), *Op));
}

// ---------------------------------------------------------------------------
// pose_estimation, read_pose_data
// ---------------------------------------------------------------------------

FIHPCActionResult UIHPCAssistantComp::DoPoseEstimation(const FJsonObject& Action)
{
	const TCHAR* Type = TEXT("pose_estimation");
	const ACPP_PlayerController* Controller = Cast<ACPP_PlayerController>(GetOwner());
	UPoseEstimationComp* Pose = Controller ? Controller->GetPoseEstimation() : nullptr;
	if (!Pose)
	{
		return Fail(Type, TEXT("Pose estimation is not part of this build."));
	}

	bool bEnabled = false, bOverlay = true, bShowPanel = false;
	const bool bHasEnabled = GetFlag(Action, TEXT("enabled"), bEnabled);
	const bool bHasOverlay = GetFlag(Action, TEXT("overlay"), bOverlay);
	const bool bHasPanel = GetFlag(Action, TEXT("show_panel"), bShowPanel);

	if (!bHasEnabled)
	{
		// Only the overlay / only the panel.
		if (!bHasOverlay && !bHasPanel)
		{
			return Fail(Type, TEXT("I need to know whether to switch pose estimation on or off."));
		}
		if (bHasOverlay)
		{
			Pose->SetOverlayVisible(bOverlay);
		}
		if (bHasPanel)
		{
			Pose->SetDataPanelOpen(bShowPanel);
		}
		return Ok(Type);
	}

	if (bEnabled)
	{
		Pose->SetEstimationEnabled(true);
		Pose->SetOverlayVisible(bHasOverlay ? bOverlay : true);
		if (!bHasPanel || bShowPanel)
		{
			Pose->SetDataPanelOpen(true);   // the Pose Estimation side panel: the live graph and the angles
		}
		if (Pose->GetStatus() == EPoseEstimationStatus::Error)
		{
			return Fail(Type, FString::Printf(TEXT("Pose estimation could not start: %s"), *Pose->GetLastError()));
		}
	}
	else
	{
		Pose->SetEstimationEnabled(false);
		// The analysis keeps running while its panel is open, so "off" closes the panel too unless told not to.
		if (Pose->IsDataPanelOpen() && (!bHasPanel || !bShowPanel))
		{
			Pose->SetDataPanelOpen(false);
		}
		bPoseReadPending = false;
	}
	return Ok(Type);
}

bool UIHPCAssistantComp::HasFreshPose() const
{
	const ACPP_PlayerController* Controller = Cast<ACPP_PlayerController>(GetOwner());
	const UPoseEstimationComp* Pose = Controller ? Controller->GetPoseEstimation() : nullptr;
	// "Fresh": a frame was analysed in the last second and a half (the result of an earlier run does not count).
	return Pose && Pose->IsRunning() && LastPoseResultTime > 0.0 && FPlatformTime::Seconds() - LastPoseResultTime < 1.5;
}

FString UIHPCAssistantComp::FormatPoseData(const TArray<FString>& OnlyTheseAngles) const
{
	const ACPP_PlayerController* Controller = Cast<ACPP_PlayerController>(GetOwner());
	const UPoseEstimationComp* Pose = Controller ? Controller->GetPoseEstimation() : nullptr;
	const FPosePerson* Person = (Pose && HasFreshPose()) ? Pose->GetLatestResult().GetPrimary() : nullptr;
	if (!Person)
	{
		return FString();
	}

	auto FindAngle = [Person](const FString& Name) -> const FPoseAngle*
	{
		const FName Wanted(*Name.ToLower());
		return Person->Angles.FindByPredicate([Wanted](const FPoseAngle& Angle) { return Angle.Name == Wanted; });
	};
	auto Number = [](const FPoseAngle* Angle)
	{
		return FString::Printf(TEXT("%d%s"), FMath::RoundToInt32(Angle->Degrees), Degree);
	};

	TArray<FString> Lines;
	if (OnlyTheseAngles.Num() > 0)
	{
		// Exactly what was asked for.
		for (const FString& Name : OnlyTheseAngles)
		{
			const FPoseAngle* Angle = FindAngle(Name);
			if (Angle && Angle->bValid)
			{
				Lines.Add(FString::Printf(TEXT("%s: %s"), *Name.ToLower(), *Number(Angle)));
			}
			else if (Angle)
			{
				Lines.Add(FString::Printf(TEXT("%s: not visible right now"), *Name.ToLower()));
			}
			else
			{
				Lines.Add(FString::Printf(TEXT("%s: not an angle I measure"), *Name.ToLower()));
			}
		}
	}
	else
	{
		// The main joints, right and left on one line.
		static const TCHAR* Joints[] = { TEXT("knee"), TEXT("hip"), TEXT("ankle"), TEXT("shoulder"), TEXT("elbow") };
		for (const TCHAR* Joint : Joints)
		{
			const FPoseAngle* RightAngle = FindAngle(FString::Printf(TEXT("right %s"), Joint));
			const FPoseAngle* LeftAngle = FindAngle(FString::Printf(TEXT("left %s"), Joint));
			TArray<FString> Sides;
			if (RightAngle && RightAngle->bValid)
			{
				Sides.Add(TEXT("right ") + Number(RightAngle));
			}
			if (LeftAngle && LeftAngle->bValid)
			{
				Sides.Add(TEXT("left ") + Number(LeftAngle));
			}
			if (Sides.Num() > 0)
			{
				FString Label(Joint);
				Label[0] = FChar::ToUpper(Label[0]);
				Lines.Add(FString::Printf(TEXT("%s: %s"), *Label, *FString::Join(Sides, TEXT(", "))));
			}
		}
		const FPoseAngle* Trunk = FindAngle(TEXT("trunk"));
		if (Trunk && Trunk->bValid)
		{
			Lines.Add(FString::Printf(TEXT("Trunk: %s"), *Number(Trunk)));
		}
	}
	if (Lines.Num() == 0)
	{
		return FString();
	}
	// The rate is an average over the last two seconds: it reads 0 for the first frames after switching on.
	const float Rate = Pose->GetAnalysesPerSecond();
	const FString Header = Rate >= 1.f ? FString::Printf(TEXT("Pose data now (%.0f frames per second):"), Rate) : FString(TEXT("Pose data now:"));
	return Header + TEXT("\n") + FString::Join(Lines, TEXT("\n"));
}

FIHPCActionResult UIHPCAssistantComp::DoReadPoseData(const FJsonObject& Action)
{
	const TCHAR* Type = TEXT("read_pose_data");
	const ACPP_PlayerController* Controller = Cast<ACPP_PlayerController>(GetOwner());
	const UPoseEstimationComp* Pose = Controller ? Controller->GetPoseEstimation() : nullptr;
	if (!Pose)
	{
		return Fail(Type, TEXT("Pose estimation is not part of this build."));
	}
	if (!Pose->IsRunning())
	{
		return Fail(Type, TEXT("Pose estimation is off. Ask me to start it first."));
	}

	TArray<FString> Angles;
	const TArray<TSharedPtr<FJsonValue>>* AngleValues = nullptr;
	if (Action.TryGetArrayField(TEXT("angles"), AngleValues) && AngleValues)
	{
		for (const TSharedPtr<FJsonValue>& Value : *AngleValues)
		{
			FString Name;
			if (Value.IsValid() && Value->TryGetString(Name) && !Name.TrimStartAndEnd().IsEmpty())
			{
				Angles.Add(Name.TrimStartAndEnd());
			}
		}
	}

	const FString Text = FormatPoseData(Angles);
	if (!Text.IsEmpty())
	{
		Say(Text, /*bOpenChat*/ true);
		return Ok(Type);
	}

	// Just switched on (the models take a second or two to load), or nobody in view yet: answer when a frame arrives.
	bPoseReadPending = true;
	PoseReadDeadline = FPlatformTime::Seconds() + PoseReadTimeout;
	PoseReadAngles = MoveTemp(Angles);
	return Ok(Type);
}

// ---------------------------------------------------------------------------
// show_part, clear_selection
// ---------------------------------------------------------------------------

const FIHPCPartRef* UIHPCAssistantComp::FindPart(const FString& Kind, const FString& Name, const FString& Side) const
{
	const FString Key = NormalizeName(Name);
	if (Key.IsEmpty())
	{
		return nullptr;
	}
	auto KindFits = [&Kind](const FIHPCPartRef& Part) { return Kind.IsEmpty() || Part.Kind == Kind; };

	// 1. The name as the atlas has it. 2. A name that contains what was asked ("biceps" -> "Biceps brachii"),
	//    the shortest such name first, so "deltoid" does not become a longer muscle that merely mentions it.
	FString MatchedKey, MatchedKind;
	for (const FIHPCPartRef& Part : Parts)
	{
		if (KindFits(Part) && Part.Key == Key)
		{
			MatchedKey = Part.Key;
			MatchedKind = Part.Kind;
			break;
		}
	}
	if (MatchedKey.IsEmpty())
	{
		for (const FIHPCPartRef& Part : Parts)
		{
			if (KindFits(Part) && Part.Key.Contains(Key) && (MatchedKey.IsEmpty() || Part.Key.Len() < MatchedKey.Len()))
			{
				MatchedKey = Part.Key;
				MatchedKind = Part.Kind;
			}
		}
	}
	if (MatchedKey.IsEmpty())
	{
		return nullptr;
	}

	// Every side of that name.
	TArray<const FIHPCPartRef*> Candidates;
	for (const FIHPCPartRef& Part : Parts)
	{
		if (Part.Kind == MatchedKind && Part.Key == MatchedKey && Part.Picker.IsValid())
		{
			Candidates.Add(&Part);
		}
	}
	if (Candidates.Num() == 0)
	{
		return nullptr;
	}

	// The side that was asked for, if the part has it.
	const FString Lower = Side.TrimStartAndEnd().ToLower();
	if (Lower == TEXT("left") || Lower == TEXT("right") || Lower == TEXT("center") || Lower == TEXT("centre"))
	{
		const FString Wanted = NormalizeSide(Lower);
		for (const FIHPCPartRef* Candidate : Candidates)
		{
			if (Candidate->Side == Wanted)
			{
				return Candidate;
			}
		}
	}
	if (Candidates.Num() == 1)
	{
		return Candidates[0];
	}

	// No side given: the one the user can see best, i.e. the one nearer to the camera.
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->PlayerCameraManager)
	{
		return Candidates[0];
	}
	const FVector Eye = Controller->PlayerCameraManager->GetCameraLocation();
	const FIHPCPartRef* Nearest = Candidates[0];
	float NearestDistance2 = TNumericLimits<float>::Max();
	for (const FIHPCPartRef* Candidate : Candidates)
	{
		FVector Centre;
		float Radius = 0.f;
		if (Candidate->Picker->GetMuscleWorldLocation(Candidate->Id, Centre, Radius))
		{
			const float Distance2 = FVector::DistSquared(Centre, Eye);
			if (Distance2 < NearestDistance2)
			{
				NearestDistance2 = Distance2;
				Nearest = Candidate;
			}
		}
	}
	return Nearest;
}

FIHPCActionResult UIHPCAssistantComp::DoShowPart(const FJsonObject& Action)
{
	const TCHAR* Type = TEXT("show_part");
	FString Kind = GetText(Action, TEXT("kind")).ToLower();
	const FString Name = GetText(Action, TEXT("name"));
	const FString Side = GetText(Action, TEXT("side"));
	if (Kind != TEXT("muscle") && Kind != TEXT("bone"))
	{
		Kind.Reset();   // not said: look through both
	}

	if (Parts.Num() == 0)
	{
		return Fail(Type, TEXT("This avatar has no muscles or bones I can point at."));
	}
	const FIHPCPartRef* Part = FindPart(Kind, Name, Side);
	if (!Part && !Kind.IsEmpty())
	{
		Part = FindPart(FString(), Name, Side);   // "the femur muscle": the name decides
	}
	UAnatomyPickerComp* Picker = Part ? Part->Picker.Get() : nullptr;
	if (!Picker)
	{
		return Fail(Type, FString::Printf(TEXT("This avatar has no %s called \"%s\"."), Kind.IsEmpty() ? TEXT("muscle or bone") : *Kind, *Name));
	}
	const int32 PartId = Part->Id;
	const bool bBone = Part->Kind == TEXT("bone");

	bool bDescribe = true, bFocus = true;
	GetFlag(Action, TEXT("describe"), bDescribe);
	GetFlag(Action, TEXT("focus"), bFocus);

	// 1. The layer the part is on.
	const EIHPCAnatomyLayer Wanted = bBone ? EIHPCAnatomyLayer::Skeleton : EIHPCAnatomyLayer::Muscles;
	UCPP_UW_IHPCLayerSwitcher* Layers = GetHud() ? GetHud()->GetLayerSwitcher() : nullptr;
	if (Layers && Layers->CanDriveAvatarLayers())
	{
		if (Layers->GetSelectedLayer() != Wanted)
		{
			Layers->SelectLayer(Wanted);
		}
	}
	else if (UAnatomyViewComp* View = GetAvatar() ? GetAvatar()->FindComponentByClass<UAnatomyViewComp>() : nullptr)
	{
		View->ShowLayerByName(Picker->RequiredLayerName);   // no HUD: drive the avatar directly
	}

	// 2. One highlighted part at a time, across the layers.
	for (const TWeakObjectPtr<UAnatomyPickerComp>& Other : Pickers)
	{
		if (Other.IsValid() && Other.Get() != Picker)
		{
			Other->ClearSelection();
		}
	}

	// 3. Highlight it. A new selection writes the description into the chat (HandlePartSelectionChanged); a part
	//    that was already highlighted does not fire that event, so it is described here.
	const bool bWasSelected = Picker->GetSelectedMuscle() == PartId;
	const bool bWasDescribing = bDescribeClickedParts;
	bDescribeClickedParts = bDescribe && bWasDescribing;
	Picker->SelectById(PartId);
	bDescribeClickedParts = bWasDescribing;
	if (bDescribe && (bWasSelected || !bWasDescribing))
	{
		DescribePart(Picker, PartId);
	}

	// 4. Turn the camera to it.
	UCPP_MouseFunctionComp* Camera = GetCamera();
	FVector Centre;
	float Radius = 0.f;
	if (bFocus && Camera && Picker->GetMuscleWorldLocation(PartId, Centre, Radius))
	{
		FVector Origin, Forward, Right;
		GetAvatarFrame(Origin, Forward, Right);
		const FVector From = PickViewSide(FindBodyMesh(GetAvatar()), Centre, Origin, Forward, Right);
		FVector Look = -From;   // the camera looks back at the part from that side ...
		Look.Z = -0.12f;        // ... a little from above

		const float Distance = FMath::Max(FocusMinDistance, Radius * FocusDistanceScale + FocusDistanceExtra);
		Camera->MoveViewTo(Look, /*bSetTarget*/ true, Centre, Distance, CameraMoveSeconds);
	}
	return Ok(Type);
}

FIHPCActionResult UIHPCAssistantComp::DoClearSelection(const FJsonObject& Action)
{
	for (const TWeakObjectPtr<UAnatomyPickerComp>& Picker : Pickers)
	{
		if (Picker.IsValid())
		{
			Picker->ClearSelection();
		}
	}
	return Ok(TEXT("clear_selection"));
}

// ---------------------------------------------------------------------------
// play_action, open_panel, switch_avatar
// ---------------------------------------------------------------------------

FIHPCActionResult UIHPCAssistantComp::DoPlayAction(const FJsonObject& Action)
{
	const TCHAR* Type = TEXT("play_action");
	const FString Name = GetText(Action, TEXT("name"));
	UCPP_UW_IHPCActionsPanel* Panel = GetHud() ? GetHud()->GetActionsPanel() : nullptr;
	if (!Panel)
	{
		return Fail(Type, TEXT("The action list is not on screen."));
	}

	// "running", "Running" and "tennis forehand" all find their card.
	const FString Key = NormalizeName(Name);
	FName Target = NAME_None;
	const TArray<FName> Available = Panel->GetAvailableActions();
	for (const FName& Candidate : Available)
	{
		if (NormalizeName(Candidate.ToString()) == Key)
		{
			Target = Candidate;
			break;
		}
	}
	if (Target.IsNone())
	{
		TArray<FString> Names;
		for (const FName& Candidate : Available)
		{
			Names.Add(Candidate.ToString());
		}
		return Fail(Type, FString::Printf(TEXT("This avatar can't do \"%s\". It can do: %s."), *Name, *FString::Join(Names, TEXT(", "))));
	}

	// Selecting the card that is already on would switch it off again (a second click = back to Idle).
	if (Panel->GetHighlightedAction() != Target)
	{
		Panel->SelectAction(Target);
	}
	if (Panel->GetHighlightedAction() != Target)
	{
		return Fail(Type, FString::Printf(TEXT("\"%s\" did not start on this avatar."), *Target.ToString()));
	}
	return Ok(Type);
}

FIHPCActionResult UIHPCAssistantComp::DoOpenPanel(const FJsonObject& Action)
{
	const TCHAR* Type = TEXT("open_panel");
	const FString Panel = GetText(Action, TEXT("panel")).ToLower();
	UCPP_UW_IHPCHud* HudWidget = GetHud();
	if (!HudWidget)
	{
		return Fail(Type, TEXT("The side panels are not on screen."));
	}

	EIHPCSideTab Tab;
	if (Panel == TEXT("none") || Panel == TEXT("close") || Panel.IsEmpty())       { Tab = EIHPCSideTab::None; }
	else if (Panel == TEXT("actions") || Panel == TEXT("action"))                 { Tab = EIHPCSideTab::Actions; }
	else if (Panel == TEXT("pose_estimation") || Panel == TEXT("pose"))           { Tab = EIHPCSideTab::PoseEstimation; }
	else if (Panel == TEXT("graphs") || Panel == TEXT("graph"))                   { Tab = EIHPCSideTab::Graphs; }
	else if (Panel == TEXT("timeline"))                                           { Tab = EIHPCSideTab::Timeline; }
	else if (Panel == TEXT("saved_videos") || Panel == TEXT("videos"))            { Tab = EIHPCSideTab::SavedVideos; }
	else if (Panel == TEXT("settings"))                                           { Tab = EIHPCSideTab::Settings; }
	else
	{
		return Fail(Type, FString::Printf(TEXT("There is no panel called \"%s\"."), *Panel));
	}
	HudWidget->SelectSideTab(Tab);
	return Ok(Type);
}

FIHPCActionResult UIHPCAssistantComp::DoSwitchAvatar(const FJsonObject& Action)
{
	const TCHAR* Type = TEXT("switch_avatar");
	ACPP_PlayerController* Controller = Cast<ACPP_PlayerController>(GetOwner());
	if (!Controller || Controller->SwitchablePawnClasses.Num() < 2)
	{
		return Fail(Type, TEXT("There is only one avatar."));
	}
	const FString Direction = GetText(Action, TEXT("direction")).ToLower();
	const APawn* Before = Controller->GetPawn();
	Controller->SwitchPawn((Direction == TEXT("previous") || Direction == TEXT("prev") || Direction == TEXT("back")) ? -1 : 1);
	if (Controller->GetPawn() == Before)
	{
		return Fail(Type, TEXT("The other avatar could not be shown."));
	}
	return Ok(Type);
}

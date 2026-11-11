// IHPCAssistantContext.cpp - what the assistant sends with every request: the avatar's state ("context") and the
// names of its muscles and bones ("catalog"). Field names and values are fixed by docs/PROTOCOL.md.

#include "Assistant/IHPCAssistantComp.h"

#include "../../CPP_MouseFunctionComp.h"    // module root (not on the include path)
#include "../../CPP_PlayerController.h"
#include "AnatomyPickerComp.h"
#include "AnimationTimelineComp.h"
#include "IHPC_UI/CPP_UW_IHPCActionsPanel.h"
#include "IHPC_UI/CPP_UW_IHPCHud.h"
#include "IHPC_UI/CPP_UW_IHPCLayerSwitcher.h"
#include "IHPC_UI/CPP_UW_IHPCSideBar.h"
#include "IHPC_UI/CPP_UW_IHPCTimeline.h"
#include "IHPC_UI/IHPCUILibrary.h"
#include "MuscleAtlas.h"
#include "PoseEstimation/PoseEstimationComp.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameFramework/Pawn.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	const TCHAR* LayerName(EIHPCAnatomyLayer Layer)
	{
		switch (Layer)
		{
		case EIHPCAnatomyLayer::Muscles:  return TEXT("muscles");
		case EIHPCAnatomyLayer::Skeleton: return TEXT("skeleton");
		default:                          return TEXT("body");
		}
	}

	const TCHAR* PanelName(EIHPCSideTab Tab)
	{
		switch (Tab)
		{
		case EIHPCSideTab::Actions:        return TEXT("actions");
		case EIHPCSideTab::PoseEstimation: return TEXT("pose_estimation");
		case EIHPCSideTab::Graphs:         return TEXT("graphs");
		case EIHPCSideTab::Timeline:       return TEXT("timeline");
		case EIHPCSideTab::SavedVideos:    return TEXT("saved_videos");
		case EIHPCSideTab::Settings:       return TEXT("settings");
		default:                           return TEXT("none");
		}
	}

	const TCHAR* VisibleSideName(EPoseVisibleSide Side)
	{
		switch (Side)
		{
		case EPoseVisibleSide::Right: return TEXT("right");
		case EPoseVisibleSide::Left:  return TEXT("left");
		case EPoseVisibleSide::Front: return TEXT("front");
		case EPoseVisibleSide::Back:  return TEXT("back");
		default:                      return TEXT("");
		}
	}

	/** Two decimals are plenty for the server and keep the request short. */
	double Round2(double Value)
	{
		return FMath::RoundToDouble(Value * 100.0) / 100.0;
	}

	FString ToJsonString(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}
}

FString UIHPCAssistantComp::NormalizeName(const FString& Name)
{
	// "Biceps femoris (long head)" and "biceps femoris long head" are the same name.
	FString Out;
	Out.Reserve(Name.Len());
	for (const TCHAR Char : Name)
	{
		if (FChar::IsAlnum(Char))
		{
			Out.AppendChar(FChar::ToLower(Char));
		}
	}
	return Out;
}

FString UIHPCAssistantComp::NormalizeSide(const FString& Side)
{
	const FString Lower = Side.TrimStartAndEnd().ToLower();
	if (Lower.StartsWith(TEXT("l")))
	{
		return TEXT("left");
	}
	if (Lower.StartsWith(TEXT("r")))
	{
		return TEXT("right");
	}
	return TEXT("center");   // "Centre", "Center", "" (the atlases use Centre for the spine, skull ...)
}

// ---------------------------------------------------------------------------
// Catalog: the names the avatar knows
// ---------------------------------------------------------------------------

void UIHPCAssistantComp::RebuildCatalog()
{
	Parts.Reset();

	for (const TWeakObjectPtr<UAnatomyPickerComp>& WeakPicker : Pickers)
	{
		const UAnatomyPickerComp* Picker = WeakPicker.Get();
		const UMuscleAtlas* Atlas = Picker ? Picker->GetAtlas() : nullptr;
		if (!Atlas)
		{
			continue;
		}
		// A picker works on one anatomy layer; the layer says whether its parts are muscles or bones.
		const FString Layer = Picker->RequiredLayerName.ToString();
		const bool bBones = Layer.Contains(TEXT("Skel")) || Layer.Contains(TEXT("Bone"));

		for (const FMuscleInfo& Info : Atlas->Muscles)
		{
			FIHPCPartRef Part;
			Part.Picker = const_cast<UAnatomyPickerComp*>(Picker);
			Part.Id = Info.Id;
			Part.Kind = bBones ? TEXT("bone") : TEXT("muscle");
			Part.Name = Info.Name.ToString().TrimStartAndEnd();
			Part.Side = NormalizeSide(Info.Side.ToString());
			Part.Region = Info.Region.ToString();
			Part.Key = NormalizeName(Part.Name);
			if (Part.Id > 0 && !Part.Key.IsEmpty())
			{
				Parts.Add(MoveTemp(Part));
			}
		}
	}

	// One catalog entry per kind + name, with the sides it exists on.
	TArray<TSharedPtr<FJsonValue>> Entries;
	TMap<FString, TSharedPtr<FJsonObject>> ByName;
	static const TCHAR* SideOrder[] = { TEXT("left"), TEXT("right"), TEXT("center") };
	for (const FIHPCPartRef& Part : Parts)
	{
		const FString EntryKey = Part.Kind + TEXT(":") + Part.Key;
		if (!ByName.Contains(EntryKey))
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("kind"), Part.Kind);
			Entry->SetStringField(TEXT("name"), Part.Name);
			TArray<TSharedPtr<FJsonValue>> Sides;
			for (const TCHAR* Side : SideOrder)
			{
				const bool bHasSide = Parts.ContainsByPredicate([&](const FIHPCPartRef& Other)
					{ return Other.Kind == Part.Kind && Other.Key == Part.Key && Other.Side == Side; });
				if (bHasSide)
				{
					Sides.Add(MakeShared<FJsonValueString>(Side));
				}
			}
			Entry->SetArrayField(TEXT("sides"), Sides);
			Entry->SetStringField(TEXT("region"), Part.Region);
			ByName.Add(EntryKey, Entry);
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	const TSharedRef<FJsonObject> Catalog = MakeShared<FJsonObject>();
	Catalog->SetArrayField(TEXT("parts"), Entries);
	CatalogJson = ToJsonString(Catalog);
	// The id only has to change when the names change: a hash of the text.
	CatalogId = Entries.Num() > 0 ? FMD5::HashAnsiString(*CatalogJson).Left(12) : FString();

	UE_LOG(LogIHPCAssistant, Log, TEXT("Catalog %s: %d names (%d muscle / bone entries) from %d picker(s) on %s"),
		CatalogId.IsEmpty() ? TEXT("(empty)") : *CatalogId, Entries.Num(), Parts.Num(), Pickers.Num(), *GetNameSafe(GetAvatar()));
}

// ---------------------------------------------------------------------------
// Context: the avatar's state right now
// ---------------------------------------------------------------------------

TSharedRef<FJsonObject> UIHPCAssistantComp::BuildContext() const
{
	const TSharedRef<FJsonObject> Context = MakeShared<FJsonObject>();
	const ACPP_PlayerController* Controller = Cast<ACPP_PlayerController>(GetOwner());
	const APawn* Avatar = GetAvatar();
	const UCPP_UW_IHPCHud* HudWidget = GetHud();

	// ---- avatar
	if (Avatar)
	{
		const TSharedRef<FJsonObject> AvatarJson = MakeShared<FJsonObject>();
		FString Name = Avatar->GetClass()->GetName();
		Name.RemoveFromEnd(TEXT("_C"));   // Blueprint classes are called BP_Abby8_C
		AvatarJson->SetStringField(TEXT("name"), Name);
		if (Controller)
		{
			AvatarJson->SetNumberField(TEXT("index"), Controller->GetCurrentPawnIndex());
			AvatarJson->SetNumberField(TEXT("count"), Controller->SwitchablePawnClasses.Num());
		}
		Context->SetObjectField(TEXT("avatar"), AvatarJson);
	}

	// ---- layer (only for an avatar that has anatomy layers)
	const UCPP_UW_IHPCLayerSwitcher* Layers = HudWidget ? HudWidget->GetLayerSwitcher() : nullptr;
	if (Layers && Layers->CanDriveAvatarLayers())
	{
		Context->SetStringField(TEXT("layer"), LayerName(Layers->GetSelectedLayer()));
	}

	// ---- timeline
	if (const UAnimationTimelineComp* Animation = UIHPCUILibrary::FindAvatarTimeline(this))
	{
		const TSharedRef<FJsonObject> TimelineJson = MakeShared<FJsonObject>();
		TimelineJson->SetBoolField(TEXT("playing"), Animation->IsPlaying());
		TimelineJson->SetNumberField(TEXT("speed"), Round2(Animation->PlaySpeed));
		TimelineJson->SetNumberField(TEXT("time"), Round2(Animation->GetNormalizedTime()));
		TimelineJson->SetNumberField(TEXT("cycle_seconds"), Round2(Animation->GetCycleLength()));
		if (const UCPP_UW_IHPCTimeline* Bar = HudWidget ? HudWidget->GetTimeline() : nullptr)
		{
			TArray<TSharedPtr<FJsonValue>> Steps;
			for (const float Step : Bar->SpeedSteps)
			{
				Steps.Add(MakeShared<FJsonValueNumber>(Step));
			}
			TimelineJson->SetArrayField(TEXT("speed_steps"), Steps);
			TimelineJson->SetBoolField(TEXT("recording"), Bar->IsRecording());
		}
		Context->SetObjectField(TEXT("timeline"), TimelineJson);
	}

	// ---- action clips
	if (const UCPP_UW_IHPCActionsPanel* Actions = HudWidget ? HudWidget->GetActionsPanel() : nullptr)
	{
		const TSharedRef<FJsonObject> ActionJson = MakeShared<FJsonObject>();
		// "" = the avatar's own animation (the Idle card).
		const FName Highlighted = Actions->GetHighlightedAction();
		const bool bIsIdleCard = Actions->Actions.ContainsByPredicate([Highlighted](const FIHPCActionInfo& Info)
			{ return Info.bIsDefault && Info.ActionName == Highlighted; });
		ActionJson->SetStringField(TEXT("playing"), (Highlighted.IsNone() || bIsIdleCard) ? FString() : Highlighted.ToString());
		TArray<TSharedPtr<FJsonValue>> Available;
		for (const FName& Name : Actions->GetAvailableActions())
		{
			Available.Add(MakeShared<FJsonValueString>(Name.ToString()));
		}
		ActionJson->SetArrayField(TEXT("available"), Available);
		Context->SetObjectField(TEXT("action"), ActionJson);
	}

	// ---- side panel, chat
	if (HudWidget)
	{
		Context->SetStringField(TEXT("panel"), PanelName(HudWidget->GetSelectedSideTab()));
		if (const UCPP_UW_IHPCSideBar* SideBar = HudWidget->GetSideBar())
		{
			Context->SetBoolField(TEXT("chat_open"), SideBar->IsChatOpen());
		}
	}

	// ---- camera
	if (const UCPP_MouseFunctionComp* Camera = GetCamera())
	{
		const TSharedRef<FJsonObject> CameraJson = MakeShared<FJsonObject>();
		CameraJson->SetNumberField(TEXT("distance"), Round2(Camera->GetTargetCameraDistance()));
		CameraJson->SetNumberField(TEXT("min"), Camera->MinZoom);
		CameraJson->SetNumberField(TEXT("max"), Camera->MaxZoom);
		CameraJson->SetBoolField(TEXT("moved"), Camera->IsViewChanged());
		Context->SetObjectField(TEXT("camera"), CameraJson);
	}

	// ---- pose estimation
	if (const UPoseEstimationComp* Pose = Controller ? Controller->GetPoseEstimation() : nullptr)
	{
		const TSharedRef<FJsonObject> PoseJson = MakeShared<FJsonObject>();
		static const TCHAR* StatusNames[] = { TEXT("off"), TEXT("loading"), TEXT("running"), TEXT("error") };
		PoseJson->SetStringField(TEXT("status"), StatusNames[FMath::Clamp(static_cast<int32>(Pose->GetStatus()), 0, 3)]);
		PoseJson->SetBoolField(TEXT("overlay"), Pose->IsEstimationEnabled() && Pose->IsOverlayVisible());
		PoseJson->SetBoolField(TEXT("panel_open"), Pose->IsDataPanelOpen());
		PoseJson->SetBoolField(TEXT("recording"), Pose->IsRecording());
		PoseJson->SetNumberField(TEXT("fps"), Round2(Pose->GetAnalysesPerSecond()));

		// The numbers only mean something while frames are being analysed.
		const FPosePerson* Person = HasFreshPose() ? Pose->GetLatestResult().GetPrimary() : nullptr;
		PoseJson->SetBoolField(TEXT("person"), Person != nullptr);
		if (Person)
		{
			PoseJson->SetStringField(TEXT("visible_side"), VisibleSideName(Person->VisibleSide));
			const TSharedRef<FJsonObject> AnglesJson = MakeShared<FJsonObject>();
			for (const FPoseAngle& Angle : Person->Angles)
			{
				if (Angle.bValid)
				{
					AnglesJson->SetNumberField(Angle.Name.ToString(), FMath::RoundToDouble(Angle.Degrees * 10.0) / 10.0);
				}
			}
			PoseJson->SetObjectField(TEXT("angles"), AnglesJson);
		}
		Context->SetObjectField(TEXT("pose"), PoseJson);
	}

	// ---- the highlighted muscle / bone
	for (const FIHPCPartRef& Part : Parts)
	{
		const UAnatomyPickerComp* Picker = Part.Picker.Get();
		if (Picker && Picker->GetSelectedMuscle() == Part.Id)
		{
			const TSharedRef<FJsonObject> PartJson = MakeShared<FJsonObject>();
			PartJson->SetStringField(TEXT("kind"), Part.Kind);
			PartJson->SetStringField(TEXT("name"), Part.Name);
			PartJson->SetStringField(TEXT("side"), Part.Side);
			Context->SetObjectField(TEXT("selected_part"), PartJson);
			break;
		}
	}

	// ---- what the last answer's actions did
	TArray<TSharedPtr<FJsonValue>> Results;
	for (const FIHPCActionResult& Result : LastResults)
	{
		const TSharedRef<FJsonObject> ResultJson = MakeShared<FJsonObject>();
		ResultJson->SetStringField(TEXT("type"), Result.Type);
		ResultJson->SetBoolField(TEXT("ok"), Result.bOk);
		ResultJson->SetStringField(TEXT("message"), Result.Message);
		Results.Add(MakeShared<FJsonValueObject>(ResultJson));
	}
	Context->SetArrayField(TEXT("last_results"), Results);

	return Context;
}

FString UIHPCAssistantComp::BuildRequestBody(const FRequest& Request, bool& bOutIncludedCatalog) const
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetNumberField(TEXT("protocol"), 1);
	Body->SetStringField(TEXT("session_id"), SessionId);
	if (!Request.bVoice)
	{
		Body->SetStringField(TEXT("text"), Request.Text);
	}
	Body->SetObjectField(TEXT("context"), BuildContext());
	Body->SetStringField(TEXT("catalog_id"), CatalogId);

	// The names go along the first time (new session, or a different avatar) and whenever the server asks.
	bOutIncludedCatalog = !CatalogId.IsEmpty() && (Request.bForceCatalog || CatalogIdSent != CatalogId);
	if (bOutIncludedCatalog)
	{
		TSharedPtr<FJsonObject> Catalog;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CatalogJson);
		if (FJsonSerializer::Deserialize(Reader, Catalog) && Catalog.IsValid())
		{
			Body->SetObjectField(TEXT("catalog"), Catalog);
		}
	}
	return ToJsonString(Body);
}

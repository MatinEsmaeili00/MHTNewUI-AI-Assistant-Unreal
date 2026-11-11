#include "AnatomyPickerComp.h"

#include "AnatomyViewComp.h"
#include "MetaHumanAnatomyLayers.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

UAnatomyPickerComp::UAnatomyPickerComp()
{
	PrimaryComponentTick.bCanEverTick = false; // driven by the player controller (UpdateHover / SelectHovered)
	Atlas = TSoftObjectPtr<UMuscleAtlas>(FSoftObjectPath(TEXT("/MetaHumanAnatomyLayers/Anatomy/Data/DA_MuscleAtlas_UpperBody.DA_MuscleAtlas_UpperBody")));
	HighlightMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/MetaHumanAnatomyLayers/Anatomy/Materials/M_Anatomy_MuscleHighlight.M_Anatomy_MuscleHighlight")));
}

void UAnatomyPickerComp::BeginPlay()
{
	Super::BeginPlay();
	Resolve();
}

void UAnatomyPickerComp::Resolve()
{
	LoadedAtlas = Atlas.LoadSynchronous();
	Mesh = nullptr;
	View = GetOwner() ? GetOwner()->FindComponentByClass<UAnatomyViewComp>() : nullptr;

	if (AActor* Owner = GetOwner())
	{
		TArray<USkinnedMeshComponent*> Meshes;
		Owner->GetComponents<USkinnedMeshComponent>(Meshes);
		for (USkinnedMeshComponent* Candidate : Meshes)
		{
			if (Candidate && Candidate->GetFName() == MeshComponentName)
			{
				Mesh = Candidate;
				break;
			}
		}
	}

	SampleBones.Reset();
	if (Mesh && LoadedAtlas)
	{
		SampleBones.SetNum(LoadedAtlas->Muscles.Num());
		for (int32 MuscleIndex = 0; MuscleIndex < LoadedAtlas->Muscles.Num(); ++MuscleIndex)
		{
			for (const FMuscleSample& Sample : LoadedAtlas->Muscles[MuscleIndex].Samples)
			{
				SampleBones[MuscleIndex].Add(Mesh->GetBoneIndex(Sample.Bone));
			}
		}
	}

	if (UMaterialInterface* Highlight = HighlightMaterial.LoadSynchronous())
	{
		HighlightMID = UMaterialInstanceDynamic::Create(Highlight, this);
	}

	// Highlight copy of the muscle mesh: same mesh, follows the same leader, translucent highlight material.
	USkeletalMeshComponent* Source = Cast<USkeletalMeshComponent>(Mesh);
	if (Source && HighlightMID && !HighlightMesh)
	{
		// One copy PER PICKER, so the name must be unique: with a fixed name ("MuscleHighlight") the second picker
		// (BonePicker) re-created the first picker's copy in place - the muscle picker then drove a material that
		// was no longer on any mesh, and hovered muscles never lit up.
		const FName CopyName = MakeUniqueObjectName(GetOwner(), USkeletalMeshComponent::StaticClass(),
			FName(*FString::Printf(TEXT("%s_Highlight"), *GetName())));
		USkeletalMeshComponent* Copy = NewObject<USkeletalMeshComponent>(GetOwner(), CopyName);
		Copy->SetSkeletalMeshAsset(Source->GetSkeletalMeshAsset());
		Copy->SetupAttachment(Source);
		Copy->ComponentTags.Add(TEXT("AnatomyIgnore")); // not a layer of its own for UAnatomyViewComp
		Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Copy->SetCastShadow(false);
		Copy->bUseBoundsFromLeaderPoseComponent = true;
		Copy->SetVisibility(false);
		Copy->RegisterComponent();
		USkinnedMeshComponent* Leader = Source->LeaderPoseComponent.IsValid() ? Source->LeaderPoseComponent.Get() : Source;
		Copy->SetLeaderPoseComponent(Leader);
		for (int32 Slot = 0; Slot < Copy->GetNumMaterials(); ++Slot)
		{
			Copy->SetMaterial(Slot, HighlightMID);
		}
		Copy->SetTranslucentSortPriority(10);
		Copy->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		HighlightMesh = Copy;
	}

	UE_LOG(LogAnatomyLayers, Log, TEXT("[%s] picker: mesh %s, %d muscles"), *GetNameSafe(GetOwner()), *GetNameSafe(Mesh),
		LoadedAtlas ? LoadedAtlas->Muscles.Num() : 0);
}

bool UAnatomyPickerComp::IsPickingActive() const
{
	if (!Mesh || !LoadedAtlas || !Mesh->IsVisible())
	{
		return false;
	}
	if (!View)
	{
		return true;
	}
	const int32 LayerIndex = View->FindLayerIndex(RequiredLayerName);
	if (LayerIndex == INDEX_NONE)
	{
		return true;
	}
	// The layer must be what you see: opaque enough, and every layer outside it already faded / ghosted.
	if (View->GetLayerOpacity(LayerIndex) < MinLayerOpacity)
	{
		return false;
	}
	for (int32 Outer = 0; Outer < LayerIndex; ++Outer)
	{
		if (View->GetLayerOpacity(Outer) >= 0.999f)
		{
			return false;
		}
	}
	return true;
}

void UAnatomyPickerComp::UpdateHover(FVector RayOrigin, FVector RayDirection)
{
	if (!IsPickingActive())
	{
		ClearHover();
		RefreshHighlight(); // hides the selection glow while the muscle layer isn't shown
		return;
	}

	const FVector Dir = RayDirection.GetSafeNormal();
	const float Radius2 = PickRadius * PickRadius;
	float BestT = TNumericLimits<float>::Max();
	int32 BestId = 0;

	// Bone transforms once per update (the muscle mesh follows Body through Leader Pose).
	TMap<int32, FTransform> BoneCache;
	for (int32 MuscleIndex = 0; MuscleIndex < LoadedAtlas->Muscles.Num(); ++MuscleIndex)
	{
		const FMuscleInfo& Muscle = LoadedAtlas->Muscles[MuscleIndex];
		const TArray<int32>& Bones = SampleBones[MuscleIndex];
		for (int32 SampleIndex = 0; SampleIndex < Muscle.Samples.Num(); ++SampleIndex)
		{
			const int32 BoneIndex = Bones[SampleIndex];
			if (BoneIndex == INDEX_NONE)
			{
				continue;
			}
			const FTransform* Bone = BoneCache.Find(BoneIndex);
			if (!Bone)
			{
				Bone = &BoneCache.Add(BoneIndex, Mesh->GetBoneTransform(BoneIndex));
			}
			const FVector Point = Bone->TransformPosition(Muscle.Samples[SampleIndex].Offset);
			const float T = FVector::DotProduct(Point - RayOrigin, Dir);
			if (T <= 0.f || T >= BestT)
			{
				continue;
			}
			if (FVector::DistSquared(Point, RayOrigin + Dir * T) <= Radius2)
			{
				BestT = T;
				BestId = Muscle.Id;
			}
		}
	}
	SetHovered(BestId);

	// A selection made while this layer was still fading in (SelectById during a layer change) could not show its
	// glow then: show it now that the layer is the one being looked at.
	if (SelectedId > 0 && HighlightMesh && !HighlightMesh->IsVisible())
	{
		RefreshHighlight();
	}
}

void UAnatomyPickerComp::ClearHover()
{
	SetHovered(0);
}

void UAnatomyPickerComp::UpdateWithoutPointer()
{
	ClearHover();
	// The selection may have to appear (its layer has just arrived) or disappear (another layer is shown now).
	if (HighlightMesh && HighlightMesh->IsVisible() != (SelectedId > 0 && IsPickingActive()))
	{
		RefreshHighlight();
	}
}

void UAnatomyPickerComp::SetHovered(int32 MuscleId)
{
	if (MuscleId == HoveredId)
	{
		return;
	}
	HoveredId = MuscleId;
	RefreshHighlight();
	OnHoverChanged.Broadcast(HoveredId);
}

bool UAnatomyPickerComp::SelectHovered()
{
	if (HoveredId == 0)
	{
		ClearSelection();
		return false;
	}
	if (SelectedId != HoveredId)
	{
		SelectedId = HoveredId;
		RefreshHighlight();
		OnSelectionChanged.Broadcast(SelectedId);
	}
	return true;
}

bool UAnatomyPickerComp::SelectById(int32 MuscleId)
{
	if (MuscleId <= 0 || !LoadedAtlas || !LoadedAtlas->FindById(MuscleId))
	{
		return false;
	}
	if (SelectedId != MuscleId)
	{
		SelectedId = MuscleId;
		RefreshHighlight();
		OnSelectionChanged.Broadcast(SelectedId);
	}
	return true;
}

bool UAnatomyPickerComp::GetMuscleWorldLocation(int32 MuscleId, FVector& OutCentre, float& OutRadius) const
{
	OutCentre = FVector::ZeroVector;
	OutRadius = 0.f;
	if (!Mesh || !LoadedAtlas)
	{
		return false;
	}
	const int32 MuscleIndex = LoadedAtlas->Muscles.IndexOfByPredicate([MuscleId](const FMuscleInfo& Info) { return Info.Id == MuscleId; });
	if (MuscleIndex == INDEX_NONE || !SampleBones.IsValidIndex(MuscleIndex))
	{
		return false;
	}

	// The same points the mouse picking uses, placed on the animated skeleton.
	const FMuscleInfo& Muscle = LoadedAtlas->Muscles[MuscleIndex];
	const TArray<int32>& Bones = SampleBones[MuscleIndex];
	TArray<FVector, TInlineAllocator<64>> Points;
	TMap<int32, FTransform> BoneCache;
	for (int32 SampleIndex = 0; SampleIndex < Muscle.Samples.Num() && SampleIndex < Bones.Num(); ++SampleIndex)
	{
		const int32 BoneIndex = Bones[SampleIndex];
		if (BoneIndex == INDEX_NONE)
		{
			continue;
		}
		const FTransform* Bone = BoneCache.Find(BoneIndex);
		if (!Bone)
		{
			Bone = &BoneCache.Add(BoneIndex, Mesh->GetBoneTransform(BoneIndex));
		}
		Points.Add(Bone->TransformPosition(Muscle.Samples[SampleIndex].Offset));
	}
	if (Points.Num() == 0)
	{
		return false;
	}

	FVector Sum = FVector::ZeroVector;
	for (const FVector& Point : Points)
	{
		Sum += Point;
	}
	OutCentre = Sum / Points.Num();
	float MaxDistance2 = 0.f;
	for (const FVector& Point : Points)
	{
		MaxDistance2 = FMath::Max<float>(MaxDistance2, FVector::DistSquared(Point, OutCentre));
	}
	OutRadius = FMath::Sqrt(MaxDistance2);
	return true;
}

void UAnatomyPickerComp::ClearSelection()
{
	if (SelectedId != 0)
	{
		SelectedId = 0;
		RefreshHighlight();
		OnSelectionChanged.Broadcast(0);
	}
}

void UAnatomyPickerComp::RefreshHighlight()
{
	if (!Mesh || !HighlightMID)
	{
		return;
	}
	HighlightMID->SetScalarParameterValue(TEXT("HoverId"), HoveredId > 0 ? static_cast<float>(HoveredId) : -1.f);
	HighlightMID->SetScalarParameterValue(TEXT("SelectedId"), SelectedId > 0 ? static_cast<float>(SelectedId) : -1.f);
	// The highlight copy is an extra translucent draw of the whole mesh: only show it while something is highlighted
	// and the muscle layer is what you're looking at.
	if (HighlightMesh)
	{
		const bool bShow = (HoveredId > 0 || SelectedId > 0) && IsPickingActive();
		if (HighlightMesh->IsVisible() != bShow)
		{
			HighlightMesh->SetVisibility(bShow);
		}
	}
}

bool UAnatomyPickerComp::GetMuscleInfo(int32 MuscleId, FMuscleInfo& OutInfo) const
{
	if (const FMuscleInfo* Info = LoadedAtlas ? LoadedAtlas->FindById(MuscleId) : nullptr)
	{
		OutInfo = *Info;
		OutInfo.Samples.Reset(); // callers want the text, not the picking data
		return true;
	}
	return false;
}

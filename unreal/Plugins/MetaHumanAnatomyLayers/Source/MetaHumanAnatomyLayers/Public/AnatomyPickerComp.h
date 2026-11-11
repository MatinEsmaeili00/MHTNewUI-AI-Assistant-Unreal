// Anatomy picker: hover / click individual muscles on the animated muscle layer.
// Lives on the pawn next to UAnatomyViewComp. The player controller feeds it the mouse ray (UpdateHover) and clicks
// (SelectHovered); widgets listen to OnHoverChanged / OnSelectionChanged and read GetMuscleInfo.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MuscleAtlas.h"
#include "AnatomyPickerComp.generated.h"

class UAnatomyViewComp;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkinnedMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAnatomyMuscleChanged, int32, MuscleId);

UCLASS(ClassGroup=(Anatomy), meta=(BlueprintSpawnableComponent), DisplayName="Anatomy Picker")
class METAHUMANANATOMYLAYERS_API UAnatomyPickerComp : public UActorComponent
{
	GENERATED_BODY()

public:
	UAnatomyPickerComp();

	/** Names, info and picking samples of the muscles. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy Picker")
	TSoftObjectPtr<UMuscleAtlas> Atlas;

	/** The muscle mesh component (its vertex colours carry the muscle IDs). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy Picker")
	FName MeshComponentName = TEXT("MuscleMesh");

	/** Translucent material that glows the muscle whose ID equals HoverId / SelectedId (the mesh's vertex colours carry
	 *  the IDs). Drawn by a highlight copy of the muscle mesh that follows Body (an Overlay Material doesn't render on
	 *  Leader Pose followers). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy Picker")
	TSoftObjectPtr<UMaterialInterface> HighlightMaterial;

	/** Picking only works while this anatomy layer is the one you're looking at... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy Picker")
	FName RequiredLayerName = TEXT("Muscles");

	/** ...i.e. it is at least this opaque and the outer layer (skin) has started to fade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy Picker", meta=(ClampMin="0", ClampMax="1"))
	float MinLayerOpacity = 0.5f;

	/** How close (cm) the mouse ray has to pass to a muscle's sample points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anatomy Picker", meta=(ClampMin="0.5"))
	float PickRadius = 3.5f;

	UPROPERTY(BlueprintAssignable, Category="Anatomy Picker")
	FOnAnatomyMuscleChanged OnHoverChanged;

	UPROPERTY(BlueprintAssignable, Category="Anatomy Picker")
	FOnAnatomyMuscleChanged OnSelectionChanged;

	/** Ray from the camera through the mouse (world space). Hovers the front-most muscle the ray passes. */
	UFUNCTION(BlueprintCallable, Category="Anatomy Picker")
	void UpdateHover(FVector RayOrigin, FVector RayDirection);

	UFUNCTION(BlueprintCallable, Category="Anatomy Picker")
	void ClearHover();

	/** Select the hovered muscle (or clear the selection if nothing is hovered). Returns true if a muscle is selected. */
	UFUNCTION(BlueprintCallable, Category="Anatomy Picker")
	bool SelectHovered();

	UFUNCTION(BlueprintCallable, Category="Anatomy Picker")
	void ClearSelection();

	/** Select a muscle by its atlas ID without the mouse (the assistant's "show me the biceps"). Works while another
	 *  layer is shown too: the glow appears as soon as this picker's layer is the one you're looking at.
	 *  Returns false if the atlas has no such ID. Fires OnSelectionChanged if the selection changed. */
	UFUNCTION(BlueprintCallable, Category="Anatomy Picker")
	bool SelectById(int32 MuscleId);

	/** The loaded atlas (names, info text), or null before BeginPlay / when it is missing. */
	UFUNCTION(BlueprintPure, Category="Anatomy Picker")
	const UMuscleAtlas* GetAtlas() const { return LoadedAtlas; }

	/** Where a muscle is right now, in world space: the middle of its sample points on the animated skeleton, and
	 *  how far the points reach from it (cm). False if the muscle is unknown or has no usable samples. */
	UFUNCTION(BlueprintPure, Category="Anatomy Picker")
	bool GetMuscleWorldLocation(int32 MuscleId, FVector& OutCentre, float& OutRadius) const;

	UFUNCTION(BlueprintPure, Category="Anatomy Picker")
	bool IsPickingActive() const;

	UFUNCTION(BlueprintPure, Category="Anatomy Picker")
	int32 GetHoveredMuscle() const { return HoveredId; }

	UFUNCTION(BlueprintPure, Category="Anatomy Picker")
	int32 GetSelectedMuscle() const { return SelectedId; }

	UFUNCTION(BlueprintPure, Category="Anatomy Picker")
	bool GetMuscleInfo(int32 MuscleId, FMuscleInfo& OutInfo) const;

protected:
	virtual void BeginPlay() override;

private:
	void Resolve();
	void SetHovered(int32 MuscleId);
	void RefreshHighlight();

	UPROPERTY(Transient)
	TObjectPtr<UMuscleAtlas> LoadedAtlas = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USkinnedMeshComponent> Mesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAnatomyViewComp> View = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HighlightMID = nullptr;

	/** Copy of the muscle mesh with the highlight material; only visible while something is hovered / selected. */
	UPROPERTY(Transient)
	TObjectPtr<USkinnedMeshComponent> HighlightMesh = nullptr;

	/** Per atlas muscle, the bone index of each sample in Mesh (INDEX_NONE if missing). */
	TArray<TArray<int32>> SampleBones;

	int32 HoveredId = 0;
	int32 SelectedId = 0;
};

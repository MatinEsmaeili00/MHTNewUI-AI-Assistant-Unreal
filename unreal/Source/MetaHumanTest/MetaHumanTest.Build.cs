// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class MetaHumanTest : ModuleRules
{
	public MetaHumanTest(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "UMG",
			// The animated BOT logo in the side bar (Plugins/HoloAvatar). Public because CPP_UW_IHPCSideBar.h, a
			// public header, holds an FHoloAvatarSettings.
			"HoloAvatar",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"EnhancedInput",
			"Slate",
			"SlateCore",
			"MetaHumanAnatomyLayers", // UAnatomyViewComp (Plugins/MetaHumanAnatomyLayers)
			// Pose estimation (Source/MetaHumanTest/*/PoseEstimation)
			"NNE",        // Neural Network Engine: runs the ONNX models (runtime: NNERuntimeORT plugin)
			"RenderCore", // copying the game view back from the GPU
			"RHI",
			"ImageCore",  // loading / saving test images
			// The AI assistant behind the BOT chat (Source/MetaHumanTest/*/Assistant)
			"HTTP",             // requests to the AI server
			"Json",             // the request and the answer
			"AudioCaptureCore", // microphone for push-to-talk (hold the BOT logo)
			"AudioCapture",     // ... and the platform's capture code (engine plugin Audio Capture)
		});

		// The Sports2D models (.onnx) are read at runtime: ship them with packaged builds.
		RuntimeDependencies.Add("$(ProjectDir)/PoseModels/*.onnx", StagedFileType.UFS);
	}
}

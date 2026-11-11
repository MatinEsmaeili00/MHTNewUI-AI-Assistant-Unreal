// IHPCAssistantComp.h
// -----------------------------------------------------------------------------
// The AI ASSISTANT behind the BOT chat. Lives on the player controller (ACPP_PlayerController creates it), so the
// conversation and the server connection outlive a pawn switch.
//
//   chat box: type + Enter  ─┐
//   hold the BOT logo: talk ─┤──►  POST /v1/chat   {text, context}            ──►  AI server (DGX Spark)
//                            │     POST /v1/voice  {wav, context}                  Whisper -> rules -> LLM
//                            │◄──  {transcript, reply, actions[]}             ◄──
//                            ▼
//       runs the actions on the avatar (layer, timeline, camera, pose estimation, "show me the biceps" ...)
//       and writes the reply into the chat box
//
// The server decides WHAT to do; this component does it and knows whether it worked. It only ever makes outgoing
// HTTP requests, so it works across the internet (the app in one city, the AI computer in another).
// The contract with the server is docs/PROTOCOL.md of the repository MHTNewUI-AI-Assistant-Unreal (the same file is
// in MHTNewUI-AI-Assistant-Server). Protocol version: 1.
//
// It also does one thing with no server at all: a CLICK on a muscle or bone writes its description into the chat
// box (this replaced the info popup of UCPP_UW_MuscleInfo).
//
// Files:
//   IHPCAssistantComp.cpp          start-up, the chat, the request queue, HTTP, the answer
//   IHPCAssistantContext.cpp       what is sent with every request: the avatar's state and the names it knows
//   IHPCAssistantActions.cpp       one function per action type
//   IHPCAssistantVoice.h / .cpp    microphone -> WAV (push-to-talk)
//
// In the chat box:   /server <url>   /key <text>   /status   /clear   /help
//                    /do <action type> key=value ...   (runs one action without the server, like Assistant.Do)
// Console (also the editor's Cmd box during Play):
//   Assistant.Ask <text>        same as typing <text> in the chat box
//   Assistant.Do <json>         run one action without the server, e.g. Assistant.Do {"type":"set_layer","layer":"muscles"}
//   Assistant.Server [host:port] show / change (and remember) the server address. No "http://" here: the console
//                               reads "//" as a comment. ("/server http://host:8010" in the chat box is fine.)
//   Assistant.Key [text]        show whether a key is set / change (and remember) it
//   Assistant.Status            ask the server for its /health
//   Assistant.Talk 1 | 0        start / stop-and-send a microphone recording (same as holding the BOT logo)
//   Assistant.SendWav <file>    send a .wav file from disk as if it had been spoken
//   Assistant.HoldLogo [s]      TEST: press, hold and release the BOT logo through Slate's own pointer events
// Command line:      -AssistantURL=http://host:8010   -AssistantKey=...
// -----------------------------------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/IHttpRequest.h"
#include "IHPC_UI/IHPCTypes.h"
#include "PoseEstimation/PoseTypes.h"   // FPoseFrameResult (a UFUNCTION parameter needs the whole type)
#include "IHPCAssistantComp.generated.h"

class APawn;
class FJsonObject;
class FIHPCVoiceRecorder;
class UAnatomyPickerComp;
class UCPP_UW_IHPCHud;
class UCPP_UW_IHPCChatPanel;
class UCPP_UW_IHPCSideBar;
class UCPP_MouseFunctionComp;
class USkeletalMeshComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogIHPCAssistant, Log, All);

/** The assistant answered. Transcript = what it understood (what was typed, or what was heard), Reply = its text. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FIHPCAssistantReply, const FString&, Transcript, const FString&, Reply);

/** What happened to one action. Sent back to the server with the next request (context.last_results). */
struct FIHPCActionResult
{
	FString Type;
	bool bOk = false;
	FString Message;   // empty on success, one sentence for the user on failure
};

/** One muscle or bone of the possessed avatar (one side of it). */
struct FIHPCPartRef
{
	TWeakObjectPtr<UAnatomyPickerComp> Picker;
	int32 Id = 0;
	FString Kind;     // "muscle" | "bone"
	FString Name;     // as in the atlas: "Biceps brachii"
	FString Side;     // "left" | "right" | "center"
	FString Region;
	FString Key;      // normalised Name, for matching
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class METAHUMANTEST_API UIHPCAssistantComp : public UActorComponent
{
	GENERATED_BODY()

public:
	UIHPCAssistantComp();

	// ---------------------------------------------------------------- server

	/** The AI server (the DGX Spark), e.g. "http://spark:8010". Changed at run time with "/server <url>" in the chat
	 *  box (remembered on this computer) or -AssistantURL= on the command line; those win over this value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assistant|Server")
	FString ServerURL = TEXT("http://127.0.0.1:8010");

	/** Sent as "Authorization: Bearer <key>" when not empty. Must be the server's ASSISTANT_API_KEY.
	 *  Changed at run time with "/key <text>" (remembered on this computer) or -AssistantKey=. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assistant|Server")
	FString ApiKey;

	/** Seconds to wait for an answer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assistant|Server", meta=(ClampMin="1"))
	float RequestTimeout = 30.f;

	/** Ask the server for its /health when Play starts (a line in the log; nothing in the chat). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assistant|Server")
	bool bCheckServerOnBeginPlay = true;

	// ---------------------------------------------------------------- voice (hold the BOT logo)

	/** A recording stops and is sent by itself after this long. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assistant|Voice", meta=(ClampMin="1"))
	float MaxRecordSeconds = 15.f;

	/** Shorter recordings are thrown away (a hold that was let go straight away). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assistant|Voice", meta=(ClampMin="0"))
	float MinRecordSeconds = 0.4f;

	// ---------------------------------------------------------------- muscles and bones

	/** Write the description of a clicked muscle / bone into the chat box (instead of the old info popup). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assistant|Anatomy")
	bool bDescribeClickedParts = true;

	/** Open the chat window when a description is written into it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assistant|Anatomy")
	bool bOpenChatForDescriptions = true;

	/** "Show me the biceps": the camera stops this many times the muscle's size away from it, plus Focus Distance
	 *  Extra (cm), and never closer than Focus Min Distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assistant|Anatomy", meta=(ClampMin="0.5"))
	float FocusDistanceScale = 3.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assistant|Anatomy", meta=(ClampMin="0"))
	float FocusDistanceExtra = 55.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assistant|Anatomy", meta=(ClampMin="0"))
	float FocusMinDistance = 90.f;

	/** Seconds a camera move asked for by the assistant takes. 0 = jump. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assistant|Camera", meta=(ClampMin="0"))
	float CameraMoveSeconds = 0.7f;

	/** How long "read the pose data" waits for the first analysed frame after pose estimation was switched on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assistant|Pose", meta=(ClampMin="0"))
	float PoseReadTimeout = 10.f;

	// ---------------------------------------------------------------- events

	/** Fires for every answer (after its actions ran). Hook text-to-speech here. */
	UPROPERTY(BlueprintAssignable, Category="Assistant")
	FIHPCAssistantReply OnAssistantReply;

	// ---------------------------------------------------------------- calls

	/** Same as typing Text in the chat box and pressing Enter. */
	UFUNCTION(BlueprintCallable, Category="Assistant")
	void Ask(const FString& Text);

	/** Start recording the microphone (push-to-talk pressed). False if the microphone could not be opened. */
	UFUNCTION(BlueprintCallable, Category="Assistant|Voice")
	bool StartVoice();

	/** Stop recording and send what was said to the server (push-to-talk released). */
	UFUNCTION(BlueprintCallable, Category="Assistant|Voice")
	void StopVoiceAndSend();

	/** Stop recording and throw the recording away. */
	UFUNCTION(BlueprintCallable, Category="Assistant|Voice")
	void CancelVoice();

	UFUNCTION(BlueprintPure, Category="Assistant|Voice")
	bool IsRecording() const;

	/** Send a .wav file from disk as if it had been spoken (tests without a microphone). */
	UFUNCTION(BlueprintCallable, Category="Assistant|Voice")
	bool SendWavFile(const FString& FilePath);

	/** Run one action given as JSON text (the "actions" entries of docs/PROTOCOL.md), without the server.
	 *  Returns true if it worked; OutMessage says why not. */
	UFUNCTION(BlueprintCallable, Category="Assistant")
	bool RunActionJson(const FString& ActionJson, FString& OutMessage);

	/** The address requests go to: command line, else what "/server" remembered, else Server URL. No trailing slash. */
	UFUNCTION(BlueprintPure, Category="Assistant|Server")
	FString GetServerURL() const;

	/** Change the server address. bRemember: keep it for the next runs on this computer (GameUserSettings.ini). */
	UFUNCTION(BlueprintCallable, Category="Assistant|Server")
	void SetServerURL(const FString& NewURL, bool bRemember = true);

	UFUNCTION(BlueprintCallable, Category="Assistant|Server")
	void SetApiKey(const FString& NewKey, bool bRemember = true);

	/** GET /health; the answer is written into the chat box (bToChat) and the log. */
	UFUNCTION(BlueprintCallable, Category="Assistant|Server")
	void CheckServer(bool bToChat = true);

	/** True while a request is on its way or waiting its turn. */
	UFUNCTION(BlueprintPure, Category="Assistant")
	bool IsBusy() const { return bRequestInFlight || Queue.Num() > 0; }

	/** Writes the description of a muscle / bone into the chat box. */
	void DescribePart(const UAnatomyPickerComp* Picker, int32 PartId);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// ---- UI / pawn hook-up (IHPCAssistantComp.cpp)

	/** Finds the HUD (it is created after this component begins play) and binds the chat box and the BOT logo. */
	void EnsureHud();

	/** Binds to the possessed pawn's pickers (muscles, bones) and rebuilds the list of names. */
	void BindToPawn(APawn* Pawn);

	UFUNCTION() void HandleChatMessageSent(const FText& Text);
	UFUNCTION() void HandleBotVoiceHoldStarted();
	UFUNCTION() void HandleBotVoiceHoldEnded();
	UFUNCTION() void HandlePartSelectionChanged(int32 PartId);
	UFUNCTION() void HandlePoseEstimated(const FPoseFrameResult& Result);

	UCPP_UW_IHPCHud* GetHud() const { return Hud.Get(); }
	UCPP_UW_IHPCChatPanel* GetChat() const;
	UCPP_UW_IHPCSideBar* GetSideBar() const;
	APawn* GetAvatar() const;

	/** A bot bubble in the chat box (opens the chat first if bOpenChat). Kept for later if the HUD is not there yet. */
	void Say(const FString& Text, bool bOpenChat = false);
	void SetStatus(const FString& Text);

	/** Handles "/server ...", "/key ...", "/status", "/clear", "/help". True if Text was one of them. */
	bool HandleSlashCommand(const FString& Text);

	// ---- requests (IHPCAssistantComp.cpp)

	struct FRequest
	{
		bool bVoice = false;
		FString Text;                 // typed text, or the transcript when a voice request is repeated with the catalog
		TArray<uint8> Wav;            // voice only
		bool bForceCatalog = false;   // the server asked for the catalog
		bool bCatalogRetry = false;   // this is that repeat: never ask a second time
	};

	void Enqueue(FRequest&& Request);
	void SendNext();
	void HandleResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr Response, bool bConnected, int32 Serial);
	void HandleHealthResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr Response, bool bConnected, bool bToChat);
	void ApplyAnswer(const TSharedPtr<FJsonObject>& Answer, const FRequest& Request);
	void FinishRequest();
	void AddAuthHeader(const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& HttpRequest) const;
	FString GetApiKey() const;

	TArray<FRequest> Queue;
	FRequest Current;
	bool bRequestInFlight = false;
	int32 RequestSerial = 0;
	FString SessionId;

	// ---- context and catalog (IHPCAssistantContext.cpp)

	/** The avatar's state, sent with every request (docs/PROTOCOL.md, Context). */
	TSharedRef<FJsonObject> BuildContext() const;

	/** The request body shared by /v1/chat and /v1/voice. */
	FString BuildRequestBody(const FRequest& Request, bool& bOutIncludedCatalog) const;

	/** Rebuilds Parts and the catalog (the names of the avatar's muscles and bones) from the bound pickers. */
	void RebuildCatalog();

	static FString NormalizeName(const FString& Name);
	static FString NormalizeSide(const FString& Side);

	TArray<TWeakObjectPtr<UAnatomyPickerComp>> Pickers;
	/** The part we last described for each picker, 0 after it was cleared (to tell which picker an event comes from,
	 *  and not to describe the same selection twice). */
	TMap<TWeakObjectPtr<UAnatomyPickerComp>, int32> LastSelected;
	TWeakObjectPtr<APawn> BoundPawn;
	TArray<FIHPCPartRef> Parts;
	FString CatalogJson;          // {"parts":[...]}
	FString CatalogId;
	FString CatalogIdSent;        // the catalog the server already got in this session
	float RebindCooldown = 0.f;

	// ---- actions (IHPCAssistantActions.cpp)

	FIHPCActionResult RunAction(const TSharedPtr<FJsonObject>& Action);

	FIHPCActionResult DoSetLayer(const FJsonObject& Action);
	FIHPCActionResult DoTimeline(const FJsonObject& Action);
	FIHPCActionResult DoCamera(const FJsonObject& Action);
	FIHPCActionResult DoPoseEstimation(const FJsonObject& Action);
	FIHPCActionResult DoReadPoseData(const FJsonObject& Action);
	FIHPCActionResult DoShowPart(const FJsonObject& Action);
	FIHPCActionResult DoClearSelection(const FJsonObject& Action);
	FIHPCActionResult DoPlayAction(const FJsonObject& Action);
	FIHPCActionResult DoOpenPanel(const FJsonObject& Action);
	FIHPCActionResult DoSwitchAvatar(const FJsonObject& Action);

	/** The entry for Kind + Name + Side; with no side, the one nearer to the camera. Null if the avatar has none. */
	const FIHPCPartRef* FindPart(const FString& Kind, const FString& Name, const FString& Side) const;

	/** Which way the avatar faces and where its right is (world space, horizontal), and the middle of its hips. */
	bool GetAvatarFrame(FVector& OutOrigin, FVector& OutForward, FVector& OutRight) const;
	UCPP_MouseFunctionComp* GetCamera() const;

	/** The current angles as chat text. Empty if pose estimation has no person right now. */
	FString FormatPoseData(const TArray<FString>& OnlyTheseAngles) const;
	bool HasFreshPose() const;

	TArray<FIHPCActionResult> LastResults;

	/** While an answer's actions run, text they produce (descriptions, pose data) waits here so it lands in the
	 *  chat box after the reply, not before it. */
	bool bDeferMessages = false;
	TArray<FString> DeferredMessages;

	/** "read the pose data" asked before the first analysed frame: answered from the tick when one arrives. */
	bool bPoseReadPending = false;
	double PoseReadDeadline = 0.0;
	TArray<FString> PoseReadAngles;
	double LastPoseResultTime = 0.0;
	bool bPoseEventBound = false;

	// ---- voice (IHPCAssistantVoice.cpp)

	TSharedPtr<FIHPCVoiceRecorder> Recorder;
	double RecordStartTime = 0.0;

	// ---- UI

	TWeakObjectPtr<UCPP_UW_IHPCHud> Hud;
	float HudSearchCooldown = 0.f;
	TArray<FString> SayBeforeHud;   // bot text produced before the HUD existed
};

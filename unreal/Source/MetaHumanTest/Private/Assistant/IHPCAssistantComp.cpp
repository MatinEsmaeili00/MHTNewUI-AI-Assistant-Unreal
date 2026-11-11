// IHPCAssistantComp.cpp - the assistant behind the BOT chat: start-up, the chat box, the request queue, HTTP and
// what is done with the server's answer. See the header for the overview.

#include "Assistant/IHPCAssistantComp.h"

#include "IHPCAssistantVoice.h"
#include "../../CPP_PlayerController.h"    // module root (not on the include path)
#include "AnatomyPickerComp.h"
#include "IHPC_UI/CPP_UW_IHPCChatPanel.h"
#include "IHPC_UI/CPP_UW_IHPCHud.h"
#include "IHPC_UI/CPP_UW_IHPCSideBar.h"
#include "MuscleAtlas.h"
#include "PoseEstimation/PoseEstimationComp.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY(LogIHPCAssistant);

namespace
{
	// Where "/server" and "/key" are remembered: GameUserSettings.ini of this computer, not the project.
	const TCHAR* ConfigSection = TEXT("IHPCAssistant");

	void AppendUtf8(TArray<uint8>& Out, const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text);
		Out.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}

	TSharedPtr<FJsonObject> ParseJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Object))
		{
			Object.Reset();
		}
		return Object;
	}

	/** "set_layer layer=muscles" -> {"type":"set_layer","layer":"muscles"}. The console strips the quotes from real
	 *  JSON, so this plainer form is what can be typed there (and after "/do" in the chat box). Numbers and
	 *  true / false stay what they are. */
	FString ActionJsonFromWords(const TArray<FString>& Words)
	{
		const TSharedRef<FJsonObject> Action = MakeShared<FJsonObject>();
		for (int32 Index = 0; Index < Words.Num(); ++Index)
		{
			FString Key, Value;
			if (Index == 0 && !Words[Index].Contains(TEXT("=")))
			{
				Action->SetStringField(TEXT("type"), Words[Index]);
			}
			else if (Words[Index].Split(TEXT("="), &Key, &Value))
			{
				if (Value.IsNumeric())
				{
					Action->SetNumberField(Key, FCString::Atod(*Value));
				}
				else if (Value == TEXT("true") || Value == TEXT("false"))
				{
					Action->SetBoolField(Key, Value == TEXT("true"));
				}
				else
				{
					// Only a name has spaces ("Biceps brachii"); "zoom_in" and "pose_estimation" keep their underscore.
					Action->SetStringField(Key, Key == TEXT("name") ? Value.Replace(TEXT("_"), TEXT(" ")) : Value);
				}
			}
		}
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Action, Writer);
		return Out;
	}

	const TCHAR* HelpText =
		TEXT("Type or hold my logo and talk. For example:\n")
		TEXT("- show the muscles / the skeleton / the body\n")
		TEXT("- show me the left biceps, highlight the femur\n")
		TEXT("- play, pause, faster, half speed, go to the middle\n")
		TEXT("- zoom in, look from behind, reset the view\n")
		TEXT("- start pose estimation and read the data\n")
		TEXT("Commands: /server <address>, /key <text>, /status, /clear, /do <action> key=value");
}

UIHPCAssistantComp::UIHPCAssistantComp()
{
	PrimaryComponentTick.bCanEverTick = true;   // finds the HUD, feeds the microphone, waits for pose data
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UIHPCAssistantComp::BeginPlay()
{
	Super::BeginPlay();

	SessionId = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
	UE_LOG(LogIHPCAssistant, Log, TEXT("Assistant ready. Server: %s%s. Session %s"), *GetServerURL(),
		GetApiKey().IsEmpty() ? TEXT(" (no key)") : TEXT(" (with key)"), *SessionId);

	if (bCheckServerOnBeginPlay)
	{
		CheckServer(/*bToChat*/ false);
	}
}

void UIHPCAssistantComp::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Recorder.IsValid())
	{
		Recorder->Abort();   // closes the microphone
		Recorder.Reset();
	}
	BindToPawn(nullptr);
	Queue.Reset();
	Super::EndPlay(EndPlayReason);
}

void UIHPCAssistantComp::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 1. The HUD is created by the controller after this component begins play: look for it until it is there.
	if (!Hud.IsValid())
	{
		HudSearchCooldown -= DeltaTime;
		if (HudSearchCooldown <= 0.f)
		{
			HudSearchCooldown = 0.25f;
			EnsureHud();
		}
	}

	// 2. Follow the possessed pawn (Abby -> Beau -> ...): its pickers, its muscle and bone names.
	APawn* Avatar = GetAvatar();
	if (Avatar != BoundPawn.Get())
	{
		BindToPawn(Avatar);
	}
	else if (Avatar && Parts.Num() == 0)
	{
		// The pickers load their atlas in their own Begin Play, which may come after ours: try again now and then.
		RebindCooldown -= DeltaTime;
		if (RebindCooldown <= 0.f)
		{
			RebindCooldown = 1.f;
			BindToPawn(Avatar);
		}
	}

	// 3. Push-to-talk: move the microphone's audio into the recording, show its loudness on the logo.
	if (IsRecording())
	{
		const float Level = Recorder->Poll();
		if (UCPP_UW_IHPCSideBar* SideBar = GetSideBar())
		{
			SideBar->SetBotInputLevel(Level);
		}
		if (FPlatformTime::Seconds() - RecordStartTime >= MaxRecordSeconds)
		{
			StopVoiceAndSend();   // talked long enough: send what there is
		}
	}

	// 4. "Read the pose data" asked before the first analysed frame.
	if (bPoseReadPending)
	{
		const FString Text = FormatPoseData(PoseReadAngles);
		if (!Text.IsEmpty())
		{
			bPoseReadPending = false;
			Say(Text, /*bOpenChat*/ true);
		}
		else if (FPlatformTime::Seconds() >= PoseReadDeadline)
		{
			bPoseReadPending = false;
			Say(TEXT("Pose estimation is on, but it does not see a person yet. Make sure the whole avatar is in view, then ask me to read the data again."), true);
		}
	}
}

// ---------------------------------------------------------------------------
// HUD and pawn hook-up
// ---------------------------------------------------------------------------

APawn* UIHPCAssistantComp::GetAvatar() const
{
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	return Controller ? Controller->GetPawn() : nullptr;
}

UCPP_UW_IHPCChatPanel* UIHPCAssistantComp::GetChat() const
{
	return Hud.IsValid() ? Hud->GetChatPanel() : nullptr;
}

UCPP_UW_IHPCSideBar* UIHPCAssistantComp::GetSideBar() const
{
	return Hud.IsValid() ? Hud->GetSideBar() : nullptr;
}

void UIHPCAssistantComp::EnsureHud()
{
	const ACPP_PlayerController* Controller = Cast<ACPP_PlayerController>(GetOwner());
	UCPP_UW_IHPCHud* HudWidget = Controller ? Controller->GetMainHUD() : nullptr;
	if (!HudWidget)
	{
		return;
	}
	Hud = HudWidget;

	if (UCPP_UW_IHPCChatPanel* Chat = HudWidget->GetChatPanel())
	{
		// Typing + Enter (or the send button). The panel has already added the user's bubble.
		Chat->OnMessageSent.AddUniqueDynamic(this, &UIHPCAssistantComp::HandleChatMessageSent);
	}
	if (UCPP_UW_IHPCSideBar* SideBar = HudWidget->GetSideBar())
	{
		// Holding the BOT logo = push-to-talk.
		SideBar->OnBotVoiceHoldStarted.AddUniqueDynamic(this, &UIHPCAssistantComp::HandleBotVoiceHoldStarted);
		SideBar->OnBotVoiceHoldEnded.AddUniqueDynamic(this, &UIHPCAssistantComp::HandleBotVoiceHoldEnded);
	}
	if (UPoseEstimationComp* Pose = Controller->GetPoseEstimation())
	{
		if (!bPoseEventBound)
		{
			bPoseEventBound = true;
			Pose->OnPoseEstimated.AddUniqueDynamic(this, &UIHPCAssistantComp::HandlePoseEstimated);
		}
	}

	// Anything said before the chat box existed.
	TArray<FString> Waiting = MoveTemp(SayBeforeHud);
	SayBeforeHud.Reset();
	for (const FString& Text : Waiting)
	{
		Say(Text);
	}
}

void UIHPCAssistantComp::BindToPawn(APawn* Pawn)
{
	for (const TWeakObjectPtr<UAnatomyPickerComp>& Old : Pickers)
	{
		if (Old.IsValid())
		{
			Old->OnSelectionChanged.RemoveDynamic(this, &UIHPCAssistantComp::HandlePartSelectionChanged);
		}
	}
	Pickers.Reset();
	LastSelected.Reset();
	BoundPawn = Pawn;

	TArray<UAnatomyPickerComp*> Found;
	if (Pawn)
	{
		Pawn->GetComponents<UAnatomyPickerComp>(Found);
	}
	for (UAnatomyPickerComp* Picker : Found)
	{
		// One picker per pickable layer (muscles, bones). A click on a part = OnSelectionChanged.
		Picker->OnSelectionChanged.AddUniqueDynamic(this, &UIHPCAssistantComp::HandlePartSelectionChanged);
		Pickers.Add(Picker);
		LastSelected.Add(Picker, Picker->GetSelectedMuscle());   // what is already highlighted is not news
	}
	RebuildCatalog();
}

void UIHPCAssistantComp::HandlePoseEstimated(const FPoseFrameResult& Result)
{
	LastPoseResultTime = FPlatformTime::Seconds();
}

// ---------------------------------------------------------------------------
// The chat box
// ---------------------------------------------------------------------------

void UIHPCAssistantComp::Say(const FString& Text, bool bOpenChat)
{
	if (Text.TrimStartAndEnd().IsEmpty())
	{
		return;
	}
	if (bDeferMessages)
	{
		DeferredMessages.Add(Text);   // lands after the reply (ApplyAnswer)
		return;
	}
	UCPP_UW_IHPCChatPanel* Chat = GetChat();
	if (!Chat)
	{
		SayBeforeHud.Add(Text);
		UE_LOG(LogIHPCAssistant, Log, TEXT("(no chat box yet) %s"), *Text);
		return;
	}
	UCPP_UW_IHPCSideBar* SideBar = GetSideBar();
	if (bOpenChat && SideBar && !SideBar->IsChatOpen())
	{
		SideBar->SetChatOpen(true);
	}
	Chat->AddBotMessage(FText::FromString(Text));   // the side bar makes the BOT logo talk
}

void UIHPCAssistantComp::SetStatus(const FString& Text)
{
	if (UCPP_UW_IHPCChatPanel* Chat = GetChat())
	{
		Chat->SetStatus(FText::FromString(Text));
	}
}

void UIHPCAssistantComp::Ask(const FString& Text)
{
	const FString Clean = Text.TrimStartAndEnd();
	if (Clean.IsEmpty())
	{
		return;
	}
	EnsureHud();
	if (UCPP_UW_IHPCChatPanel* Chat = GetChat())
	{
		if (UCPP_UW_IHPCSideBar* SideBar = GetSideBar())
		{
			if (!SideBar->IsChatOpen())
			{
				SideBar->SetChatOpen(true);   // so the answer is seen
			}
		}
		Chat->SubmitMessage(FText::FromString(Clean));   // adds the bubble and comes back through HandleChatMessageSent
	}
	else
	{
		HandleChatMessageSent(FText::FromString(Clean));
	}
}

void UIHPCAssistantComp::HandleChatMessageSent(const FText& Text)
{
	const FString Clean = Text.ToString().TrimStartAndEnd();
	if (Clean.IsEmpty() || HandleSlashCommand(Clean))
	{
		return;
	}
	FRequest Request;
	Request.Text = Clean;
	Enqueue(MoveTemp(Request));
}

bool UIHPCAssistantComp::HandleSlashCommand(const FString& Text)
{
	if (!Text.StartsWith(TEXT("/")))
	{
		return false;
	}
	FString Command, Argument;
	if (!Text.Split(TEXT(" "), &Command, &Argument))
	{
		Command = Text;
	}
	Command = Command.ToLower();
	Argument = Argument.TrimStartAndEnd();

	if (Command == TEXT("/server"))
	{
		if (!Argument.IsEmpty())
		{
			SetServerURL(Argument, /*bRemember*/ true);
		}
		Say(FString::Printf(TEXT("AI server: %s"), *GetServerURL()));
		if (!Argument.IsEmpty())
		{
			CheckServer(/*bToChat*/ true);
		}
	}
	else if (Command == TEXT("/key"))
	{
		if (!Argument.IsEmpty())
		{
			SetApiKey(Argument.Equals(TEXT("none"), ESearchCase::IgnoreCase) ? FString() : Argument, /*bRemember*/ true);
		}
		Say(GetApiKey().IsEmpty() ? TEXT("No key is set.") : TEXT("A key is set."));
	}
	else if (Command == TEXT("/status"))
	{
		Say(FString::Printf(TEXT("AI server: %s. Asking it..."), *GetServerURL()));
		CheckServer(/*bToChat*/ true);
	}
	else if (Command == TEXT("/do"))
	{
		// A test command: run one action here, without the AI server. "/do camera op=view value=back"
		TArray<FString> Words;
		Argument.ParseIntoArrayWS(Words);
		FString Message;
		const bool bOk = Words.Num() > 0 && RunActionJson(ActionJsonFromWords(Words), Message);
		Say(bOk ? TEXT("Done.") : (Message.IsEmpty() ? TEXT("Usage: /do <action type> key=value ...") : *Message));
	}
	else if (Command == TEXT("/clear"))
	{
		if (UCPP_UW_IHPCChatPanel* Chat = GetChat())
		{
			Chat->ClearMessages();
		}
		SessionId = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();   // the server forgets the conversation too
		CatalogIdSent.Reset();
	}
	else
	{
		Say(HelpText);   // "/help" and anything else that starts with a slash
	}
	return true;
}

// ---------------------------------------------------------------------------
// Muscle / bone descriptions (no server involved)
// ---------------------------------------------------------------------------

void UIHPCAssistantComp::HandlePartSelectionChanged(int32 PartId)
{
	// The event does not say which picker it comes from, and one click can produce two events (the new selection,
	// and another layer's selection being cleared). The reporter is the picker whose selection is PartId and is not
	// the one we described for it last.
	UAnatomyPickerComp* Reporter = nullptr;
	for (const TWeakObjectPtr<UAnatomyPickerComp>& WeakPicker : Pickers)
	{
		UAnatomyPickerComp* Picker = WeakPicker.Get();
		if (!Picker)
		{
			continue;
		}
		const int32 Selected = Picker->GetSelectedMuscle();
		int32& Described = LastSelected.FindOrAdd(Picker);
		if (Selected == 0)
		{
			Described = 0;   // cleared: selecting the same part again is news again
		}
		else if (!Reporter && PartId > 0 && Selected == PartId && Described != PartId)
		{
			Reporter = Picker;
			Described = PartId;
		}
	}
	if (Reporter && bDescribeClickedParts)
	{
		DescribePart(Reporter, PartId);
	}
}

void UIHPCAssistantComp::DescribePart(const UAnatomyPickerComp* Picker, int32 PartId)
{
	FMuscleInfo Info;
	if (!Picker || !Picker->GetMuscleInfo(PartId, Info))
	{
		return;
	}

	// The same lines the info popup had, as one chat bubble:
	//   Biceps brachii (left)
	//   Upper arm
	//   What it does: ...
	//   When running: ...
	//   Watch out for: ...
	const FString Side = NormalizeSide(Info.Side.ToString());
	FString Text = Info.Name.ToString();
	if (Side != TEXT("center"))
	{
		Text += FString::Printf(TEXT(" (%s)"), *Side);
	}
	auto AddLine = [&Text](const TCHAR* Label, const FText& Value)
	{
		const FString Line = Value.ToString().TrimStartAndEnd();
		if (!Line.IsEmpty())
		{
			Text += FString::Printf(TEXT("\n%s%s"), Label, *Line);
		}
	};
	AddLine(TEXT(""), Info.Region);
	AddLine(TEXT("What it does: "), Info.Function);
	AddLine(TEXT("When running: "), Info.Running);
	AddLine(TEXT("Watch out for: "), Info.Injury);

	Say(Text, bOpenChatForDescriptions);
}

// ---------------------------------------------------------------------------
// Server address and key
// ---------------------------------------------------------------------------

FString UIHPCAssistantComp::GetServerURL() const
{
	FString URL;
	if (!FParse::Value(FCommandLine::Get(), TEXT("AssistantURL="), URL) || URL.IsEmpty())
	{
		if (!GConfig || !GConfig->GetString(ConfigSection, TEXT("ServerURL"), URL, GGameUserSettingsIni) || URL.IsEmpty())
		{
			URL = ServerURL;
		}
	}
	URL = URL.TrimStartAndEnd();
	while (URL.EndsWith(TEXT("/")))
	{
		URL.LeftChopInline(1);
	}
	if (!URL.IsEmpty() && !URL.Contains(TEXT("://")))
	{
		URL = TEXT("http://") + URL;   // "spark:8010" is enough
	}
	return URL;
}

void UIHPCAssistantComp::SetServerURL(const FString& NewURL, bool bRemember)
{
	const FString Clean = NewURL.TrimStartAndEnd();
	const bool bReset = Clean.Equals(TEXT("default"), ESearchCase::IgnoreCase) || Clean.Equals(TEXT("reset"), ESearchCase::IgnoreCase);
	if (bRemember && GConfig)
	{
		if (bReset)
		{
			GConfig->RemoveKey(ConfigSection, TEXT("ServerURL"), GGameUserSettingsIni);   // back to the project's value
		}
		else
		{
			GConfig->SetString(ConfigSection, TEXT("ServerURL"), *Clean, GGameUserSettingsIni);
		}
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	else if (!bReset)
	{
		ServerURL = Clean;
	}
	CatalogIdSent.Reset();   // a different server has not seen the names
	UE_LOG(LogIHPCAssistant, Log, TEXT("Server address is now %s"), *GetServerURL());
}

FString UIHPCAssistantComp::GetApiKey() const
{
	FString Key;
	if (!FParse::Value(FCommandLine::Get(), TEXT("AssistantKey="), Key) || Key.IsEmpty())
	{
		if (!GConfig || !GConfig->GetString(ConfigSection, TEXT("ApiKey"), Key, GGameUserSettingsIni) || Key.IsEmpty())
		{
			Key = ApiKey;
		}
	}
	return Key.TrimStartAndEnd();
}

void UIHPCAssistantComp::SetApiKey(const FString& NewKey, bool bRemember)
{
	const FString Clean = NewKey.TrimStartAndEnd();
	if (bRemember && GConfig)
	{
		if (Clean.IsEmpty())
		{
			GConfig->RemoveKey(ConfigSection, TEXT("ApiKey"), GGameUserSettingsIni);
		}
		else
		{
			GConfig->SetString(ConfigSection, TEXT("ApiKey"), *Clean, GGameUserSettingsIni);
		}
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	else
	{
		ApiKey = Clean;
	}
}

void UIHPCAssistantComp::AddAuthHeader(const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& HttpRequest) const
{
	const FString Key = GetApiKey();
	if (!Key.IsEmpty())
	{
		HttpRequest->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Key);
	}
}

void UIHPCAssistantComp::CheckServer(bool bToChat)
{
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(GetServerURL() + TEXT("/health"));
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetTimeout(8.f);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UIHPCAssistantComp::HandleHealthResponse, bToChat);
	HttpRequest->ProcessRequest();
}

void UIHPCAssistantComp::HandleHealthResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr Response, bool bConnected, bool bToChat)
{
	FString Text;
	const TSharedPtr<FJsonObject> Health = (bConnected && Response.IsValid() && Response->GetResponseCode() == 200)
		? ParseJson(Response->GetContentAsString()) : nullptr;
	if (Health.IsValid())
	{
		FString Llm, Stt;
		bool bLlmReady = false, bSttReady = false, bAuth = false;
		Health->TryGetStringField(TEXT("llm_model"), Llm);
		Health->TryGetStringField(TEXT("stt_model"), Stt);
		Health->TryGetBoolField(TEXT("llm_ready"), bLlmReady);
		Health->TryGetBoolField(TEXT("stt_ready"), bSttReady);
		Health->TryGetBoolField(TEXT("auth_required"), bAuth);
		Text = FString::Printf(TEXT("The AI server answers. Language model %s: %s. Speech recognition %s: %s.%s"),
			*Llm, bLlmReady ? TEXT("ready") : TEXT("not ready (direct commands still work)"),
			*Stt, bSttReady ? TEXT("ready") : TEXT("not ready (typing still works)"),
			(bAuth && GetApiKey().IsEmpty()) ? TEXT(" It wants a key: set it with /key <text>.") : TEXT(""));
		UE_LOG(LogIHPCAssistant, Log, TEXT("%s (%s)"), *Text, *GetServerURL());
	}
	else
	{
		Text = FString::Printf(TEXT("I can't reach the AI server at %s. Clicking a muscle or bone still shows its description. Set the address with /server <address>."), *GetServerURL());
		UE_LOG(LogIHPCAssistant, Warning, TEXT("AI server not reachable at %s (HTTP %d)"), *GetServerURL(), Response.IsValid() ? Response->GetResponseCode() : 0);
	}
	if (bToChat)
	{
		Say(Text);
	}
}

// ---------------------------------------------------------------------------
// Requests: one at a time, in the order they were made
// ---------------------------------------------------------------------------

void UIHPCAssistantComp::Enqueue(FRequest&& Request)
{
	Queue.Add(MoveTemp(Request));
	SetStatus(TEXT("Thinking..."));
	if (UCPP_UW_IHPCSideBar* SideBar = GetSideBar())
	{
		SideBar->SetBotState(EHoloAvatarState::Thinking, RequestTimeout + 2.f);   // the logo thinks until the answer
	}
	if (!bRequestInFlight)
	{
		SendNext();
	}
}

void UIHPCAssistantComp::SendNext()
{
	if (bRequestInFlight || Queue.Num() == 0)
	{
		return;
	}
	Current = MoveTemp(Queue[0]);
	Queue.RemoveAt(0);
	bRequestInFlight = true;

	bool bIncludedCatalog = false;
	const FString Body = BuildRequestBody(Current, bIncludedCatalog);
	if (bIncludedCatalog)
	{
		CatalogIdSent = CatalogId;   // taken back in HandleResponse if the request fails
	}
	const FString URL = GetServerURL() + (Current.bVoice ? TEXT("/v1/voice") : TEXT("/v1/chat"));

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(URL);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetTimeout(RequestTimeout);
	AddAuthHeader(HttpRequest);

	if (Current.bVoice)
	{
		// multipart/form-data: "request" (the JSON) and "audio" (the WAV file).
		const FString Boundary = TEXT("IHPCAssistant") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		TArray<uint8> Payload;
		AppendUtf8(Payload, FString::Printf(
			TEXT("--%s\r\nContent-Disposition: form-data; name=\"request\"\r\n\r\n"), *Boundary));
		AppendUtf8(Payload, Body);
		AppendUtf8(Payload, FString::Printf(
			TEXT("\r\n--%s\r\nContent-Disposition: form-data; name=\"audio\"; filename=\"speech.wav\"\r\nContent-Type: audio/wav\r\n\r\n"), *Boundary));
		Payload.Append(Current.Wav);
		AppendUtf8(Payload, FString::Printf(TEXT("\r\n--%s--\r\n"), *Boundary));
		HttpRequest->SetHeader(TEXT("Content-Type"), FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));
		HttpRequest->SetContent(MoveTemp(Payload));
	}
	else
	{
		HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
		HttpRequest->SetContentAsString(Body);
	}

	UE_LOG(LogIHPCAssistant, Log, TEXT("-> %s %s%s"), *URL,
		Current.bVoice ? *FString::Printf(TEXT("(%d bytes of audio)"), Current.Wav.Num()) : *FString::Printf(TEXT("\"%s\""), *Current.Text),
		bIncludedCatalog ? TEXT(" + catalog") : TEXT(""));

	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UIHPCAssistantComp::HandleResponse, ++RequestSerial);
	HttpRequest->ProcessRequest();
}

void UIHPCAssistantComp::HandleResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr Response, bool bConnected, int32 Serial)
{
	if (Serial != RequestSerial || !bRequestInFlight)
	{
		return;   // an answer to a request that was given up on
	}
	const FRequest Request = MoveTemp(Current);
	Current = FRequest();

	if (!bConnected || !Response.IsValid())
	{
		CatalogIdSent.Reset();
		Queue.Reset();   // no point in sending the rest to a server that is not there
		Say(FString::Printf(TEXT("I can't reach the AI server at %s. Check the network, or set the address with /server <address>."), *GetServerURL()));
		FinishRequest();
		return;
	}

	const int32 Code = Response->GetResponseCode();
	const TSharedPtr<FJsonObject> Answer = ParseJson(Response->GetContentAsString());
	if (Code != 200 || !Answer.IsValid())
	{
		CatalogIdSent.Reset();
		FString Detail;
		if (Answer.IsValid())
		{
			Answer->TryGetStringField(TEXT("detail"), Detail);
		}
		UE_LOG(LogIHPCAssistant, Warning, TEXT("AI server: HTTP %d: %s"), Code, *Response->GetContentAsString().Left(500));
		if (Code == 401 || Code == 403)
		{
			Say(TEXT("The AI server did not accept my key. Set it with /key <text>."));
		}
		else if (!Detail.IsEmpty())
		{
			Say(FString::Printf(TEXT("The AI server could not do that: %s"), *Detail));
		}
		else
		{
			Say(FString::Printf(TEXT("The AI server answered with an error (%d)."), Code));
		}
		FinishRequest();
		return;
	}

	ApplyAnswer(Answer, Request);
	FinishRequest();
}

void UIHPCAssistantComp::FinishRequest()
{
	bRequestInFlight = false;
	if (Queue.Num() > 0)
	{
		SendNext();
		return;
	}
	SetStatus(FString());   // "Thinking..." goes away with the last answer
}

void UIHPCAssistantComp::ApplyAnswer(const TSharedPtr<FJsonObject>& Answer, const FRequest& Request)
{
	FString Transcript, Reply, Source;
	bool bNeedCatalog = false;
	double LatencyMs = 0.0;
	Answer->TryGetStringField(TEXT("transcript"), Transcript);
	Answer->TryGetStringField(TEXT("reply"), Reply);
	Answer->TryGetStringField(TEXT("source"), Source);
	Answer->TryGetBoolField(TEXT("need_catalog"), bNeedCatalog);
	Answer->TryGetNumberField(TEXT("latency_ms"), LatencyMs);
	Transcript = Transcript.TrimStartAndEnd();
	Reply = Reply.TrimStartAndEnd();

	UCPP_UW_IHPCChatPanel* Chat = GetChat();
	// What was said aloud becomes the user's bubble (typed text already has one).
	if (Request.bVoice && !Transcript.IsEmpty() && Chat)
	{
		Chat->AddUserMessage(FText::FromString(Transcript));
	}

	// The server does not know this avatar's muscle and bone names: send the same words again, with the names.
	if (bNeedCatalog && !Request.bCatalogRetry && !CatalogId.IsEmpty())
	{
		FRequest Retry;
		Retry.Text = Transcript.IsEmpty() ? Request.Text : Transcript;   // text, so a voice clip is not uploaded twice
		Retry.bForceCatalog = true;
		Retry.bCatalogRetry = true;
		if (!Retry.Text.IsEmpty())
		{
			UE_LOG(LogIHPCAssistant, Log, TEXT("<- the server asked for the catalog; repeating \"%s\" with it"), *Retry.Text);
			Queue.Insert(MoveTemp(Retry), 0);
			return;
		}
	}

	// Run the actions, in order. Text they produce (descriptions, pose data) is held back until the reply is in the chat.
	TArray<FIHPCActionResult> Results;
	const TArray<TSharedPtr<FJsonValue>>* Actions = nullptr;
	bDeferMessages = true;
	DeferredMessages.Reset();
	if (Answer->TryGetArrayField(TEXT("actions"), Actions) && Actions)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Actions)
		{
			const TSharedPtr<FJsonObject>* ActionObject = nullptr;
			if (Value.IsValid() && Value->TryGetObject(ActionObject) && ActionObject)
			{
				Results.Add(RunAction(*ActionObject));
			}
		}
	}
	bDeferMessages = false;
	LastResults = Results;

	// The reply was written as if everything would work. Say what did not.
	TArray<FString> Failures;
	for (const FIHPCActionResult& Result : Results)
	{
		if (!Result.bOk && !Result.Message.IsEmpty())
		{
			Failures.AddUnique(Result.Message);
		}
	}
	FString Text = Reply;
	if (Failures.Num() > 0)
	{
		const bool bAllFailed = Failures.Num() >= Results.Num();
		Text = (bAllFailed || Reply.IsEmpty()) ? FString::Join(Failures, TEXT(" ")) : Reply + TEXT("\n") + FString::Join(Failures, TEXT(" "));
	}

	UE_LOG(LogIHPCAssistant, Log, TEXT("<- [%s %.0f ms] heard \"%s\" | %d action(s), %d failed | \"%s\""),
		*Source, LatencyMs, *Transcript, Results.Num(), Failures.Num(), *Text.Replace(TEXT("\n"), TEXT(" / ")));

	SetStatus(FString());
	Say(Text, /*bOpenChat*/ true);
	TArray<FString> Extra = MoveTemp(DeferredMessages);
	DeferredMessages.Reset();
	for (const FString& Message : Extra)
	{
		Say(Message, true);
	}
	if (Text.IsEmpty() && Extra.Num() == 0)
	{
		if (UCPP_UW_IHPCSideBar* SideBar = GetSideBar())
		{
			SideBar->SetBotState(SideBar->IsChatOpen() ? EHoloAvatarState::Listening : EHoloAvatarState::Idle);
		}
	}

	OnAssistantReply.Broadcast(Transcript.IsEmpty() ? Request.Text : Transcript, Text);
}

bool UIHPCAssistantComp::RunActionJson(const FString& ActionJson, FString& OutMessage)
{
	const TSharedPtr<FJsonObject> Action = ParseJson(ActionJson);
	if (!Action.IsValid())
	{
		OutMessage = TEXT("That is not a JSON object.");
		return false;
	}
	const FIHPCActionResult Result = RunAction(Action);
	OutMessage = Result.Message;
	return Result.bOk;
}

// ---------------------------------------------------------------------------
// Voice: hold the BOT logo
// ---------------------------------------------------------------------------

bool UIHPCAssistantComp::IsRecording() const
{
	return Recorder.IsValid() && Recorder->IsRecording();
}

void UIHPCAssistantComp::HandleBotVoiceHoldStarted()
{
	if (!StartVoice())
	{
		if (UCPP_UW_IHPCSideBar* SideBar = GetSideBar())
		{
			SideBar->SetBotVoiceActive(false);   // no microphone: the logo goes back to normal
		}
	}
}

void UIHPCAssistantComp::HandleBotVoiceHoldEnded()
{
	StopVoiceAndSend();
}

bool UIHPCAssistantComp::StartVoice()
{
	if (IsRecording())
	{
		return true;
	}
	if (!Recorder.IsValid())
	{
		Recorder = MakeShared<FIHPCVoiceRecorder>();
	}
	FString Error;
	if (!Recorder->Start(Error))
	{
		UE_LOG(LogIHPCAssistant, Warning, TEXT("Microphone: %s"), *Error);
		Say(Error, /*bOpenChat*/ true);
		return false;
	}
	RecordStartTime = FPlatformTime::Seconds();

	if (UCPP_UW_IHPCSideBar* SideBar = GetSideBar())
	{
		if (!SideBar->IsChatOpen())
		{
			SideBar->SetChatOpen(true);   // so "Listening..." and the answer are seen
		}
		SideBar->SetBotVoiceActive(true);  // red (already so when the logo is being held)
	}
	SetStatus(TEXT("Listening..."));
	return true;
}

void UIHPCAssistantComp::CancelVoice()
{
	if (Recorder.IsValid())
	{
		Recorder->Abort();
	}
	if (UCPP_UW_IHPCSideBar* SideBar = GetSideBar())
	{
		SideBar->SetBotVoiceActive(false);
	}
	if (!IsBusy())
	{
		SetStatus(FString());
	}
}

void UIHPCAssistantComp::StopVoiceAndSend()
{
	if (!IsRecording())
	{
		return;
	}
	const float Peak = Recorder->GetPeak();
	float Seconds = 0.f;
	TArray<uint8> Wav = Recorder->Stop(Seconds);
	if (UCPP_UW_IHPCSideBar* SideBar = GetSideBar())
	{
		SideBar->SetBotVoiceActive(false);
	}
	if (!IsBusy())
	{
		SetStatus(FString());
	}
	UE_LOG(LogIHPCAssistant, Log, TEXT("Microphone: %.2f s recorded, %d bytes, loudest sample %.3f"), Seconds, Wav.Num(), Peak);

	if (Seconds < MinRecordSeconds || Wav.Num() <= 44)
	{
		Say(TEXT("Keep my logo held while you speak, then let go."), true);
		return;
	}
	if (Peak < 0.0005f)
	{
		Say(TEXT("The microphone gave only silence. Check that it is not muted and that this app may use it (on a Mac: System Settings, Privacy & Security, Microphone)."), true);
		return;
	}

	FRequest Request;
	Request.bVoice = true;
	Request.Wav = MoveTemp(Wav);
	Enqueue(MoveTemp(Request));
}

bool UIHPCAssistantComp::SendWavFile(const FString& FilePath)
{
	FRequest Request;
	Request.bVoice = true;
	if (!FFileHelper::LoadFileToArray(Request.Wav, *FilePath) || Request.Wav.Num() <= 44)
	{
		UE_LOG(LogIHPCAssistant, Warning, TEXT("SendWavFile: could not read %s"), *FilePath);
		return false;
	}
	if (UCPP_UW_IHPCSideBar* SideBar = GetSideBar())
	{
		if (!SideBar->IsChatOpen())
		{
			SideBar->SetChatOpen(true);
		}
	}
	Enqueue(MoveTemp(Request));
	return true;
}

// ---------------------------------------------------------------------------
// Console commands
// ---------------------------------------------------------------------------

namespace
{
	UIHPCAssistantComp* FindAssistant(UWorld* World)
	{
		const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		return Controller ? Controller->FindComponentByClass<UIHPCAssistantComp>() : nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs AskCommand(
		TEXT("Assistant.Ask"),
		TEXT("Assistant.Ask <text>: the same as typing <text> in the BOT chat box."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UIHPCAssistantComp* Assistant = FindAssistant(World))
			{
				Assistant->Ask(FString::Join(Args, TEXT(" ")));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs DoCommand(
		TEXT("Assistant.Do"),
		TEXT("Assistant.Do <type> [key=value ...]: run one assistant action without the AI server, e.g. ")
		TEXT("\"Assistant.Do set_layer layer=muscles\", \"Assistant.Do show_part kind=muscle name=Biceps_brachii side=left\" ")
		TEXT("(an underscore in a name is a space)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UIHPCAssistantComp* Assistant = FindAssistant(World))
			{
				FString Message;
				const FString Joined = FString::Join(Args, TEXT(" "));
				// Real JSON works when it survives the console (no quotes needed inside: keys and values without spaces).
				const bool bOk = Assistant->RunActionJson(Joined.StartsWith(TEXT("{")) && Joined.Contains(TEXT("\"")) ? Joined : ActionJsonFromWords(Args), Message);
				UE_LOG(LogIHPCAssistant, Display, TEXT("Assistant.Do: %s%s%s"), bOk ? TEXT("ok") : TEXT("failed"), Message.IsEmpty() ? TEXT("") : TEXT(": "), *Message);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs ServerCommand(
		TEXT("Assistant.Server"),
		TEXT("Assistant.Server [host:port]: show, or change and remember, the AI server address (without http://: the console ")
		TEXT("cuts everything after \"//\"). \"default\" = the project's value."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UIHPCAssistantComp* Assistant = FindAssistant(World))
			{
				// The console reads "//" as the start of a comment, so "http://spark:8010" arrives as "http:".
				if (Args.Num() > 0 && (Args[0].Equals(TEXT("http:"), ESearchCase::IgnoreCase) || Args[0].Equals(TEXT("https:"), ESearchCase::IgnoreCase)))
				{
					UE_LOG(LogIHPCAssistant, Warning, TEXT("The console cuts everything after \"//\". Give the address without http://, e.g. Assistant.Server spark:8010 (or use /server in the chat box)."));
					return;
				}
				if (Args.Num() > 0)
				{
					Assistant->SetServerURL(Args[0], true);
				}
				UE_LOG(LogIHPCAssistant, Display, TEXT("AI server: %s"), *Assistant->GetServerURL());
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs KeyCommand(
		TEXT("Assistant.Key"),
		TEXT("Assistant.Key [text]: change and remember the key sent to the AI server. \"none\" = no key."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UIHPCAssistantComp* Assistant = FindAssistant(World))
			{
				if (Args.Num() > 0)
				{
					Assistant->SetApiKey(Args[0].Equals(TEXT("none"), ESearchCase::IgnoreCase) ? FString() : Args[0], true);
				}
				UE_LOG(LogIHPCAssistant, Display, TEXT("AI server key: changed with Assistant.Key <text>"));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs StatusCommand(
		TEXT("Assistant.Status"),
		TEXT("Assistant.Status: ask the AI server for its /health (the answer goes to the chat box and the log)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UIHPCAssistantComp* Assistant = FindAssistant(World))
			{
				Assistant->CheckServer(true);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs TalkCommand(
		TEXT("Assistant.Talk"),
		TEXT("Assistant.Talk 1 | 0: start / stop-and-send a microphone recording (the same as holding the BOT logo)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UIHPCAssistantComp* Assistant = FindAssistant(World))
			{
				const bool bStart = Args.Num() > 0 ? Args[0].ToBool() : !Assistant->IsRecording();
				bStart ? (void)Assistant->StartVoice() : Assistant->StopVoiceAndSend();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs HoldLogoCommand(
		TEXT("Assistant.HoldLogo"),
		TEXT("Assistant.HoldLogo [seconds]: TEST - press the BOT logo, hold it (default 2 s) and let go, through Slate's own pointer events."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const ACPP_PlayerController* Controller = World ? Cast<ACPP_PlayerController>(World->GetFirstPlayerController()) : nullptr;
			UCPP_UW_IHPCHud* HudWidget = Controller ? Controller->GetMainHUD() : nullptr;
			if (UCPP_UW_IHPCSideBar* SideBar = HudWidget ? HudWidget->GetSideBar() : nullptr)
			{
				SideBar->SimulateBotHold(Args.Num() > 0 ? FCString::Atof(*Args[0]) : 2.f);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs SendWavCommand(
		TEXT("Assistant.SendWav"),
		TEXT("Assistant.SendWav <file.wav>: send a recording from disk to the AI server as if it had been spoken."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UIHPCAssistantComp* Assistant = FindAssistant(World))
			{
				Assistant->SendWavFile(FString::Join(Args, TEXT(" ")));
			}
		}));
}

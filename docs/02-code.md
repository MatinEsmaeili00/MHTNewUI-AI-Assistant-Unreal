# 02 - The code

Everything is C++ in the project module `MetaHumanTest` and one function set in the plugin
`MetaHumanAnatomyLayers`. No Widget Blueprint, level or other asset was changed: the chat bubbles, the status
line and the red logo are made in code, and the assistant component is a default subobject of the C++ player
controller, so `BP_PlayerController` gets it by itself.

The files are in [`unreal/`](../unreal) with the project's own paths, and as one patch in
[`patches/`](../patches).

## New files

| File | What is in it |
|---|---|
| `Source/MetaHumanTest/Public/Assistant/IHPCAssistantComp.h` | `UIHPCAssistantComp`: the settings, the public calls, the overview comment |
| `Source/MetaHumanTest/Private/Assistant/IHPCAssistantComp.cpp` | start-up, hook-up to the HUD and the pawn, the chat box, slash commands, the request queue, HTTP, what is done with an answer, console commands |
| `Source/MetaHumanTest/Private/Assistant/IHPCAssistantContext.cpp` | the `context` and the `catalog` of the protocol |
| `Source/MetaHumanTest/Private/Assistant/IHPCAssistantActions.cpp` | one function per action type |
| `Source/MetaHumanTest/Private/Assistant/IHPCAssistantVoice.h/.cpp` | `FIHPCVoiceRecorder`: microphone to WAV |

## Changed files

| File | Change |
|---|---|
| `CPP_PlayerController.h/.cpp` | creates the `Assistant` component next to `PoseEstimation`; `GetAssistant()`, `GetCurrentPawnIndex()` |
| `IHPC_UI/CPP_UW_IHPCSideBar.h/.cpp` | hold the BOT logo = push-to-talk: hold timer, the red look, `OnBotVoiceHoldStarted` / `OnBotVoiceHoldEnded`, `SetBotVoiceActive`, `SetBotInputLevel`, `SimulateBotHold` (test) |
| `IHPC_UI/CPP_UW_IHPCChatPanel.h/.cpp` | `SetStatus` / `ClearStatus`: the temporary "Listening..." / "Thinking..." line |
| `CPP_UW_MuscleInfo.h/.cpp` | `bShowPopupOnSelect` (off): a click no longer opens the info popup; the hover tooltip stays |
| `CPP_MouseFunctionComp.h/.cpp` | scripted camera moves that glide: `MoveViewTo`, `GlideToDistance`, `GlideOrbitBy`, `GlideToStartView` |
| `IHPC_UI/CPP_UW_IHPCActionsPanel.h/.cpp` | `GetAvailableActions()`, `GetHighlightedAction()` |
| `IHPC_UI/CPP_UW_IHPCHud.h` | `GetActionsPanel()` |
| `Plugins/MetaHumanAnatomyLayers/.../AnatomyPickerComp.h/.cpp` | `SelectById`, `GetAtlas`, `GetMuscleWorldLocation`; the glow of a selection made during a layer change now appears when the layer arrives |
| `MetaHumanTest.Build.cs` | modules `HTTP`, `Json`, `AudioCaptureCore`, `AudioCapture` |
| `MetaHumanTest.uproject` | plugin `AudioCapture` listed (it is on by default; listed because the module depends on it) |

## How one request travels

```
chat box: Enter                         hold the BOT logo ... let go
   UCPP_UW_IHPCChatPanel::SubmitMessage     UCPP_UW_IHPCSideBar: HandleBotPressed -> timer (0.3 s)
   adds the user's bubble                    -> HandleBotHoldTimer: red, OnBotVoiceHoldStarted
   OnMessageSent                             UIHPCAssistantComp::StartVoice (microphone on, "Listening...")
        │                                    release -> OnBotVoiceHoldEnded -> StopVoiceAndSend (WAV)
        ▼                                         │
UIHPCAssistantComp::HandleChatMessageSent         │
   "/..." -> HandleSlashCommand (local)           │
   else Enqueue(text)  ◄──────────────────────────┘ Enqueue(voice)
        │   "Thinking...", the logo thinks
        ▼
   SendNext: BuildRequestBody = { protocol, session_id, text, context, catalog_id [, catalog] }
             POST /v1/chat (JSON)   or   POST /v1/voice (multipart: "request" + "audio")
        ▼
   HandleResponse -> ApplyAnswer:
        voice: the transcript becomes the user's bubble
        need_catalog: the same words are sent again with the catalog (once)
        every action -> RunAction -> Do...()  -> { type, ok, message }   (kept as context.last_results)
        the reply goes into the chat (plus one line per failed action)
        text the actions produced (descriptions, pose data) follows the reply
   FinishRequest -> the next request in the queue, or the status line is cleared
```

Requests are sent one at a time, in order. Two quick commands ("pause", "zoom in") can therefore never be
applied in the wrong order, and an answer never arrives for a state the avatar has already left.

## The request: context and catalog (`IHPCAssistantContext.cpp`)

`BuildContext()` reads the avatar's state from the things that own it:

| Context field | Read from |
|---|---|
| `avatar` | the possessed pawn's class name, `ACPP_PlayerController::GetCurrentPawnIndex`, `SwitchablePawnClasses` |
| `layer` | `UCPP_UW_IHPCLayerSwitcher::GetSelectedLayer` (only if the pawn has anatomy layers) |
| `timeline` | `UAnimationTimelineComp` (playing, speed, time, cycle length), `UCPP_UW_IHPCTimeline` (speed steps, recording) |
| `action` | `UCPP_UW_IHPCActionsPanel::GetHighlightedAction` / `GetAvailableActions` |
| `panel`, `chat_open` | `UCPP_UW_IHPCHud::GetSelectedSideTab`, `UCPP_UW_IHPCSideBar::IsChatOpen` |
| `camera` | `UCPP_MouseFunctionComp` (distance, limits, moved) |
| `pose` | `UPoseEstimationComp` (status, overlay, panel, recording, rate) and the latest result's valid angles |
| `selected_part` | the pickers' selected ID |
| `last_results` | what the previous answer's actions did |

`RebuildCatalog()` runs when the possessed pawn changes. It walks every `UAnatomyPickerComp` on the pawn (one
per pickable layer), reads its atlas (`GetAtlas`), and keeps one `FIHPCPartRef` per atlas entry (picker, ID, kind,
name, side). A picker whose layer name contains "Skel" or "Bone" holds bones; any other holds muscles. The
catalog sent to the server has one entry per kind + name with its sides; its ID is the first 12 characters of
the MD5 of that JSON, so it only changes when the names do. The catalog goes along with the first request of a
session (or after a pawn switch) and whenever the server answers `need_catalog`.

## The actions (`IHPCAssistantActions.cpp`)

`RunAction` reads `type` and calls one function. Each returns `{type, ok, message}`; `message` is a sentence
the user can read. Unknown types fail with a message and the rest of the list still runs.

| Action | Function | It calls |
|---|---|---|
| `set_layer` | `DoSetLayer` | `UCPP_UW_IHPCLayerSwitcher::SelectLayer` (the side bar's buttons, so the highlight follows) |
| `timeline` | `DoTimeline` | `UIHPCUILibrary` play / pause / seek, `UCPP_UW_IHPCTimeline` speed steps / bookmark / record, `UAnimationTimelineComp::StepSeconds` |
| `camera` | `DoCamera` | `UCPP_MouseFunctionComp::GlideToDistance` / `GlideOrbitBy` / `MoveViewTo` / `GlideToStartView` |
| `pose_estimation` | `DoPoseEstimation` | `UPoseEstimationComp::SetEstimationEnabled` / `SetOverlayVisible` / `SetDataPanelOpen` |
| `read_pose_data` | `DoReadPoseData` | `FormatPoseData`; if no frame was analysed yet, the tick answers when one arrives (10 s limit) |
| `show_part` | `DoShowPart` | `FindPart`, `SelectLayer`, `UAnatomyPickerComp::SelectById`, `PickViewSide`, `MoveViewTo` |
| `clear_selection` | `DoClearSelection` | `UAnatomyPickerComp::ClearSelection` on every picker |
| `play_action` | `DoPlayAction` | `UCPP_UW_IHPCActionsPanel::SelectAction` |
| `open_panel` | `DoOpenPanel` | `UCPP_UW_IHPCHud::SelectSideTab` |
| `switch_avatar` | `DoSwitchAvatar` | `ACPP_PlayerController::SwitchPawn` |

Details that are not obvious:

- **Numbers and flags are read loosely.** The answer may come from a language model: `"2"` is accepted for `2`,
  `"true"` for `true`.
- **Limits are reported, not ignored.** "Faster" at x3 fails with "It is already at the fastest speed (x3)",
  "zoom in" at the closest distance with "The camera is already as close as it goes".
- **`show_part` without a side** takes the side nearer to the camera (`FindPart`).
- **Which side to look from** (`PickViewSide`). A muscle lies on one side of its limb: the biceps in front of
  the upper arm, the hamstrings behind the thigh. The function finds the nearest limb line (pelvis to neck,
  upper arm, forearm, thigh, shin, foot; MetaHuman bone names) and looks from the side of it the part is on.
  A part that faces the middle of the body (the adductors) is looked at from the front or back instead, so the
  other leg is not in the way. A part on the limb line itself (a bone) is looked at from the front, turned
  towards its side of the body. The avatar's forward and right come from its two upper-arm bones
  (`GetAvatarFrame`), because the avatars are plain pawns whose actor rotation says nothing about where they face.
  A first attempt used the picking samples' offsets from the bone they are attached to; that failed because the
  muscle mesh is skinned to MetaHuman helper bones that sit on the muscles themselves.
- **`play_action` on the card that is already on does nothing.** Selecting it again would switch it off (a
  second click on a card means "back to Idle").
- **Descriptions come from the selection event.** `SelectById` fires the picker's `OnSelectionChanged`, the same
  event a mouse click fires, and `HandlePartSelectionChanged` writes the description. While an answer's actions
  run, such text is held back (`bDeferMessages`) so it lands after the reply.

## The camera moves (`CPP_MouseFunctionComp`)

The component did orbit / pan / zoom from the mouse, each a jump of the spring arm. The assistant needed moves
that glide. `BeginViewMove` remembers the spring arm's rotation, location and length, the values to reach and a
duration; a timer (`StepViewMove`) interpolates with ease in / out from the wall clock and takes the short way
round for the rotation. Any user orbit, pan or zoom calls `CancelViewMove`, so the user always wins. A timer is
used instead of a component tick because the component never ticked, and Blueprint pawns made before this keep
their saved tick settings.

## Push-to-talk (`CPP_UW_IHPCSideBar`, `IHPCAssistantVoice`)

- `HandleBotPressed` starts a timer of `BotHoldSeconds` (0.3). If the logo is still pressed when it fires
  (`HandleBotHoldTimer`), the hold is push-to-talk: the logo turns red and `OnBotVoiceHoldStarted` fires.
- `HandleBotReleased` ends the hold (`OnBotVoiceHoldEnded`). `UButton` fires `OnReleased` before `OnClicked`, so
  the flag `bBotSwallowClick` set by the hold makes `HandleBotClicked` skip the chat toggle once.
- `NativeTick` also ends the hold when the button is no longer pressed (the pointer slid off).
- The red look: `ApplyBotLook` first blends the closed and chat-open looks as before, then blends the colours
  and the glow towards `BotVoiceLook` by `BotVoiceBlend`. The shape of the hologram is left alone.
- `FIHPCVoiceRecorder` wraps the engine's `Audio::FAudioCaptureSynth`: open the default input, poll it from the
  component's tick, close it on release. The result is one 16-bit mono WAV at the device's sample rate, with
  quiet recordings amplified up to 8 times. The loudest sample is kept: a recording that is pure silence is
  reported to the user (muted, or the system did not allow the microphone) instead of being uploaded.

## Settings

Select the `Assistant` component in `BP_PlayerController`:

| Setting | Default | |
|---|---|---|
| Server URL | `http://127.0.0.1:8010` | overridden by `/server` (remembered) and `-AssistantURL=` |
| Api Key | empty | overridden by `/key` (remembered) and `-AssistantKey=` |
| Request Timeout | 30 s | |
| Max / Min Record Seconds | 15 / 0.4 | |
| Describe Clicked Parts | on | off = clicks write nothing into the chat |
| Open Chat For Descriptions | on | |
| Focus Distance Scale / Extra / Min | 3.2 / 55 / 90 | how far the camera stops from a shown part |
| Camera Move Seconds | 0.7 | |
| Pose Read Timeout | 10 s | |

On the side bar (`WBP_IHPC_SideBar`, Class Defaults, IHPC | Bot | Voice): `Bot Hold Seconds`, `Bot Voice Look`,
`Bot Voice Hologram Scale`, `Bot Voice Blend Time`, `Bot Voice Tint`.
On `WB_MuscleInfo`: `Show Popup On Select` brings the old popup back.

`/server` and `/key` are stored in the `[IHPCAssistant]` section of `GameUserSettings.ini` of the computer the
app runs on, not in the project.

## Adding an action

1. Describe it in `docs/PROTOCOL.md` (both repositories).
2. Unreal: add `DoSomething(const FJsonObject&)` to `IHPCAssistantActions.cpp` and the header, and one line in
   `RunAction`. Return `Ok(Type)` or `Fail(Type, "a sentence for the user")`.
3. If the server needs to know something new about the avatar to choose it, add the field in `BuildContext`.
4. Server: add the rule, the validation and (if needed) the line in the language model's prompt.
5. Test without the server first: `/do something key=value` in the chat box, or `Assistant.Do ...` in the console.

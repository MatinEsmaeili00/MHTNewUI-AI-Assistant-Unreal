// CPP_UW_IHPCChatPanel.h
// -----------------------------------------------------------------------------
// The assistant chat window that opens next to the BOT avatar at the top of the side bar.
// Figma: component "BOT" (page "FINAL FESTIVAL"), Property 1 = Variant2:
//
//   (bot)  +-----------------------------------+
//          | ( Hello, how may I assist you? )  |   <- bot message bubble, white 27 %, fully rounded
//          |                                   |
//          |                                   |
//          | ( Type here              )  (>)   |   <- text box (white 13 %) + round send button
//          +-----------------------------------+      window: rgba(12,39,61,0.72), radius 18, 279 x 286
//
// What it does:
//   * Shows the greeting when it first opens.
//   * Typing + Enter (or the send button) adds the user's message as a bubble on the right and fires
//     OnMessageSent - that is where a chatbot / LLM backend plugs in (Blueprint or C++). The backend answers by
//     calling AddBotMessage. The project's backend is UIHPCAssistantComp (Assistant/IHPCAssistantComp.h, on the player
//     controller): it sends the message to the AI server and runs the actions that come back. Without it nothing
//     answers; set PlaceholderReply to have a fixed reply while testing.
//   * SetStatus shows a temporary line at the bottom ("Listening...", "Thinking...") while the assistant is busy.
//   * The muscle / bone descriptions are shown here too (a click on a muscle adds a bot bubble), not in a popup.
//
// Widget Blueprint: /Game/UserInterface/IHPC/Widgets/WBP_IHPC_ChatPanel
//   Needs a ScrollBox "MessageList", an EditableTextBox "MessageInput" and a Button "SendButton".
//   Bubbles are made in code (rounded Border + TextBlock), styled by the properties below.
// -----------------------------------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/SlateEnums.h"
#include "IHPC_UI/IHPCTypes.h"
#include "CPP_UW_IHPCChatPanel.generated.h"

class UButton;
class UEditableTextBox;
class UScrollBox;

UCLASS(Abstract)
class METAHUMANTEST_API UCPP_UW_IHPCChatPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	UCPP_UW_IHPCChatPanel(const FObjectInitializer& ObjectInitializer);

	// ---------------------------------------------------------------- text

	/** First bot bubble (Figma: "Hello, how may I assist you?"). Empty = no greeting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Chat")
	FText GreetingText;

	/** If not empty, the bot answers every message with this text (for testing until a real assistant is connected). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Chat")
	FText PlaceholderReply;

	// ---------------------------------------------------------------- style

	/** Bot bubble fill (Figma: white 27 %). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	FLinearColor BotBubbleColor;

	/** User bubble fill (not in Figma: the side bar's selected blue, so the two sides are easy to tell apart). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	FLinearColor UserBubbleColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	FSlateColor BubbleTextColor = FSlateColor(FLinearColor::White);

	/** Fill of the status line ("Listening...", "Thinking..."): fainter than a bot bubble, so it reads as temporary. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	FLinearColor StatusBubbleColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	FSlateColor StatusTextColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.7f));

	/** Bubble text (Figma: Avenir Medium 15 px = 11 pt). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	FSlateFontInfo BubbleFont;

	/** Corner radius; half the height of a one-line bubble = fully round ends (Figma radius 29 on a 31 px bubble). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	float BubbleCornerRadius = 15.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	FMargin BubblePadding = FMargin(10.f, 5.f, 12.f, 5.f);

	/** Longest a bubble gets before its text wraps (px). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	float MaxBubbleWidth = 230.f;

	/** Space between two bubbles (px). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IHPC|Style")
	float BubbleSpacing = 8.f;

	// ---------------------------------------------------------------- events / calls

	/** The user sent a message (Enter or the send button). Connect the chatbot here; answer with AddBotMessage. */
	UPROPERTY(BlueprintAssignable, Category = "IHPC|Events")
	FIHPCTextEvent OnMessageSent;

	/** The assistant answered (AddBotMessage was called). The side bar uses it to make the animated BOT logo talk. */
	UPROPERTY(BlueprintAssignable, Category = "IHPC|Events")
	FIHPCTextEvent OnBotMessage;

	/** Adds a bubble on the left (the assistant) and fires OnBotMessage. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|Chat")
	void AddBotMessage(const FText& Message);

	/** Adds a bubble on the right (the user). Does NOT fire OnMessageSent - use SubmitMessage for that. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|Chat")
	void AddUserMessage(const FText& Message);

	/** Same as typing Message and pressing Enter. Empty / blank messages are ignored. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|Chat")
	void SubmitMessage(const FText& Message);

	/** Shows a temporary line at the bottom of the list, on the assistant's side ("Listening...", "Thinking...").
	 *  It replaces the previous status and stays below new messages until ClearStatus. Empty = ClearStatus. */
	UFUNCTION(BlueprintCallable, Category = "IHPC|Chat")
	void SetStatus(const FText& Status);

	UFUNCTION(BlueprintCallable, Category = "IHPC|Chat")
	void ClearStatus();

	UFUNCTION(BlueprintPure, Category = "IHPC|Chat")
	FText GetStatus() const { return StatusText; }

	/** Removes every bubble (the greeting comes back). */
	UFUNCTION(BlueprintCallable, Category = "IHPC|Chat")
	void ClearMessages();

	/** Puts the keyboard cursor in the text box (the side bar does this when the window opens). */
	UFUNCTION(BlueprintCallable, Category = "IHPC|Chat")
	void FocusInput();

	UFUNCTION(BlueprintPure, Category = "IHPC|Chat")
	int32 GetMessageCount() const { return NumMessages; }

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UScrollBox> MessageList;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEditableTextBox> MessageInput;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SendButton;

private:
	UFUNCTION() void HandleSendClicked();
	UFUNCTION() void HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	void AddBubble(const FText& Message, bool bFromUser);

	/** Makes one bubble (rounded box + wrapped text) without adding it to the list. */
	UWidget* MakeBubble(const FText& Message, const FLinearColor& Fill, const FSlateColor& TextColor);

	/** Puts the status line (if any) back at the end of the list. */
	void RefreshStatusBubble();

	/** The status line's bubble while one is shown. */
	UPROPERTY(Transient)
	TObjectPtr<UWidget> StatusBubble;

	FText StatusText;

	/** Shows the greeting if the list is empty. */
	void AddGreetingIfEmpty();

	int32 NumMessages = 0;
};

// CPP_UW_IHPCChatPanel.cpp - the assistant chat window next to the BOT avatar. See header.

#include "IHPC_UI/CPP_UW_IHPCChatPanel.h"

#include "IHPC_UI/IHPCUILibrary.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "IHPCChatPanel"

UCPP_UW_IHPCChatPanel::UCPP_UW_IHPCChatPanel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	GreetingText = LOCTEXT("Greeting", "Hello, how may I assist you?");
	BotBubbleColor = UIHPCUILibrary::FigmaColor(TEXT("FFFFFF"), 0.27f);
	UserBubbleColor = UIHPCUILibrary::FigmaColor(TEXT("245983"), 0.9f);
	StatusBubbleColor = UIHPCUILibrary::FigmaColor(TEXT("FFFFFF"), 0.12f);

	// Fallback font (the Widget Blueprint sets Avenir Medium 11).
	if (!IsRunningDedicatedServer())
	{
		static ConstructorHelpers::FObjectFinder<UFont> DefaultFont(*UWidget::GetDefaultFontName());
		BubbleFont = FSlateFontInfo(DefaultFont.Object, 11, FName("Regular"));
	}
}

void UCPP_UW_IHPCChatPanel::NativePreConstruct()
{
	Super::NativePreConstruct();
	// Designer preview: show the greeting bubble so the window can be laid out.
	if (IsDesignTime() && MessageList)
	{
		MessageList->ClearChildren();
		NumMessages = 0;
		AddGreetingIfEmpty();
	}
}

void UCPP_UW_IHPCChatPanel::NativeConstruct()
{
	Super::NativeConstruct();

	if (SendButton)
	{
		SendButton->OnClicked.AddUniqueDynamic(this, &UCPP_UW_IHPCChatPanel::HandleSendClicked);
	}
	if (MessageInput)
	{
		MessageInput->OnTextCommitted.AddUniqueDynamic(this, &UCPP_UW_IHPCChatPanel::HandleTextCommitted);
	}
	AddGreetingIfEmpty();
}

void UCPP_UW_IHPCChatPanel::AddGreetingIfEmpty()
{
	if (NumMessages == 0 && !GreetingText.IsEmpty())
	{
		AddBubble(GreetingText, /*bFromUser*/ false);
	}
}

void UCPP_UW_IHPCChatPanel::AddBotMessage(const FText& Message)
{
	AddBubble(Message, /*bFromUser*/ false);
	OnBotMessage.Broadcast(Message);   // the side bar's animated BOT logo talks while the answer is read
}

void UCPP_UW_IHPCChatPanel::AddUserMessage(const FText& Message)
{
	AddBubble(Message, /*bFromUser*/ true);
}

void UCPP_UW_IHPCChatPanel::SubmitMessage(const FText& Message)
{
	const FString Trimmed = Message.ToString().TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return;
	}
	const FText Clean = FText::FromString(Trimmed);
	AddUserMessage(Clean);
	OnMessageSent.Broadcast(Clean);   // a connected assistant answers with AddBotMessage

	if (!PlaceholderReply.IsEmpty())
	{
		AddBotMessage(PlaceholderReply);
	}
}

void UCPP_UW_IHPCChatPanel::ClearMessages()
{
	if (MessageList)
	{
		MessageList->ClearChildren();
	}
	NumMessages = 0;
	StatusBubble = nullptr;
	AddGreetingIfEmpty();
	RefreshStatusBubble();
}

void UCPP_UW_IHPCChatPanel::SetStatus(const FText& Status)
{
	StatusText = Status;
	RefreshStatusBubble();
}

void UCPP_UW_IHPCChatPanel::ClearStatus()
{
	SetStatus(FText::GetEmpty());
}

void UCPP_UW_IHPCChatPanel::RefreshStatusBubble()
{
	if (!MessageList || !WidgetTree)
	{
		return;
	}
	// Made again each time: simplest way to keep it the last thing in the list.
	if (StatusBubble)
	{
		MessageList->RemoveChild(StatusBubble);
		StatusBubble = nullptr;
	}
	if (StatusText.IsEmpty())
	{
		return;
	}
	StatusBubble = MakeBubble(StatusText, StatusBubbleColor, StatusTextColor);
	if (UScrollBoxSlot* StatusSlot = Cast<UScrollBoxSlot>(MessageList->AddChild(StatusBubble)))
	{
		StatusSlot->SetHorizontalAlignment(HAlign_Left);
		StatusSlot->SetPadding(FMargin(0.f, NumMessages > 0 ? BubbleSpacing : 0.f, 0.f, 0.f));
	}
	MessageList->ScrollToEnd();
}

void UCPP_UW_IHPCChatPanel::FocusInput()
{
	if (MessageInput)
	{
		MessageInput->SetKeyboardFocus();
	}
}

void UCPP_UW_IHPCChatPanel::HandleSendClicked()
{
	if (MessageInput)
	{
		SubmitMessage(MessageInput->GetText());
		MessageInput->SetText(FText::GetEmpty());
		FocusInput();
	}
}

void UCPP_UW_IHPCChatPanel::HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		HandleSendClicked();
	}
}

UWidget* UCPP_UW_IHPCChatPanel::MakeBubble(const FText& Message, const FLinearColor& Fill, const FSlateColor& TextColor)
{
	// Rounded box (Figma bubble) with the text inside; the text wraps once the bubble reaches MaxBubbleWidth.
	UBorder* Bubble = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Bubble->SetBrush(UIHPCUILibrary::MakeRoundedBrush(Fill, BubbleCornerRadius));
	Bubble->SetPadding(BubblePadding);
	Bubble->SetVisibility(ESlateVisibility::HitTestInvisible);   // scrolling with the wheel still works over bubbles

	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(Message);
	Text->SetFont(BubbleFont);
	Text->SetColorAndOpacity(TextColor);
	Text->SetAutoWrapText(false);
	Text->SetWrapTextAt(FMath::Max(MaxBubbleWidth - BubblePadding.Left - BubblePadding.Right, 20.f));
	Bubble->SetContent(Text);
	return Bubble;
}

void UCPP_UW_IHPCChatPanel::AddBubble(const FText& Message, bool bFromUser)
{
	if (!MessageList || !WidgetTree)
	{
		return;
	}

	// The status line stays the last thing in the list: take it out, add the message, put it back.
	if (StatusBubble)
	{
		MessageList->RemoveChild(StatusBubble);
		StatusBubble = nullptr;
	}

	UWidget* Bubble = MakeBubble(Message, bFromUser ? UserBubbleColor : BotBubbleColor, BubbleTextColor);
	if (UScrollBoxSlot* BubbleSlot = Cast<UScrollBoxSlot>(MessageList->AddChild(Bubble)))
	{
		BubbleSlot->SetHorizontalAlignment(bFromUser ? HAlign_Right : HAlign_Left);
		BubbleSlot->SetPadding(FMargin(0.f, NumMessages > 0 ? BubbleSpacing : 0.f, 0.f, 0.f));
	}
	++NumMessages;
	RefreshStatusBubble();
	MessageList->ScrollToEnd();
}

#undef LOCTEXT_NAMESPACE

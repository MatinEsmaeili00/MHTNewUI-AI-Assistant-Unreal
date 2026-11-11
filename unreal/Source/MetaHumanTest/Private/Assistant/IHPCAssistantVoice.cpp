// IHPCAssistantVoice.cpp - microphone -> WAV for the assistant's push-to-talk. See the header.

#include "IHPCAssistantVoice.h"

#include "AudioCaptureCore.h"
#include "Modules/ModuleManager.h"

FIHPCVoiceRecorder::FIHPCVoiceRecorder() = default;

FIHPCVoiceRecorder::~FIHPCVoiceRecorder()
{
	Abort();
}

bool FIHPCVoiceRecorder::Start(FString& OutError)
{
	if (bRecording)
	{
		return true;
	}

	// The platform's capture code lives in the Audio Capture plugin; without it there is no stream to open.
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("AudioCapture")))
	{
		FModuleManager::Get().LoadModule(TEXT("AudioCapture"));
	}

	if (!Synth.IsValid())
	{
		Synth = MakeUnique<Audio::FAudioCaptureSynth>();
	}

	// The stream is opened for every recording and closed after it, so a microphone that was plugged in or
	// chosen as the system default in between is picked up.
	if (!Synth->IsStreamOpen())
	{
		Audio::FCaptureDeviceInfo Device;
		if (!Synth->GetDefaultCaptureDeviceInfo(Device) || Device.InputChannels <= 0 || Device.PreferredSampleRate <= 0)
		{
			OutError = TEXT("I can't find a microphone. Check the system's default input device.");
			return false;
		}
		// Without the real channel count and sample rate the WAV would play at the wrong speed.
		Channels = Device.InputChannels;
		SampleRate = Device.PreferredSampleRate;

		if (!Synth->OpenDefaultStream())
		{
			OutError = TEXT("I couldn't open the microphone. Allow microphone access for this app in the system settings.");
			return false;
		}
	}

	Samples.Reset();
	Peak = 0.f;
	if (!Synth->StartCapturing())
	{
		Synth->AbortCapturing();
		OutError = TEXT("The microphone did not start.");
		return false;
	}
	bRecording = true;
	return true;
}

float FIHPCVoiceRecorder::Poll()
{
	if (!bRecording || !Synth.IsValid())
	{
		return 0.f;
	}
	TArray<float> Fresh;
	if (!Synth->GetAudioData(Fresh) || Fresh.Num() == 0)
	{
		return 0.f;
	}
	Samples.Append(Fresh);

	// Loudness of this piece: root mean square, scaled so normal speech lands around 0.3 - 0.8.
	double SumSquares = 0.0;
	for (const float Sample : Fresh)
	{
		SumSquares += static_cast<double>(Sample) * Sample;
		Peak = FMath::Max(Peak, FMath::Abs(Sample));
	}
	const float Rms = FMath::Sqrt(static_cast<float>(SumSquares / Fresh.Num()));
	return FMath::Clamp(Rms * 6.f, 0.f, 1.f);
}

TArray<uint8> FIHPCVoiceRecorder::Stop(float& OutSeconds)
{
	OutSeconds = 0.f;
	if (!bRecording)
	{
		return TArray<uint8>();
	}
	Poll();   // whatever arrived since the last poll
	bRecording = false;
	if (Synth.IsValid() && Synth->IsStreamOpen())
	{
		Synth->AbortCapturing();   // stops and closes the stream
	}

	OutSeconds = GetSeconds();
	TArray<uint8> Wav = Samples.Num() > 0 ? BuildWav(Samples, Channels, SampleRate) : TArray<uint8>();
	Samples.Reset();
	return Wav;
}

void FIHPCVoiceRecorder::Abort()
{
	bRecording = false;
	if (Synth.IsValid() && Synth->IsStreamOpen())
	{
		Synth->AbortCapturing();
	}
	Samples.Reset();
}

float FIHPCVoiceRecorder::GetSeconds() const
{
	const int32 SamplesPerSecond = FMath::Max(1, Channels) * FMath::Max(1, SampleRate);
	return static_cast<float>(Samples.Num()) / SamplesPerSecond;
}

TArray<uint8> FIHPCVoiceRecorder::BuildWav(const TArray<float>& InSamples, int32 NumChannels, int32 InSampleRate)
{
	NumChannels = FMath::Max(1, NumChannels);
	const int32 NumFrames = InSamples.Num() / NumChannels;

	// Mix down to mono: speech recognition needs no more, and the upload is smaller.
	TArray<float> Mono;
	Mono.SetNumUninitialized(NumFrames);
	float Peak = 0.f;
	for (int32 Frame = 0; Frame < NumFrames; ++Frame)
	{
		float Sum = 0.f;
		for (int32 Channel = 0; Channel < NumChannels; ++Channel)
		{
			Sum += InSamples[Frame * NumChannels + Channel];
		}
		Mono[Frame] = Sum / NumChannels;
		Peak = FMath::Max(Peak, FMath::Abs(Mono[Frame]));
	}

	// Laptop and headset microphones are often very quiet: bring the loudest sample up to 0.9, at most 8 x.
	const float Gain = (Peak > 0.001f && Peak < 0.5f) ? FMath::Min(0.9f / Peak, 8.f) : 1.f;

	const uint32 DataBytes = static_cast<uint32>(NumFrames) * 2u;
	TArray<uint8> Wav;
	Wav.Reserve(44 + DataBytes);
	auto WriteTag = [&Wav](const char* Tag) { Wav.Append(reinterpret_cast<const uint8*>(Tag), 4); };
	auto Write32 = [&Wav](uint32 Value) { Wav.Append(reinterpret_cast<const uint8*>(&Value), 4); };
	auto Write16 = [&Wav](uint16 Value) { Wav.Append(reinterpret_cast<const uint8*>(&Value), 2); };

	WriteTag("RIFF");
	Write32(36 + DataBytes);
	WriteTag("WAVE");
	WriteTag("fmt ");
	Write32(16);                                         // size of the fmt chunk
	Write16(1);                                          // PCM
	Write16(1);                                          // mono
	Write32(static_cast<uint32>(InSampleRate));
	Write32(static_cast<uint32>(InSampleRate) * 2u);     // bytes per second
	Write16(2);                                          // bytes per frame
	Write16(16);                                         // bits per sample
	WriteTag("data");
	Write32(DataBytes);
	for (int32 Frame = 0; Frame < NumFrames; ++Frame)
	{
		const float Value = FMath::Clamp(Mono[Frame] * Gain, -1.f, 1.f);
		Write16(static_cast<uint16>(static_cast<int16>(Value * 32767.f)));
	}
	return Wav;
}

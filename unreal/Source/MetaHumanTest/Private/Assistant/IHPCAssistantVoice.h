// IHPCAssistantVoice.h - the microphone for the assistant's push-to-talk (hold the BOT logo).
// Records the default input device while the logo is held and hands back one 16-bit mono WAV file, which
// UIHPCAssistantComp uploads to the AI server (POST /v1/voice). Needs the engine's Audio Capture plugin.

#pragma once

#include "CoreMinimal.h"

namespace Audio
{
	class FAudioCaptureSynth;
}

class FIHPCVoiceRecorder
{
public:
	FIHPCVoiceRecorder();
	~FIHPCVoiceRecorder();

	/** Opens the default microphone and starts recording. False (with a sentence for the user in OutError) if there
	 *  is no microphone, the Audio Capture plugin is off, or the system did not allow access. */
	bool Start(FString& OutError);

	/** Call a few times per second while recording: moves what the microphone captured into the recording.
	 *  Returns the loudness of the newest piece, 0..1 (for the BOT logo's ripples). */
	float Poll();

	/** Stops and returns the recording as a WAV file (16-bit PCM, mono, the microphone's sample rate).
	 *  OutSeconds = its length. An empty array if nothing was captured. */
	TArray<uint8> Stop(float& OutSeconds);

	/** Stops and throws the recording away. */
	void Abort();

	bool IsRecording() const { return bRecording; }

	/** Seconds recorded so far. */
	float GetSeconds() const;

	/** The loudest sample of the recording so far, 0..1. About 0 = the microphone gave only silence (muted, or the
	 *  system did not allow this app to use it). */
	float GetPeak() const { return Peak; }

	/** 16-bit mono PCM WAV from interleaved float samples. Quiet recordings are made louder (up to 8 x). */
	static TArray<uint8> BuildWav(const TArray<float>& Samples, int32 NumChannels, int32 SampleRate);

private:
	TUniquePtr<Audio::FAudioCaptureSynth> Synth;
	TArray<float> Samples;      // interleaved, Channels per frame
	int32 Channels = 1;
	int32 SampleRate = 48000;
	float Peak = 0.f;
	bool bRecording = false;
};

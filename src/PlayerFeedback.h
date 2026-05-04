#pragma once

#include "PCH.h"

namespace HSK
{
	class PlayerFeedback
	{
	public:
		[[nodiscard]] static PlayerFeedback* GetSingleton()
		{
			static PlayerFeedback instance;
			return &instance;
		}

		void Init();

		void OnPlayerHeadshot();

	private:
		PlayerFeedback() = default;
		PlayerFeedback(const PlayerFeedback&) = delete;
		PlayerFeedback& operator=(const PlayerFeedback&) = delete;

		struct EffectLayer
		{
			RE::TESImageSpaceModifier* imod = nullptr;
			std::string editorID;
			bool animatable{ false };
		};

		void RefreshLayer(EffectLayer& a_layer, const std::string& a_editorID,
			const char* a_debugLabel);
		void PlayTinnitusSound();
		void ApplyAudioMuffle();

		// Load raw WAV bytes from disk (no volume scaling).
		bool LoadWAV(const std::filesystem::path& a_path,
			std::vector<std::uint8_t>& a_outBuffer);

		// Scale 16-bit / 8-bit PCM samples in `a_buffer` (same WAV layout as
		// LoadWAV's output) by `a_volume`. Returns false if no samples were
		// scaled (non-PCM, no data chunk, etc.).
		bool ScaleWAVVolume(std::vector<std::uint8_t>& a_buffer, float a_volume);

		// Rebuild _audioBuffer from _rawAudioBuffer at the given volume.
		// Caller must hold _audioMutex.
		void RebuildAudioBufferAtVolume(float a_volume);

		EffectLayer         _concussion;
		EffectLayer         _impactFlash;
		EffectLayer         _greyscale;
		std::atomic<bool>   _ready{ false };

		// Pristine WAV bytes (full volume) loaded at Init -- never modified
		// once populated. _audioBuffer is a working copy with the current
		// volume baked in.
		std::vector<std::uint8_t> _rawAudioBuffer;
		std::vector<std::uint8_t> _audioBuffer;
		float                     _audioBufferVolume{ -1.0f };
		std::mutex                _audioMutex;

		// Audio muffle fade-back state
		std::atomic<bool>   _muffleActive{ false };

		std::chrono::steady_clock::time_point _lastFeedbackTime{};
		std::mutex                            _cooldownMutex;
	};
}

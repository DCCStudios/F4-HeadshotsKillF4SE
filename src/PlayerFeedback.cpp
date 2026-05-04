#include "PlayerFeedback.h"
#include "Settings.h"
#include "GameDefinitions.h"

#include <Windows.h>
#include <fstream>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

#ifdef PlaySound
#	undef PlaySound
#endif

namespace
{
#pragma pack(push, 1)
	struct WAVHeader
	{
		char          riff[4];
		std::uint32_t fileSize;
		char          wave[4];
	};

	struct WAVChunkHeader
	{
		char          id[4];
		std::uint32_t size;
	};

	struct WAVFmtChunk
	{
		std::uint16_t audioFormat;
		std::uint16_t numChannels;
		std::uint32_t sampleRate;
		std::uint32_t byteRate;
		std::uint16_t blockAlign;
		std::uint16_t bitsPerSample;
	};
#pragma pack(pop)

	std::filesystem::path ResolveWavPath(std::string_view a_name)
	{
		std::string fn(a_name);
		while (!fn.empty() && (fn.back() == ' ' || fn.back() == '\t')) fn.pop_back();
		std::size_t i = 0;
		while (i < fn.size() && (fn[i] == ' ' || fn[i] == '\t')) ++i;
		if (i > 0) fn.erase(0, i);
		if (fn.empty()) fn = "headshot_tinnitus.wav";
		else if (fn.size() < 4 || _stricmp(fn.c_str() + fn.size() - 4, ".wav") != 0)
			fn += ".wav";

		auto p = std::filesystem::current_path();
		p /= "Data";
		p /= "F4SE";
		p /= "Plugins";
		p /= "HeadshotsKillF4SE";
		p /= fn;
		return p;
	}

	// ── Version-aware IMod engine functions ──────────────────────────────
	// Pre-NG (1.10.163): Trigger/Stop by EditorID name
	//   Trigger(BSFixedString&) -> REL::ID(1216312)
	//   Stop(BSFixedString&)    -> REL::ID(549773)
	// Post-NG/AE:
	//   Trigger(BSFixedString&) -> REL::ID(2199907)
	//   Stop(BSFixedString&)    -> REL::ID(2199910)

	using TriggerByNameFn = void* (*)(const RE::BSFixedString&);
	using StopByNameFn    = void (*)(const RE::BSFixedString&);

	TriggerByNameFn g_triggerByName = nullptr;
	StopByNameFn    g_stopByName    = nullptr;

	void ResolveFunctions()
	{
		if (HSK::IsNextGen()) {
			g_triggerByName = REL::Relocation<TriggerByNameFn>{ REL::ID(2199907) }.get();
			g_stopByName    = REL::Relocation<StopByNameFn>{ REL::ID(2199910) }.get();
			logger::info("[HSK]   IMod functions resolved (Post-NG IDs)");
		} else {
			g_triggerByName = REL::Relocation<TriggerByNameFn>{ REL::ID(1216312) }.get();
			g_stopByName    = REL::Relocation<StopByNameFn>{ REL::ID(549773) }.get();
			logger::info("[HSK]   IMod functions resolved (Pre-NG IDs)");
		}
	}

	void TriggerIMod(const char* a_editorID)
	{
		if (g_triggerByName && a_editorID && a_editorID[0]) {
			RE::BSFixedString name(a_editorID);
			g_triggerByName(name);
		}
	}

	void StopIMod(const char* a_editorID)
	{
		if (g_stopByName && a_editorID && a_editorID[0]) {
			RE::BSFixedString name(a_editorID);
			g_stopByName(name);
		}
	}
}

namespace HSK
{
	// ── WAV loader (raw, no volume scaling) ──────────────────────────────
	bool PlayerFeedback::LoadWAV(const std::filesystem::path& a_path,
		std::vector<std::uint8_t>& a_outBuffer)
	{
		std::ifstream file(a_path, std::ios::binary | std::ios::ate);
		if (!file.is_open()) return false;

		const auto fileSize = file.tellg();
		file.seekg(0, std::ios::beg);
		a_outBuffer.resize(static_cast<std::size_t>(fileSize));
		if (!file.read(reinterpret_cast<char*>(a_outBuffer.data()), fileSize))
			return false;
		file.close();

		if (a_outBuffer.size() < sizeof(WAVHeader)) return false;
		auto* header = reinterpret_cast<WAVHeader*>(a_outBuffer.data());
		if (std::memcmp(header->riff, "RIFF", 4) != 0 ||
			std::memcmp(header->wave, "WAVE", 4) != 0)
			return false;

		return true;
	}

	// ── Apply volume to PCM samples in-place ─────────────────────────────
	// Walks the WAV chunks to find fmt + data, then multiplies each sample
	// by a_volume (clamped to int range). Volume of 1.0 is a no-op, so this
	// is cheap to call every play.
	bool PlayerFeedback::ScaleWAVVolume(std::vector<std::uint8_t>& a_buffer,
		float a_volume)
	{
		if (a_buffer.size() < sizeof(WAVHeader)) return false;

		std::size_t pos = sizeof(WAVHeader);
		WAVFmtChunk*  fmt       = nullptr;
		std::uint8_t* audioData = nullptr;
		std::uint32_t audioDataSize = 0;

		while (pos + sizeof(WAVChunkHeader) <= a_buffer.size()) {
			auto* chunk = reinterpret_cast<WAVChunkHeader*>(a_buffer.data() + pos);
			if (std::memcmp(chunk->id, "fmt ", 4) == 0)
				fmt = reinterpret_cast<WAVFmtChunk*>(a_buffer.data() + pos + sizeof(WAVChunkHeader));
			else if (std::memcmp(chunk->id, "data", 4) == 0) {
				audioData     = a_buffer.data() + pos + sizeof(WAVChunkHeader);
				audioDataSize = chunk->size;
				break;
			}
			pos += sizeof(WAVChunkHeader) + chunk->size;
			if (pos % 2 != 0) ++pos;
		}

		if (!fmt || !audioData || audioDataSize == 0) return false;
		if (fmt->audioFormat != 1) return false; // not PCM
		if (a_volume >= 0.999f) return true;     // no scaling needed

		const float vol = std::clamp(a_volume, 0.0f, 1.0f);
		if (fmt->bitsPerSample == 16) {
			auto* samples = reinterpret_cast<std::int16_t*>(audioData);
			const std::size_t n = audioDataSize / sizeof(std::int16_t);
			for (std::size_t j = 0; j < n; ++j) {
				const float s = static_cast<float>(samples[j]) * vol;
				samples[j] = static_cast<std::int16_t>(std::clamp(s, -32768.0f, 32767.0f));
			}
		} else if (fmt->bitsPerSample == 8) {
			for (std::uint32_t j = 0; j < audioDataSize; ++j) {
				const float s = (static_cast<float>(audioData[j]) - 128.0f) * vol;
				audioData[j] = static_cast<std::uint8_t>(std::clamp(s + 128.0f, 0.0f, 255.0f));
			}
		}
		return true;
	}

	// Caller must hold _audioMutex. Rebuilds _audioBuffer from _rawAudioBuffer
	// scaled to the given volume, and stamps _audioBufferVolume.
	void PlayerFeedback::RebuildAudioBufferAtVolume(float a_volume)
	{
		if (_rawAudioBuffer.empty()) {
			_audioBuffer.clear();
			_audioBufferVolume = -1.0f;
			return;
		}
		_audioBuffer = _rawAudioBuffer; // copy pristine bytes
		ScaleWAVVolume(_audioBuffer, a_volume);
		_audioBufferVolume = a_volume;
	}

	// ── Per-layer init / refresh ─────────────────────────────────────────
	void PlayerFeedback::RefreshLayer(EffectLayer& a_layer,
		const std::string& a_editorID, const char* a_debugLabel)
	{
		if (a_editorID.empty()) {
			a_layer.imod = nullptr;
			a_layer.editorID.clear();
			a_layer.animatable = false;
			return;
		}

		if (a_layer.editorID == a_editorID && a_layer.imod) {
			return;
		}

		a_layer.imod = RE::TESForm::GetFormByEditorID<RE::TESImageSpaceModifier>(
			RE::BSFixedString(a_editorID.c_str()));
		if (!a_layer.imod) {
			logger::warn("[HSK]   {} -- IMod '{}' not found",
				a_debugLabel, a_editorID);
			a_layer.editorID.clear();
			a_layer.animatable = false;
			return;
		}

		a_layer.editorID = a_editorID;
		a_layer.animatable = a_layer.imod->data.animatable;
		logger::info("[HSK]   {} -- resolved '{}' (0x{:08X}, animatable={})",
			a_debugLabel, a_editorID, a_layer.imod->GetFormID(), a_layer.animatable);
	}

	// ── Init ─────────────────────────────────────────────────────────────
	void PlayerFeedback::Init()
	{
		if (_ready.load(std::memory_order_acquire)) return;

		const auto* s = Settings::GetSingleton();
		logger::info("[HSK] PlayerFeedback::Init");

		ResolveFunctions();

		RefreshLayer(_concussion,  s->playerFeedback.concussion.imodEditorID,  "Concussion");
		RefreshLayer(_impactFlash, s->playerFeedback.impactFlash.imodEditorID, "ImpactFlash");
		RefreshLayer(_greyscale,   s->playerFeedback.greyscale.imodEditorID,   "Greyscale");

		if (s->playerFeedback.enableTinnitusSound) {
			const auto wavPath = ResolveWavPath(s->playerFeedback.tinnitusSoundFile);
			if (std::filesystem::exists(wavPath)) {
				std::lock_guard lk(_audioMutex);
				if (LoadWAV(wavPath, _rawAudioBuffer)) {
					RebuildAudioBufferAtVolume(s->playerFeedback.tinnitusSoundVolume);
					logger::info("[HSK]   Tinnitus WAV loaded: {} ({} bytes, vol={:.2f})",
						wavPath.string(), _rawAudioBuffer.size(),
						s->playerFeedback.tinnitusSoundVolume);
				} else {
					logger::warn("[HSK]   Failed to load tinnitus WAV: {}", wavPath.string());
				}
			} else {
				logger::warn("[HSK]   Tinnitus WAV not found at: {}", wavPath.string());
				logger::warn("[HSK]   Place a WAV file named '{}' in Data/F4SE/Plugins/HeadshotsKillF4SE/",
					s->playerFeedback.tinnitusSoundFile);
			}
		} else {
			logger::info("[HSK]   Tinnitus sound disabled in settings");
		}

		logger::info("[HSK]   Audio muffle: enabled={}, intensity={:.2f}, fadeDur={:.1f}s",
			s->playerFeedback.enableAudioMuffle,
			s->playerFeedback.muffleIntensity,
			s->playerFeedback.muffleFadeDuration);

		_ready.store(true, std::memory_order_release);
		logger::info("[HSK] PlayerFeedback ready");
	}

	// ── Trigger ──────────────────────────────────────────────────────────
	void PlayerFeedback::OnPlayerHeadshot()
	{
		const auto* s = Settings::GetSingleton();
		if (!s->playerFeedback.enableFeedback) return;
		if (!_ready.load(std::memory_order_acquire)) return;

		{
			std::lock_guard lk(_cooldownMutex);
			const auto now = std::chrono::steady_clock::now();
			const auto elapsed =
				std::chrono::duration<float>(now - _lastFeedbackTime).count();
			if (elapsed < s->playerFeedback.feedbackCooldown) {
				if (s->debugLogging) {
					logger::info("[HSK] Player headshot feedback suppressed "
						"(cooldown {:.1f}s < {:.1f}s)",
						elapsed, s->playerFeedback.feedbackCooldown);
				}
				return;
			}
			_lastFeedbackTime = now;
		}

		if (s->debugLogging) {
			logger::info("[HSK] Player headshot feedback triggered");
		}

		PlayTinnitusSound();
		ApplyAudioMuffle();

		// Re-resolve layers from current settings (hot-reload support).
		const auto& pf = s->playerFeedback;
		RefreshLayer(_concussion,  pf.concussion.imodEditorID,  "Concussion");
		RefreshLayer(_impactFlash, pf.impactFlash.imodEditorID, "ImpactFlash");
		RefreshLayer(_greyscale,   pf.greyscale.imodEditorID,   "Greyscale");

		struct LayerTask {
			std::string editorID;
			float       duration;
		};
		std::vector<LayerTask> layers;

		if (pf.concussion.enabled && _concussion.imod)
			layers.push_back({ _concussion.editorID, pf.concussion.duration });
		if (pf.impactFlash.enabled && _impactFlash.imod)
			layers.push_back({ _impactFlash.editorID, pf.impactFlash.duration });
		if (pf.greyscale.enabled && _greyscale.imod)
			layers.push_back({ _greyscale.editorID, pf.greyscale.duration });

		if (layers.empty()) return;

		auto* task = F4SE::GetTaskInterface();
		if (!task) return;

		// Trigger all layers on the main game thread.
		// For each layer, temporarily force the IMod form to animatable=true
		// with our configured duration. This makes the engine play the effect
		// as a timed animation that naturally fades out, rather than a
		// permanent static hold that requires an abrupt Stop.
		task->AddTask([layers, debugLog = s->debugLogging]() {
			for (const auto& l : layers) {
				auto* imod = RE::TESForm::GetFormByEditorID<RE::TESImageSpaceModifier>(
					RE::BSFixedString(l.editorID.c_str()));
				if (!imod) continue;

				const bool origAnimatable = imod->data.animatable;
				const float origDuration  = imod->data.duration;

				imod->data.animatable = true;
				imod->data.duration   = l.duration;

				TriggerIMod(l.editorID.c_str());

				imod->data.animatable = origAnimatable;
				imod->data.duration   = origDuration;

				if (debugLog) {
					logger::info("[HSK]   IMod triggered: '{}' dur={:.1f}s (was animatable={}, dur={:.1f}s)",
						l.editorID, l.duration, origAnimatable, origDuration);
				}
			}
		});
	}

	// ── Sound ────────────────────────────────────────────────────────────
	void PlayerFeedback::PlayTinnitusSound()
	{
		const auto* s = Settings::GetSingleton();
		if (!s->playerFeedback.enableTinnitusSound) return;

		const float curVolume =
			std::clamp(s->playerFeedback.tinnitusSoundVolume, 0.0f, 1.0f);

		auto* task = F4SE::GetTaskInterface();
		if (!task) return;

		task->AddTask([this, curVolume]() {
			std::lock_guard lk(_audioMutex);
			if (_rawAudioBuffer.empty()) {
				if (Settings::GetSingleton()->debugLogging) {
					logger::warn("[HSK] Tinnitus: no WAV loaded (file missing or disabled at init)");
				}
				return;
			}

			// Volume zero is treated as "muted" -- skip PlaySoundA entirely
			// so we don't waste a syscall feeding silence to the audio device.
			if (curVolume <= 0.0001f) {
				if (Settings::GetSingleton()->debugLogging) {
					logger::info("[HSK] Tinnitus muted (volume=0)");
				}
				return;
			}

			// Re-scale only when the slider has actually changed since the
			// last play. The pristine bytes live in _rawAudioBuffer.
			if (std::abs(_audioBufferVolume - curVolume) > 0.001f) {
				RebuildAudioBufferAtVolume(curVolume);
				if (Settings::GetSingleton()->debugLogging) {
					logger::info("[HSK] Tinnitus volume rebuilt at {:.2f}", curVolume);
				}
			}

			const BOOL ok = PlaySoundA(
				reinterpret_cast<LPCSTR>(_audioBuffer.data()),
				nullptr,
				SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
			if (Settings::GetSingleton()->debugLogging) {
				logger::info("[HSK] Tinnitus PlaySoundA: ok={} bufSize={} vol={:.2f}",
					(bool)ok, _audioBuffer.size(), curVolume);
			}
		});
	}

	// ── Audio Muffle ─────────────────────────────────────────────────────
	void PlayerFeedback::ApplyAudioMuffle()
	{
		const auto* s = Settings::GetSingleton();
		if (!s->playerFeedback.enableAudioMuffle) return;

		if (_muffleActive.exchange(true, std::memory_order_acq_rel)) return;

		const float targetFreq = s->playerFeedback.muffleIntensity;
		const float fadeDur    = s->playerFeedback.muffleFadeDuration;

		static const char* kCategories[] = {
			"AudioCategorySFX",
			"AudioCategoryVOC",
		};

		struct CatInfo {
			RE::BGSSoundCategory* cat;
			float originalFreq;
		};

		auto* task = F4SE::GetTaskInterface();
		if (!task) { _muffleActive.store(false); return; }

		// Resolve categories and apply initial muffle on main thread,
		// then hand off the fade-back to a background thread.
		auto catsPtr = std::make_shared<std::vector<CatInfo>>();

		task->AddTask([this, catsPtr, targetFreq, fadeDur]() {
			for (const auto* name : kCategories) {
				auto* cat = RE::TESForm::GetFormByEditorID<RE::BGSSoundCategory>(
					RE::BSFixedString(name));
				if (cat) {
					auto* iface = static_cast<RE::BSISoundCategory*>(cat);
					catsPtr->push_back({ cat, iface->GetCategoryFrequency() });
					iface->SetCategoryFrequency(targetFreq);
				}
			}

			if (Settings::GetSingleton()->debugLogging) {
				logger::info("[HSK]   Audio muffle applied: freq={:.2f}, fade={:.1f}s, categories={}",
					targetFreq, fadeDur, catsPtr->size());
			}

			if (catsPtr->empty()) {
				_muffleActive.store(false, std::memory_order_release);
				return;
			}

			// Gradual fade-back
			std::thread([this, catsPtr, targetFreq, fadeDur]() {
				constexpr float kStepInterval = 0.05f;
				const int steps = std::max(1, static_cast<int>(fadeDur / kStepInterval));
				const auto stepMs = std::chrono::milliseconds(
					static_cast<int>(kStepInterval * 1000.0f));

				for (int i = 1; i <= steps; ++i) {
					std::this_thread::sleep_for(stepMs);
					const float t = static_cast<float>(i) / static_cast<float>(steps);
					auto* ti = F4SE::GetTaskInterface();
					if (!ti) break;

					auto captured = catsPtr;
					ti->AddTask([captured, targetFreq, t]() {
						for (const auto& c : *captured) {
							const float freq = std::lerp(targetFreq, c.originalFreq, t);
							static_cast<RE::BSISoundCategory*>(c.cat)->SetCategoryFrequency(freq);
						}
					});
				}

				// Final exact restore
				std::this_thread::sleep_for(stepMs);
				auto* ti = F4SE::GetTaskInterface();
				if (ti) {
					auto captured = catsPtr;
					ti->AddTask([captured]() {
						for (const auto& c : *captured) {
							static_cast<RE::BSISoundCategory*>(c.cat)->SetCategoryFrequency(c.originalFreq);
						}
					});
				}

				_muffleActive.store(false, std::memory_order_release);
			}).detach();
		});
	}
}

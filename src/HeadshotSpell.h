#pragma once

#include "PCH.h"
#include "Settings.h"

namespace HSK
{
	class HeadshotSpell
	{
	public:
		[[nodiscard]] static HeadshotSpell* GetSingleton()
		{
			static HeadshotSpell instance;
			return &instance;
		}

		// Must be called after game data is loaded (kGameDataReady).
		// Creates the runtime EffectSetting + SpellItem used for instakill damage.
		void Init();

		// Cast the instakill spell on the target. Thread-safe to call from
		// the F4SE task queue (main thread). Does nothing if Init() hasn't
		// completed or the target is dead/null.
		void ApplyKillDamage(RE::Actor* a_target, RE::Actor* a_aggressor);

		// Two-shot rule for player/followers: deals proportional damage that
		// brings the target to near-death (a_remainRatio of max HP) without
		// killing. The follow-up call to ApplyKillDamage finishes the job.
		void ApplyNearDeathDamage(RE::Actor* a_target, RE::Actor* a_aggressor,
			float a_remainRatio);

		[[nodiscard]] bool IsReady() const { return _ready.load(std::memory_order_acquire); }

	private:
		HeadshotSpell() = default;
		HeadshotSpell(const HeadshotSpell&) = delete;
		HeadshotSpell& operator=(const HeadshotSpell&) = delete;

		RE::SpellItem*     _spell  = nullptr;
		RE::EffectSetting* _effect = nullptr;
		std::atomic<bool>  _ready  = false;
	};
}

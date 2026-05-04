#pragma once

#include "PCH.h"
#include "GameDefinitions.h"

namespace HSK
{
	// =====================================================================
	// HitHook -- trampoline hook on the engine's DoHitMe function.
	//
	// TESHitEvent is unreliable on OG (1.10.163) and when projectile
	// conversion mods are installed (HitData is fired zeroed with
	// usesHitData=false). The engine's DoHitMe(Actor*, HitData&) is
	// called internally for EVERY hit with a fully populated HitData.
	// We hook the call site at REL::ID(1546751), 0x921 (same address
	// used by the DirectHit mod) to intercept every hit directly.
	//
	// The hook constructs a synthetic TESHitEvent that wraps the real
	// HitData and feeds it into HeadshotLogic::EvaluateHit, then
	// continues into the engine's original DoHitMe.
	// =====================================================================
	class HitHook
	{
	public:
		// Install the trampoline hook. Must be called AFTER
		// F4SE::AllocTrampoline() has reserved space.
		// Returns true on success.
		static bool Install();

		[[nodiscard]] static bool IsInstalled() { return _installed; }

	private:
		static inline bool _installed{ false };
	};
}

#include "HitHook.h"
#include "HeadshotLogic.h"
#include "Settings.h"

namespace HSK
{
	// =================================================================
	// DoHitMe hook -- fires for every hit with fully-populated HitData.
	//
	// Signature (discovered from the DirectHit mod, which uses the same
	// call site): void DoHitMe(RE::Actor* a_victim, RE::HitData& a_hitData)
	//
	// a_victim is the actor being hit. The real attacker is resolved
	// through a_hitData.attackerHandle (at offset 0x40 of HitData).
	// =================================================================
	namespace
	{
		// Signature of the hooked function.
		using DoHitMeFn = void (*)(RE::Actor*, RE::HitData&);

		// Storage for the original DoHitMe relocation written by the
		// trampoline -- calling this invokes the engine's real damage
		// calculation path.
		REL::Relocation<DoHitMeFn> g_originalDoHitMe;

		// Resolve an ObjectRefHandle (u32 at HitData+0x40/0x44/0x48) to
		// a raw TESObjectREFR*. Safe on null / stale handles; returns
		// nullptr in those cases.
		RE::TESObjectREFR* ResolveRefHandle(const RE::HitData& a_hd, std::size_t a_offset)
		{
			const auto* base = reinterpret_cast<const std::uint8_t*>(&a_hd);
			RE::ObjectRefHandle handle = *reinterpret_cast<const RE::ObjectRefHandle*>(base + a_offset);
			if (!handle) return nullptr;

			RE::NiPointer<RE::TESObjectREFR> ptr;
			try {
				if (RE::BSPointerHandleManagerInterface<RE::TESObjectREFR>::GetSmartPointer(handle, ptr)) {
					return ptr.get();
				}
			} catch (...) {
				// REL::ID for GetSmartPointer may not resolve; fall through
			}
			return nullptr;
		}

		void HookedDoHitMe(RE::Actor* a_victim, RE::HitData& a_hitData)
		{
			// FAST PATH: master toggle. Skip all work if the mod is disabled
			// so the hook is effectively a no-op for users who turned it off.
			const auto* settings = Settings::GetSingleton();
			if (settings && settings->enabled && a_victim) {
				try {
					// Build a synthetic TESHitEvent on the stack. Our TESHitEvent
					// layout's first 0xE0 bytes are compatible with both OG
					// (actual HitData size 0xD8) and NG (size 0xE0) -- the
					// fields we read via GameDefinitions accessors all live
					// within the first 0xD8 bytes.
					alignas(RE::TESHitEvent) std::uint8_t storage[sizeof(RE::TESHitEvent)]{};
					auto* evt = reinterpret_cast<RE::TESHitEvent*>(storage);

					// Copy the real HitData into our struct. 0xD8 bytes covers
					// all of PreNG's HitData. On NG (0xE0) the extra 8 bytes
					// contain fields we never read in the kill path, so 0xD8
					// is safe for both.
					std::memcpy(&evt->hitData, &a_hitData, 0xD8);

					// Victim = `a_victim` (this parameter), NOT the handle --
					// the handle may not be set yet at the DoHitMe call site.
					evt->target = a_victim;

					// Attacker = resolve attackerHandle at HitData+0x40.
					// Fall back to null (some hits, like traps, have no attacker).
					evt->cause = ResolveRefHandle(a_hitData, 0x40);

					evt->sourceFormID     = 0;
					evt->projectileFormID = 0;
					evt->usesHitData      = true;

					HeadshotLogic::EvaluateHit(*evt);
				} catch (const std::exception& e) {
					logger::error("[HSK] DoHitMe hook body threw: {}", e.what());
				} catch (...) {
					logger::error("[HSK] DoHitMe hook body threw unknown exception");
				}
			}

			// ALWAYS call the original to preserve normal game behavior
			// (damage, death triggers, hit reactions, etc.). Even if our
			// EvaluateHit applies a kill via its spell path, the engine's
			// DoHitMe must still run to complete the hit frame.
			g_originalDoHitMe(a_victim, a_hitData);
		}
	}

	bool HitHook::Install()
	{
		if (_installed) return true;

		try {
			// Trampoline memory is provided by F4SE::AllocTrampoline()
			// which we call in F4SEPlugin_Load. Do NOT call create() here
			// or we'd overwrite the branch-pool memory F4SE gave us.
			auto& trampoline = F4SE::GetTrampoline();

			// DirectHit uses REL::ID(1546751) + 0x921. This is the call
			// instruction to DoHitMe inside Actor::AttackAnim / HitFrame.
			// Same address on OG and NG (IDs are stable for engine calls).
			const REL::Relocation<std::uintptr_t> callSite{ REL::ID(1546751), 0x921 };

			g_originalDoHitMe = trampoline.write_call<5>(
				callSite.address(),
				reinterpret_cast<std::uintptr_t>(&HookedDoHitMe));

			_installed = true;
			logger::info("[HSK] DoHitMe hook installed at 0x{:X} (trampoline)", callSite.address());
			return true;
		} catch (const std::exception& e) {
			logger::error("[HSK] DoHitMe hook install failed: {}", e.what());
			return false;
		} catch (...) {
			logger::error("[HSK] DoHitMe hook install failed (unknown exception)");
			return false;
		}
	}
}

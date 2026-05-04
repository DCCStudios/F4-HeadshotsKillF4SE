#pragma once

#include "PCH.h"
#include "GameDefinitions.h"

namespace HSK
{
	// =====================================================================
	// LegendaryTracker
	//
	// Multi-layer detection for legendary mutation. We listen to
	// TESMagicEffectApplyEvent for known legendary mutation magic effect
	// IDs, and we also retain a per-actor health snapshot to fall back to
	// when an unknown mutation overhaul is in use.
	//
	// Public state used by HeadshotLogic on the deferred-kill task:
	//   - WasMutated(formID)       -> bool    : actor mutated since SnapshotHealth()
	//   - InCooldown(formID)       -> bool    : actor is in post-mutation cooldown
	//   - SnapshotHealth(actor)              : record current health ratio
	//   - HasMutatedByHealth(actor)-> bool    : if ratio gain > threshold
	//   - StartCooldown(formID)              : start the cooldown timer
	// =====================================================================
	class LegendaryTracker :
		public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
	{
	public:
		static LegendaryTracker* GetSingleton()
		{
			static LegendaryTracker singleton;
			return &singleton;
		}

		void Init();           // resolves the 4 mutation perk FormIDs and registers the magic sink
		void Shutdown();       // unregisters the sink

		// BSTEventSink<TESMagicEffectApplyEvent>
		RE::BSEventNotifyControl ProcessEvent(
			const RE::TESMagicEffectApplyEvent&        a_event,
			RE::BSTEventSource<RE::TESMagicEffectApplyEvent>* a_source) override;

		// Layer 2: did we receive a mutation magic effect for this actor recently?
		[[nodiscard]] bool WasJustMutated(std::uint32_t a_actorFormID) const;

		// Layer 3: snapshot health ratio at headshot time.
		void SnapshotHealth(RE::Actor* a_actor);

		// Layer 3: at deferred task time, has the ratio gone up significantly?
		[[nodiscard]] bool HasMutatedByHealth(RE::Actor* a_actor, float a_threshold) const;

		// Cooldown management (post-mutation suppression).
		[[nodiscard]] bool InCooldown(std::uint32_t a_actorFormID) const;
		void               StartCooldown(std::uint32_t a_actorFormID, float a_seconds);

		// Pruning to bound memory.
		void PruneStale();

	private:
		LegendaryTracker() = default;
		~LegendaryTracker() = default;

		mutable std::shared_mutex _mutex;

		// Mutation magic-effect IDs (resolved from default object editor IDs).
		std::unordered_set<std::uint32_t> _mutationEffectIDs;

		// FormID -> seconds-since-epoch of the mutation event (Layer 2 signal)
		std::unordered_map<std::uint32_t, float> _recentMutations;

		// FormID -> health ratio at snapshot time
		std::unordered_map<std::uint32_t, float> _snapshots;

		// FormID -> cooldown end time
		std::unordered_map<std::uint32_t, float> _cooldownEnd;

		bool _registered{ false };

		// Fast monotonic-ish clock returning seconds since plugin start.
		[[nodiscard]] static float Now();
	};
}

#include "LegendaryTracker.h"

namespace HSK
{
	// =================================================================
	// Static helpers
	// =================================================================
	float LegendaryTracker::Now()
	{
		using namespace std::chrono;
		static const auto start = steady_clock::now();
		return duration_cast<duration<float>>(steady_clock::now() - start).count();
	}

	// =================================================================
	// Init / Shutdown
	// =================================================================
	void LegendaryTracker::Init()
	{
		std::unique_lock lk(_mutex);
		_mutationEffectIDs.clear();

		// Vanilla legendary mutation magic-effect EditorIDs.
		// We try common naming conventions; missing ones are silently skipped.
		static constexpr const char* kEffectIDs[] = {
			// Mutation perk-bound spells / magic effects
			"MutationAccuracyMGEF",       // accuracy
			"MutationDmgMGEF",            // 2x damage
			"MutationPoisonMGEF",         // poison
			"MutationRadiationMGEF",      // radiation
			// Aura/visible mutation effects (more reliably present)
			"AbLegendaryAccuracy",        // not always a magic effect, but if defined as such
			"AbLegendary2xDmg",
			"AbLegendaryPoison",
			"AbLegendaryRadioactive",
		};

		for (auto* edid : kEffectIDs) {
			if (auto* form = RE::TESForm::GetFormByEditorID(edid)) {
				_mutationEffectIDs.insert(form->GetFormID());
			}
		}

		// Also register direct vanilla FormIDs as final fallback (these are the
		// well-known LegendaryAccuracyPerk etc. -- if their associated magic effects
		// share the form prefix, the keyword form lookup above usually catches them).
		// We also accept the "01FE6Bx" / "01FA24x" range as a soft hint at
		// classification time below in ProcessEvent.

		if (!_registered) {
			if (RegisterMagicEffectApplyEventSink(this)) {
				_registered = true;
				logger::info("[HSK] LegendaryTracker registered ({} known mutation effects)", _mutationEffectIDs.size());
			} else {
				logger::warn("[HSK] LegendaryTracker failed to register TESMagicEffectApplyEvent sink");
			}
		}
	}

	void LegendaryTracker::Shutdown()
	{
		std::unique_lock lk(_mutex);
		if (_registered) {
			UnregisterMagicEffectApplyEventSink(this);
			_registered = false;
		}
		_mutationEffectIDs.clear();
		_recentMutations.clear();
		_snapshots.clear();
		_cooldownEnd.clear();
	}

	// =================================================================
	// Magic effect sink
	// =================================================================
	RE::BSEventNotifyControl LegendaryTracker::ProcessEvent(
		const RE::TESMagicEffectApplyEvent& a_event,
		RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*)
	{
		// Layer 2 detection.
		if (!a_event.target) {
			return RE::BSEventNotifyControl::kContinue;
		}

		const std::uint32_t mgefID = a_event.magicEffectFormID;
		const std::uint32_t actorID = a_event.target->GetFormID();

		bool match = false;
		{
			std::shared_lock lk(_mutex);
			if (_mutationEffectIDs.count(mgefID)) {
				match = true;
			}
		}

		// Soft heuristic: vanilla legendary mutation MGEFs/perks live in
		// 0x001Fxxxx Fallout4.esm range. We only look at low-byte patterns we know:
		//   Legendary perks: 1FE6B0, 1FE6B2, 1FE6B8, 1FA246
		//   Mutation aura spells: 1FE6AC, 1FA242, 1FE6BD, 1FE6BB
		if (!match) {
			const std::uint32_t lo = mgefID & 0x00FFFFFF;
			static constexpr std::uint32_t kKnown[] = {
				0x001FE6B0, 0x001FE6B2, 0x001FE6B8, 0x001FA246,
				0x001FE6AC, 0x001FA242, 0x001FE6BD, 0x001FE6BB,
			};
			for (auto k : kKnown) {
				if (lo == k) {
					match = true;
					break;
				}
			}
		}

		if (match) {
			std::unique_lock lk(_mutex);
			_recentMutations[actorID] = Now();
			logger::debug("[HSK] Legendary mutation detected on actor 0x{:08X} (mgef 0x{:08X})", actorID, mgefID);
		}

		return RE::BSEventNotifyControl::kContinue;
	}

	// =================================================================
	// Layer 2 -- recent mutation check
	// =================================================================
	bool LegendaryTracker::WasJustMutated(std::uint32_t a_actorFormID) const
	{
		std::shared_lock lk(_mutex);
		auto it = _recentMutations.find(a_actorFormID);
		if (it == _recentMutations.end()) return false;
		// Recent = within the last ~2 seconds (mutation animation window)
		return (Now() - it->second) < 2.0f;
	}

	// =================================================================
	// Layer 3 -- health snapshot
	// =================================================================
	void LegendaryTracker::SnapshotHealth(RE::Actor* a_actor)
	{
		if (!a_actor) return;
		auto* avInfo = RE::ActorValue::GetSingleton();
		if (!avInfo || !avInfo->health) return;
		const float cur = a_actor->GetActorValue(*avInfo->health);
		const float per = a_actor->GetPermanentActorValue(*avInfo->health);
		if (per <= 0.0f) return;
		const float ratio = std::clamp(cur / per, 0.0f, 2.0f);
		std::unique_lock lk(_mutex);
		_snapshots[a_actor->GetFormID()] = ratio;
	}

	bool LegendaryTracker::HasMutatedByHealth(RE::Actor* a_actor, float a_threshold) const
	{
		if (!a_actor) return false;
		float snapshot = -1.0f;
		{
			std::shared_lock lk(_mutex);
			auto it = _snapshots.find(a_actor->GetFormID());
			if (it == _snapshots.end()) return false;
			snapshot = it->second;
		}
		auto* avInfo = RE::ActorValue::GetSingleton();
		if (!avInfo || !avInfo->health) return false;
		const float cur = a_actor->GetActorValue(*avInfo->health);
		const float per = a_actor->GetPermanentActorValue(*avInfo->health);
		if (per <= 0.0f) return false;
		const float ratio = std::clamp(cur / per, 0.0f, 2.0f);
		return (ratio - snapshot) >= a_threshold;
	}

	// =================================================================
	// Cooldowns
	// =================================================================
	bool LegendaryTracker::InCooldown(std::uint32_t a_actorFormID) const
	{
		std::shared_lock lk(_mutex);
		auto it = _cooldownEnd.find(a_actorFormID);
		if (it == _cooldownEnd.end()) return false;
		return Now() < it->second;
	}

	void LegendaryTracker::StartCooldown(std::uint32_t a_actorFormID, float a_seconds)
	{
		std::unique_lock lk(_mutex);
		_cooldownEnd[a_actorFormID] = Now() + std::max(0.0f, a_seconds);
	}

	void LegendaryTracker::PruneStale()
	{
		const float now = Now();
		std::unique_lock lk(_mutex);
		for (auto it = _recentMutations.begin(); it != _recentMutations.end();) {
			if ((now - it->second) > 10.0f) it = _recentMutations.erase(it);
			else ++it;
		}
		for (auto it = _cooldownEnd.begin(); it != _cooldownEnd.end();) {
			if (now > it->second + 5.0f) it = _cooldownEnd.erase(it);
			else ++it;
		}
		// snapshots are short-lived; cap to a reasonable size
		if (_snapshots.size() > 256) {
			_snapshots.clear();
		}
	}
}

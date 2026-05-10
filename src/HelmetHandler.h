#pragma once

#include "PCH.h"
#include "Settings.h"

namespace HSK
{
	// Result of inspecting an actor's head/helmet armor.
	// Equipped pieces with kObjectHealth ~0 (vanilla-broken, still in inventory)
	// do not count as head armor; PA frame-only bucket applies only when no
	// head-slot ARMO is equipped at all.
	struct HelmetInfo
	{
		bool   hasHeadArmor{ false };       // any protecting armor covering slot 30/31/32
		bool   isPowerArmor{ false };       // PA helmet (ArmorTypePower / kPowerArmor extra)
		float  ballisticAR{ 0.0f };         // ballistic armor rating of head piece (sum if multiple)
		float  energyAR{ 0.0f };            // energy armor rating
		RE::TESObjectARMO* headArmor{ nullptr };   // the actual ARMO form for knockoff
	};

	class HelmetHandler
	{
	public:
		static HelmetHandler* GetSingleton()
		{
			static HelmetHandler singleton;
			return &singleton;
		}

		// Inspect the actor's currently equipped head armor.
		[[nodiscard]] HelmetInfo InspectHead(RE::Actor* a_actor) const;

		struct KnockoffResult
		{
			bool  passed{ false };
			float chance{ 0.0f };
		};

		// Roll knockoff chance for the given caliber.
		[[nodiscard]] KnockoffResult ShouldKnockOff(Caliber a_caliber, bool a_isPowerArmor) const;

	// Knock the helmet off (deferred to the F4SE task interface). Safe to call from event-sink threads.
	// If a_isPlayer is true, shows a notification and tracks the helmet for auto-reequip.
	// If a_isFollower is true, records the dropped ref so it can be restored post-combat.
	// a_flyDir: world-space impulse direction hint (from HeadshotLogic: shot axis + UI
	//           sliders). KnockOff normalizes it; zero length falls back to world +Z.
	void KnockOff(RE::Actor* a_actor, RE::TESObjectARMO* a_helmet,
		bool a_isPlayer = false, bool a_isFollower = false,
		RE::NiPoint3 a_flyDir = { 0.0f, 0.0f, 0.0f });

		// Restore all pending follower helmet knockoffs: move the dropped world
		// refs back into each follower's inventory and re-equip them. Called on
		// player-combat-end and on PostLoadGame. No-op if nothing is pending.
		void RestoreFollowerHelmets();

		// Called when an item enters the player's inventory. If it matches a tracked
		// knocked-off helmet, auto-equips it and clears the tracking.
		void OnPlayerItemAdded(std::uint32_t a_baseFormID);

		// Remember which actors have had their helmet knocked off this game session.
		// Subsequent shots can skip the helmet check.
		void           MarkBareHead(std::uint32_t a_actorFormID);
		[[nodiscard]] bool IsBareHead(std::uint32_t a_actorFormID) const;
		void           ClearBareHead(std::uint32_t a_actorFormID);

		// Shotgun pellet grouping: track the timestamp of the last hit on each actor so
		// that subsequent pellets in the same volley can be detected and treated as
		// "post-knockoff" hits.
		void           NoteHit(std::uint32_t a_actorFormID);
		[[nodiscard]] bool RecentHit(std::uint32_t a_actorFormID, float a_windowSec) const;

		// Resolve engine functions that need version-aware REL::IDs.
		// Must be called after address library is loaded (F4SEPlugin_Load).
		void ResolveEngineFunctions();

	private:
		HelmetHandler() = default;
		~HelmetHandler() = default;
		HelmetHandler(const HelmetHandler&) = delete;
		HelmetHandler& operator=(const HelmetHandler&) = delete;

		mutable std::shared_mutex _mutex;
		std::unordered_set<std::uint32_t>     _bareHeads;
		std::unordered_map<std::uint32_t, float> _lastHitTime;

	// Tracked FormID of the player's knocked-off helmet (for auto-reequip)
	mutable std::atomic<std::uint32_t> _playerKnockedHelmetID{ 0 };

	// Dropped world ref for the player's helmet (for HUD tracker + light)
	std::atomic<std::uint32_t> _playerDroppedRefID{ 0 };
	std::atomic<std::uint32_t> _playerHelmetLightRefID{ 0 };  // placed light ref (for cleanup)
	// Bumped on each new player tracker; in-flight threads exit when their session != current.
	std::atomic<std::uint32_t> _trackerSession{ 0 };
	void StartHelmetTracker(std::uint32_t a_sessionId);
	void PlaceHelmetLight(RE::TESObjectREFR* a_helmetRef);
	void CleanupHelmetLight();

		// Pending follower helmet knockoffs awaiting combat-end restore.
		// Followers are capped at ~2-3, so a flat vector is cheaper than a map.
		struct FollowerKnockoffEntry
		{
			std::uint32_t actorFormID{ 0 };   // the follower
			std::uint32_t helmetBaseID{ 0 };  // base ARMO form (fallback if dropped ref is gone)
			std::uint32_t droppedRefID{ 0 };  // world ref returned by DropObject
		};
		std::mutex _followerMutex;
		std::vector<FollowerKnockoffEntry> _followerKnockoffs;

		// Internal helper -- caller must hold _followerMutex.
		void RecordFollowerKnockoff(std::uint32_t a_actorID, std::uint32_t a_helmetID, std::uint32_t a_droppedRefID);

		// Combat-end watcher: detached thread that polls the player's
		// IsInCombat() every ~750ms. Fires RestoreFollowerHelmets() and
		// exits when the player transitions from in-combat to out-of-combat.
		// Only one watcher runs at a time (guarded by _watcherRunning).
		std::atomic<bool> _watcherRunning{ false };
		void StartCombatEndWatcher();

		// TESObjectREFR::ApplyEffectShader (thiscall as free fn: explicit this in RCX)
		using ApplyEffectShaderFn = void* (*)(RE::TESObjectREFR*, RE::TESEffectShader*, float, RE::TESObjectREFR*, bool, bool, RE::NiAVObject*, bool);
		ApplyEffectShaderFn _applyShaderFn{ nullptr };

		// Apply highlight shader to a dropped ref (PostNG)
		void ApplyHelmetShader(RE::TESObjectREFR* a_ref) const;

		// PreNG fallback: pulse-blink the helmet's 3D visibility every ~400ms
		// so the player can spot it.  Runs on a detached thread, stops when
		// the ref is picked up, the duration elapses, or the ref is destroyed.
		void ApplyHelmetBlink(RE::TESObjectREFR* a_ref) const;

		[[nodiscard]] static float Now();
	};
}

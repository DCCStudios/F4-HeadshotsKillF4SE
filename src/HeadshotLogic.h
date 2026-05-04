#pragma once

#include "PCH.h"
#include "GameDefinitions.h"
#include "Settings.h"
#include "AmmoClassifier.h"
#include "ActorClassifier.h"
#include "HelmetHandler.h"
#include "HeadshotSpell.h"
#include "PlayerFeedback.h"

namespace HSK
{
	class HeadshotLogic
	{
	public:
		// Top-level entry called by HitEventHandler on every TESHitEvent.
		// Performs all filtering, classification, and dispatches the kill / knockoff.
		static void EvaluateHit(const RE::TESHitEvent& a_event);

	private:
		// =========================================================
		// Melee headshot processing (knockoff only, no instakill)
		// =========================================================
		static void EvaluateMeleeHit(const RE::TESHitEvent& a_event,
			RE::Actor* a_target, const RE::TESObjectWEAP* a_weap);

		// =========================================================
		// Filter helpers (pure / no side effects)
		// =========================================================
		[[nodiscard]] static bool PassesGlobalFilters(const RE::TESHitEvent& a_event,
			RE::Actor* a_target, const RE::TESObjectWEAP* a_weap, const RE::TESAmmo* a_ammo);

		[[nodiscard]] static bool PassesVictimFilters(const ActorInfo& a_info);

		[[nodiscard]] static bool IsHeadshot(const RE::HitData& a_hd, RE::Actor* a_target,
			ActorCategory a_cat);

		// Geometric fallback for armored creatures whose damageLimb doesn't report kHead.
		[[nodiscard]] static bool DetectFaceHitGeometric(const RE::HitData& a_hd,
			RE::Actor* a_target);

		// Geometric headshot detection from attacker's aim (for projectile-mod
		// hits where HitData is unavailable).
		[[nodiscard]] static bool DetectHeadshotFromAim(RE::Actor* a_target,
			RE::Actor* a_attacker);

		// =========================================================
		// Classification + chance calculation
		// =========================================================
		struct ChanceContext
		{
			Caliber     caliber{ Caliber::Pistol };
			DamageType  dmgType{ DamageType::Ballistic };
			float       ammoDamage{ 0.0f };
			float       distance{ 0.0f };    // game units between attacker and target
			bool        isSneaking{ false };
			bool        isCritical{ false };
			bool        isInVATS{ false };
		};

		[[nodiscard]] static float ComputeChance(
			const ChanceContext& a_ctx,
			const ActorInfo& a_actorInfo,
			const HelmetInfo& a_helmetInfo);

		// =========================================================
		// Kill application
		// =========================================================
		// a_impactDir: world-space direction the projectile/strike was moving
		//              at impact (NOT normalized; can carry magnitude). Zero
		//              vector = use horizontal aggressor->target fallback.
		static void ScheduleKill(RE::Actor* a_target, RE::Actor* a_aggressor,
			bool a_isPlayerOrFollower,
			RE::NiPoint3 a_impactDir = { 0.0f, 0.0f, 0.0f });

		static void ApplyKillImpulse(RE::Actor* a_target, RE::Actor* a_aggressor,
			RE::NiPoint3 a_impactDir = { 0.0f, 0.0f, 0.0f });

		// =========================================================
		// Two-shot rule tracking for player / followers
		// =========================================================
		struct TwoShotEntry
		{
			std::chrono::steady_clock::time_point firstHitTime;
		};
		static inline std::unordered_map<std::uint32_t, TwoShotEntry> _twoShotMap;
		static inline std::mutex _twoShotMutex;

		[[nodiscard]] static bool IsWithinTwoShotWindow(std::uint32_t a_actorID);
		static void RecordFirstShot(std::uint32_t a_actorID);
		static void ClearTwoShot(std::uint32_t a_actorID);

		// =========================================================
		// Helmet knockoff grace period -- prevents the same shot
		// (or near-simultaneous pellets) from both knocking off
		// the helmet AND instakilling on the now-bare head.
		// =========================================================
		static inline std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> _knockoffGrace;
		static inline std::mutex _knockoffGraceMutex;

		static void        RecordKnockoffGrace(std::uint32_t a_actorID);
		[[nodiscard]] static bool InKnockoffGrace(std::uint32_t a_actorID);
	};
}

#include "HeadshotLogic.h"
#include "LegendaryTracker.h"

#include <cmath>

namespace HSK
{
	// =================================================================
	// Local helpers
	// =================================================================

	// a_rawDir: world-space shot/strike direction (ranged: impactDir; melee: hitDir or line to target).
	// a_againstShot: 0 = fly along that vector, 1 = opposite (default), linear blend in between.
	// a_upLift: added to Z before KnockOff re-normalizes (higher = more upward arc).
	static RE::NiPoint3 BuildHelmetFlyDirection(const RE::NiPoint3& a_rawDir, float a_againstShot, float a_upLift)
	{
		const float t = std::clamp(a_againstShot, 0.0f, 1.0f);
		const float s = 1.0f - 2.0f * t;
		RE::NiPoint3 d{ a_rawDir.x * s, a_rawDir.y * s, a_rawDir.z * s };
		const float fl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
		if (fl > 0.001f) {
			const float inv = 1.0f / fl;
			d.x *= inv;
			d.y *= inv;
			d.z *= inv;
		}
		d.z += a_upLift;
		return d;
	}

	static std::mt19937& Rng()
	{
		thread_local std::mt19937 gen{ std::random_device{}() };
		return gen;
	}

	static const char* LimbName(std::uint32_t a_limb)
	{
		switch (a_limb) {
		case 0xFFFFFFFFu: return "None";
		case 0:  return "Torso";
		case 1:  return "Head1";
		case 2:  return "Head2";
		case 3:  return "LeftArm1";
		case 4:  return "LeftArm2";
		case 5:  return "RightArm1";
		case 6:  return "RightArm2";
		case 7:  return "LeftLeg1";
		case 8:  return "LeftLeg2";
		case 9:  return "LeftLeg3";
		case 10: return "RightLeg1";
		case 11: return "RightLeg2";
		case 12: return "RightLeg3";
		case 13: return "Brain";
		default: {
			static thread_local char buf[24];
			snprintf(buf, sizeof(buf), "Unknown(%u)", a_limb);
			return buf;
		}
		}
	}

	static std::string HitFlagsString(std::uint32_t a_flags)
	{
		std::string out;
		auto add = [&](const char* name) {
			if (!out.empty()) out += '|';
			out += name;
		};
		if (a_flags & kHitFlag_NoDamage)    add("NoDamage");
		if (a_flags & kHitFlag_Bash)        add("Bash");
		if (a_flags & kHitFlag_Sneak)       add("Sneak");
		if (a_flags & kHitFlag_Recoil)      add("Recoil");
		if (a_flags & kHitFlag_Explosion)   add("Explosion");
		if (a_flags & kHitFlag_Melee)       add("Melee");
		if (a_flags & kHitFlag_Ranged)      add("Ranged");
		if (a_flags & kHitFlag_Critical)    add("Critical");
		if (a_flags & kHitFlag_PowerAttack) add("PowerAttack");
		if (out.empty()) out = "0";
		return out;
	}

	static bool RollPercent(float a_chancePercent)
	{
		if (a_chancePercent <= 0.0f)   return false;
		if (a_chancePercent >= 100.0f) return true;
		std::uniform_real_distribution<float> dist(0.0f, 100.0f);
		return dist(Rng()) < a_chancePercent;
	}

	// Kill-impulse / head snap: humanoids only (feral ghouls are Humanoid + isFeralGhoul).
	// Synths and other categories use a different skeleton / rig and are excluded.
	[[nodiscard]] static bool ShouldApplyHeadSnapImpulse(const ActorInfo& a_info) noexcept
	{
		return a_info.category == ActorCategory::Humanoid;
	}

	static bool IsRanged(const RE::HitData& a_hd, const RE::TESObjectWEAP* a_weap)
	{
		if (IsMelee(a_hd))      return false;
		if (IsExplosion(a_hd))  return false;
		// Gun bash: weapon is still kGun but the hit is melee motion, not a round.
		// Bash often does NOT set kMelee, so it must be excluded explicitly.
		if ((a_hd.flags & kHitFlag_Bash) != 0) return false;
		if (!a_weap)            return false;
		const auto type = a_weap->weaponData.type;
		if (type != RE::WEAPON_TYPE::kGun) return false;
		// Pistol whip / gun bash: engine often leaves weaponForm set but omits ammo
		// (no cartridge). Real gunfire virtually always has HitData::ammo populated.
		if (!a_hd.ammo) return false;
		return true;
	}

	// When HitData is sparse (weaponForm + flags both zero), we infer headshots from
	// aim for projectile-mod compatibility. The game also emits duplicate events with
	// zero damage, zero impact, and no projectile — those must not use aim inference or
	// they false-positive whenever the crosshair is on the head (e.g. after a bash).
	static bool HasPlausibleImpactForAimFallback(const RE::TESHitEvent& a_event, const RE::HitData& a_hd)
	{
		if (GetTotalDamage(a_hd) > 0.001f) return true;
		if (a_event.projectileFormID != 0) return true;
		const auto& imp = a_hd.impactData;
		return std::fabs(imp.hitPosX) + std::fabs(imp.hitPosY) + std::fabs(imp.hitPosZ) > 1.0f;
	}

	// When TESHitEvent::usesHitData is false (projectile-mod compatibility),
	// reconstruct weapon + ammo from the cause actor's equipment.
	struct ResolvedWeaponAmmo
	{
		const RE::TESObjectWEAP* weapon{ nullptr };
		const RE::TESAmmo*       ammo{ nullptr };
	};

	static ResolvedWeaponAmmo ResolveWeaponAmmoFallback(
		const RE::TESHitEvent& a_event, RE::Actor* a_causeActor)
	{
		ResolvedWeaponAmmo result;

		if (a_event.sourceFormID != 0) {
			auto* form = RE::TESForm::GetFormByID(a_event.sourceFormID);
			if (form && form->IsWeapon()) {
				result.weapon = static_cast<const RE::TESObjectWEAP*>(form);
			}
		}

		if (a_causeActor) {
			try {
				auto* proc = a_causeActor->currentProcess;
				if (proc && proc->middleHigh) {
					auto* mh = proc->middleHigh;
					for (std::uint32_t i = 0; i < mh->equippedItems.size(); ++i) {
						const auto& ei = mh->equippedItems[i];
						auto* form = ei.item.object;
						if (!form || !form->IsWeapon()) continue;

						if (!result.weapon) {
							result.weapon = static_cast<const RE::TESObjectWEAP*>(form);
						}
						if (ei.data) {
							auto* weapData = static_cast<RE::EquippedWeaponData*>(ei.data.get());
							if (weapData && weapData->ammo) {
								result.ammo = weapData->ammo;
							}
						}
						if (!result.ammo) {
							result.ammo = a_causeActor->GetCurrentAmmo(ei.equipIndex);
						}
						break;
					}
				}
			} catch (...) {
				// MiddleHighProcessData access can fail on some engine versions
			}
		}

		return result;
	}

	// =================================================================
	// Top-level entry
	// =================================================================
	void HeadshotLogic::EvaluateHit(const RE::TESHitEvent& a_event)
	{
		const RE::HitData& hd = a_event.hitData;
		const auto*        settings = Settings::GetSingleton();

		// 0. VERY EARLY log so we always know the sink fires.
		//    This is gated on debugLogging; no pointer derefs that could crash.
		if (settings->debugLogging) {
			const std::uint32_t targetFID = a_event.target ? a_event.target->GetFormID() : 0u;
			const std::uint32_t causeFID  = a_event.cause  ? a_event.cause->GetFormID()  : 0u;
			logger::info("[HSK] SINK: target=0x{:08X} cause=0x{:08X} weaponForm={} ammo={} flags=0x{:X} srcFormID=0x{:08X} projFormID=0x{:08X}",
				targetFID, causeFID,
				hd.weaponForm ? "yes" : "no",
				hd.ammo ? "yes" : "no",
				hd.flags,
				a_event.sourceFormID, a_event.projectileFormID);
		}

		// 1. Resolve target actor
		auto* target = a_event.target;
		if (!target) return;
		auto* actor = target->As<RE::Actor>();
		if (!actor) return;
		const bool alreadyDead = actor->IsDead(true);

		// 1b. Resolve cause actor (attacker) -- used throughout
		auto* aggressor = a_event.cause ? a_event.cause->As<RE::Actor>() : nullptr;

		// 1c. Detect whether HitData is populated by examining the actual
		//     contents. We do NOT use the `usesHitData` field at 0x100
		//     because its offset / meaning differs between OG and NG.
		//     Projectile-conversion mods zero the entire HitData.
		const bool hasHitData = (hd.weaponForm != nullptr || hd.flags != 0);

		// 2. Resolve weapon + ammo
		const RE::TESObjectWEAP* weap = nullptr;
		const RE::TESAmmo* ammo = nullptr;

		if (hasHitData) {
			weap = static_cast<const RE::TESObjectWEAP*>(hd.weaponForm);
			ammo = hd.ammo;
		} else {
			try {
				auto resolved = ResolveWeaponAmmoFallback(a_event, aggressor);
				weap = resolved.weapon;
				ammo = resolved.ammo;
			} catch (...) {
				if (settings->debugLogging) {
					logger::warn("[HSK] ResolveWeaponAmmoFallback threw; skipping hit");
				}
				return;
			}
		}

		// --- Debug: log every hit with limb, flags, weapon, ammo ---
		if (settings->debugLogging) {
			const std::uint32_t limb = hasHitData ? GetDamageLimb(hd) : 0xFFFFFFFFu;
			const bool isHead = hasHitData ? IsHeadLimb(limb) : false;
			const float totalDmg = hasHitData ? GetTotalDamage(hd) : 0.0f;

			const char* weapName = weap ? weap->GetFormEditorID() : "<none>";
			if (!weapName) weapName = "<no-edid>";
			const char* ammoName = ammo ? ammo->GetFormEditorID() : "<none>";
			if (!ammoName) ammoName = "<no-edid>";

			const char* targetName = actor->GetFormEditorID();
			if (!targetName || !targetName[0]) targetName = "<no-edid>";

			const auto& imp = hd.impactData;

			logger::info("[HSK] HIT: target=0x{:08X}({}) limb={}({}) isHead={} dead={} "
				"flags={} totalDmg={:.1f} "
				"weapon='{}' ammo='{}' "
				"hitPos=({:.0f},{:.0f},{:.0f}) hitDir=({:.2f},{:.2f},{:.2f}) "
				"hasHitData={} srcFormID=0x{:08X} projFormID=0x{:08X}",
				actor->GetFormID(), targetName,
				limb, LimbName(limb), isHead, alreadyDead,
				HitFlagsString(hd.flags), totalDmg,
				weapName, ammoName,
				imp.hitPosX, imp.hitPosY, imp.hitPosZ,
				imp.hitDirX, imp.hitDirY, imp.hitDirZ,
				hasHitData, a_event.sourceFormID, a_event.projectileFormID);
		}

		// 2b. Melee headshot path -- only when we have actual HitData with melee flag.
		if (hasHitData && IsMelee(hd) && !alreadyDead) {
			EvaluateMeleeHit(a_event, actor, weap);
			return;
		}

		// 3. Pre-filters
		if (hasHitData) {
			if (!PassesGlobalFilters(a_event, actor, weap, ammo)) return;
		} else {
			// Sparse HitData (projectile-conversion etc.): still reject bash/melee
			// if flags were written — same as IsRanged for gun-bash edge cases.
			if ((hd.flags & (kHitFlag_Bash | kHitFlag_Melee)) != 0) return;
			if (!weap || weap->weaponData.type != RE::WEAPON_TYPE::kGun) return;
			if (!ammo) return;
			if (ActorClassifier::GetSingleton()->IsRaceBlocklisted(actor)) return;
			if (ActorClassifier::GetSingleton()->HasImmuneKeyword(actor)) return;
		}

		// 4. Classify actor + ammo
		auto actorInfo = ActorClassifier::GetSingleton()->Classify(actor);
		auto ammoEntry = AmmoClassifier::GetSingleton()->Classify(ammo);

		if (!PassesVictimFilters(actorInfo)) return;

		if (ammoEntry.caliber == Caliber::Excluded) return;
		if (actorInfo.category == ActorCategory::Robot) return;

		// 5. Headshot detection
		const bool isHeadshot = hasHitData
			? IsHeadshot(hd, actor, actorInfo.category)
			: (HasPlausibleImpactForAimFallback(a_event, hd) && DetectHeadshotFromAim(actor, aggressor));
		if (!isHeadshot) return;

		// 5a. Build the world-space impact direction (where the projectile was
		//     MOVING when it hit). Priority:
		//       1. projectileDir from HitData -- world-space projectile travel dir,
		//          populated by the engine for most projectile impacts.
		//       2. aggressor->target line -- always world-space; equivalent to the
		//          bullet flight path in the common case. Preferred over hitDir
		//          because hitDir is stored in the actor's LOCAL body space and
		//          would need a rotation transform to be useful as world-space.
		//       3. hitDir from HitData -- local-body-space last resort (no aggressor).
		RE::NiPoint3 impactDir{ 0.0f, 0.0f, 0.0f };
		const bool haveProjectileDir = hasHitData &&
			std::abs(hd.impactData.projectileDirX) + std::abs(hd.impactData.projectileDirY) +
			std::abs(hd.impactData.projectileDirZ) > 0.01f;
		if (haveProjectileDir) {
			impactDir = { hd.impactData.projectileDirX, hd.impactData.projectileDirY, hd.impactData.projectileDirZ };
		} else if (aggressor) {
			impactDir.x = actor->data.location.x - aggressor->data.location.x;
			impactDir.y = actor->data.location.y - aggressor->data.location.y;
			impactDir.z = actor->data.location.z - aggressor->data.location.z;
		} else if (hasHitData) {
			const auto& imp = hd.impactData;
			if (std::abs(imp.hitDirX) + std::abs(imp.hitDirY) + std::abs(imp.hitDirZ) > 0.01f) {
				impactDir = { imp.hitDirX, imp.hitDirY, imp.hitDirZ };
			}
		}

		// 5b. Already-dead targets: just stack additional head impulses during
		//     the death animation and bail -- no kill/chance/helmet logic.
		if (alreadyDead) {
			if (settings->killImpulse.enabled && !actorInfo.isPlayer &&
				ShouldApplyHeadSnapImpulse(actorInfo)) {
				ApplyKillImpulse(actor, aggressor, impactDir);
			}
			return;
		}

		const std::uint32_t actorID = actor->GetFormID();

		const bool isInVATS   = hasHitData ? (hd.vatsCommand != nullptr) : false;
		const bool isCritical = hasHitData ? ((hd.flags & kHitFlag_Critical) != 0) : false;

		// H: VATS/critical gating -- in VATS, only critical hits proceed
		if (isInVATS && settings->advanced.vatsRequiresCritical && !isCritical) {
			if (settings->debugLogging) {
				logger::info("[HSK] HEADSHOT on 0x{:08X} suppressed -- VATS non-critical (vatsRequiresCritical=true)", actorID);
			}
			return;
		}

		HelmetHandler::GetSingleton()->NoteHit(actorID);

		// Player headshot feedback (tinnitus + screen effect) fires on ANY
		// headshot to the player regardless of kill chance outcome.
		if (actorInfo.isPlayer) {
			PlayerFeedback::GetSingleton()->OnPlayerHeadshot();
		}

		// Compute distance and sneak state for chance context
		float distance = 0.0f;
		bool isSneaking = false;
		if (aggressor) {
			const auto& tp = actor->data.location;
			const auto& ap = aggressor->data.location;
			const float dx = tp.x - ap.x;
			const float dy = tp.y - ap.y;
			const float dz = tp.z - ap.z;
			distance = std::sqrt(dx * dx + dy * dy + dz * dz);
			isSneaking = hasHitData ? ((hd.flags & kHitFlag_Sneak) != 0) : false;
		}

		const char* weapEdid = weap ? weap->GetFormEditorID() : nullptr;

		if (settings->debugLogging) {
			logger::info("[HSK] HEADSHOT: actor=0x{:08X} cat={} feral={} caliber={} dmgType={} ammoDmg={:.1f} dist={:.0f} sneak={} crit={} vats={} legendary={} PA={} deathclaw={} queen={}",
				actorID, ActorCategoryName(actorInfo.category), actorInfo.isFeralGhoul,
				CaliberDisplay(ammoEntry.caliber),
				ammoEntry.damageType == DamageType::Energy ? "Energy" : "Ballistic",
				ammoEntry.ammoDamage, distance, isSneaking, isCritical, isInVATS,
				actorInfo.isLegendary, actorInfo.isInPowerArmor,
				actorInfo.isDeathclaw, actorInfo.isMirelurkQueen);
			logger::info("[HSK]   weapon='{}' ammo='{}' (0x{:08X}) plugin='{}' classifiedAs={} reason='{}'",
				weapEdid ? weapEdid : "<null>",
				ammoEntry.editorID.empty() ? "<null>" : ammoEntry.editorID.c_str(),
				ammoEntry.formID,
				ammoEntry.sourcePlugin.empty() ? "<null>" : ammoEntry.sourcePlugin.c_str(),
				CaliberDisplay(ammoEntry.caliber),
				ammoEntry.classificationReason.empty() ? "<none>" : ammoEntry.classificationReason.c_str());
		}

		// 6. Always inspect head armor (J: no bare-head cache)
		HelmetInfo helmet = HelmetHandler::GetSingleton()->InspectHead(actor);

		// 7. Compute kill chance
		ChanceContext ctx;
		ctx.caliber    = ammoEntry.caliber;
		ctx.dmgType    = ammoEntry.damageType;
		ctx.ammoDamage = ammoEntry.ammoDamage;
		ctx.distance   = distance;
		ctx.isSneaking = isSneaking;
		ctx.isCritical = isCritical;
		ctx.isInVATS   = isInVATS;
		const float chance = ComputeChance(ctx, actorInfo, helmet);

		const bool killRolled = RollPercent(chance);

		if (settings->debugLogging) {
			const char* helmetEdid = helmet.headArmor ? helmet.headArmor->GetFormEditorID() : nullptr;
			logger::info("[HSK]   chance={:.1f}%% killRolled={} headArmor={} balAR={:.0f} eneAR={:.0f} helmet='{}' isPA={}",
				chance, killRolled,
				helmet.hasHeadArmor, helmet.ballisticAR, helmet.energyAR,
				helmetEdid ? helmetEdid : (helmet.hasHeadArmor ? "<no-form>" : "<none>"),
				helmet.isPowerArmor);
		}

		// 7b. Head snap on all headshots (non-player humanoids / feral ghouls only)
		if (settings->killImpulse.enabled && settings->killImpulse.applyOnAllHeadshots &&
			!killRolled && !actorInfo.isPlayer && ShouldApplyHeadSnapImpulse(actorInfo)) {
			ApplyKillImpulse(actor, aggressor, impactDir);
		}

		// 7c. Helmet fly-off direction: impactDir is FROM aggressor TOWARD victim.
		//      dropFlyAgainstShot (INI/UI) blends toward opposite for knock-back;
		//      dropFlyUpLift tilts the arc upward before KnockOff normalizes again.
		const RE::NiPoint3 flyDir = BuildHelmetFlyDirection(
			impactDir, settings->helmet.dropFlyAgainstShot, settings->helmet.dropFlyUpLift);

		// 8. Helmet knockoff path
		if (!killRolled && settings->helmet.enableHelmetKnockoff && helmet.hasHeadArmor) {
			if (helmet.isPowerArmor && (ammoEntry.caliber == Caliber::Pistol || ammoEntry.caliber == Caliber::Shotgun)) {
				if (settings->debugLogging) {
					logger::info("[HSK]   knockoff skipped -- PA helmet immune to {}", CaliberDisplay(ammoEntry.caliber));
				}
				return;
			}
			// Player-specific knockoff gating
			if (actorInfo.isPlayer && !settings->playerHelmetKnockoffEnabled) {
				if (settings->debugLogging) {
					logger::info("[HSK]   knockoff skipped -- player helmet knockoff disabled");
				}
				return;
			}
			if (!helmet.headArmor) {
				return;
			}
			auto knockResult = HelmetHandler::GetSingleton()->ShouldKnockOff(ammoEntry.caliber, helmet.isPowerArmor);
			// Apply per-player chance multiplier, then re-roll against the scaled chance.
			if (actorInfo.isPlayer && settings->playerHelmetKnockoffMult != 1.0f) {
				const float scaled = std::clamp(knockResult.chance * settings->playerHelmetKnockoffMult, 0.0f, 100.0f);
				knockResult.passed = RollPercent(scaled);
				knockResult.chance = scaled;
			}
			if (settings->debugLogging) {
				const char* helmetEdid2 = helmet.headArmor->GetFormEditorID();
				logger::info("[HSK]   knockoff roll: chance={:.1f}%% passed={} helmet='{}' caliber={} PA={} isPlayer={}",
					knockResult.chance, knockResult.passed,
					helmetEdid2 ? helmetEdid2 : "<no edid>",
					CaliberDisplay(ammoEntry.caliber),
					helmet.isPowerArmor,
					actorInfo.isPlayer);
			}
			if (knockResult.passed) {
				HelmetHandler::GetSingleton()->KnockOff(actor, helmet.headArmor, actorInfo.isPlayer, actorInfo.isFollower, flyDir);
				RecordKnockoffGrace(actorID);
			}
			return;
		}

	if (!killRolled) return;

	// 8b. Knockoff grace -- don't instakill if the helmet was just knocked off
	//     by a near-simultaneous hit (same volley / multi-pellet).
	if (InKnockoffGrace(actorID)) {
		if (settings->debugLogging) {
			logger::info("[HSK]   kill suppressed -- knockoff grace period active (actor=0x{:08X})", actorID);
		}
		return;
	}

	// Shared helper: when a player kill is suppressed by protection rules, attempt
	// a normal knockoff roll (same ShouldKnockOff path + player multiplier as step 8).
	const auto TryPlayerKnockoffInsteadOfKill = [&](const char* a_reason) {
		if (settings->debugLogging) {
			logger::info("[HSK]   player kill suppressed -- {} (helmet='{}'); attempting normal knockoff roll",
				a_reason,
				helmet.headArmor ? (helmet.headArmor->GetFormEditorID() ? helmet.headArmor->GetFormEditorID() : "<no edid>") : "<none>");
		}
		if (!settings->helmet.enableHelmetKnockoff || !helmet.headArmor || !settings->playerHelmetKnockoffEnabled)
			return;
		if (helmet.isPowerArmor && (ammoEntry.caliber == Caliber::Pistol || ammoEntry.caliber == Caliber::Shotgun))
			return;

		auto knockResult = HelmetHandler::GetSingleton()->ShouldKnockOff(ammoEntry.caliber, helmet.isPowerArmor);
		if (settings->playerHelmetKnockoffMult != 1.0f) {
			const float scaled = std::clamp(knockResult.chance * settings->playerHelmetKnockoffMult, 0.0f, 100.0f);
			knockResult.passed = RollPercent(scaled);
			knockResult.chance = scaled;
		}
		if (settings->debugLogging) {
			const char* helmetEdid2 = helmet.headArmor->GetFormEditorID();
			logger::info("[HSK]   knockoff roll: chance={:.1f}%% passed={} helmet='{}' caliber={} PA={} isPlayer=true",
				knockResult.chance, knockResult.passed,
				helmetEdid2 ? helmetEdid2 : "<no edid>",
				CaliberDisplay(ammoEntry.caliber),
				helmet.isPowerArmor);
		}
		if (knockResult.passed) {
			HelmetHandler::GetSingleton()->KnockOff(actor, helmet.headArmor, /*isPlayer=*/true, /*isFollower=*/false, flyDir);
			RecordKnockoffGrace(actorID);
		}
	};

	// 8c. Helmet protection toggle -- any head armor fully blocks instakill.
	if (actorInfo.isPlayer && settings->playerHelmetProtection && helmet.hasHeadArmor) {
		TryPlayerKnockoffInsteadOfKill("helmet protection active");
		return;
	}

	// 8d. AR threshold protection -- when the toggle above is OFF, head armor with
	//     ballistic AR >= threshold still blocks instakill.
	if (actorInfo.isPlayer && !settings->playerHelmetProtection &&
		settings->playerInstakillMinAR > 0.0f &&
		helmet.hasHeadArmor && helmet.ballisticAR >= settings->playerInstakillMinAR) {
		if (settings->debugLogging) {
			logger::info("[HSK]   (head AR {:.0f} >= threshold {:.0f})",
				helmet.ballisticAR, settings->playerInstakillMinAR);
		}
		TryPlayerKnockoffInsteadOfKill("head AR meets minimum threshold");
		return;
	}

	// 9. Legendary cooldown check
		if (settings->legendary.respectLegendaryMutation && actorInfo.isLegendary) {
			if (LegendaryTracker::GetSingleton()->InCooldown(actorID)) {
				if (settings->debugLogging) {
					logger::info("[HSK]   legendary cooldown active -- suppressing kill");
				}
				return;
			}
			LegendaryTracker::GetSingleton()->SnapshotHealth(actor);
		}

		// 10. Schedule kill
		const bool isPlayerOrFollower = actorInfo.isPlayer || actorInfo.isFollower;
		ScheduleKill(actor, aggressor, isPlayerOrFollower,
			ShouldApplyHeadSnapImpulse(actorInfo), impactDir);
	}

	// =================================================================
	// Melee headshot processing (helmet knockoff only, no instakill)
	// =================================================================
	void HeadshotLogic::EvaluateMeleeHit(const RE::TESHitEvent& a_event, RE::Actor* a_target, const RE::TESObjectWEAP* a_weap)
	{
		const auto* settings = Settings::GetSingleton();
		if (!settings->melee.enableMeleeKnockoff) return;
		if (!settings->helmet.enableHelmetKnockoff) return;

		const RE::HitData& hd = a_event.hitData;

		// Skip explosions, dead targets already handled
		if (IsExplosion(hd)) return;

		// Race/keyword blocklist
		if (ActorClassifier::GetSingleton()->IsRaceBlocklisted(a_target)) return;
		if (ActorClassifier::GetSingleton()->HasImmuneKeyword(a_target)) return;

		// Classify the actor -- melee knockoff applies to humanoids and synths
		auto actorInfo = ActorClassifier::GetSingleton()->Classify(a_target);
		if (actorInfo.category != ActorCategory::Humanoid &&
			actorInfo.category != ActorCategory::Synth) return;

		// Only process head hits
		if (!IsHeadshot(hd, a_target, actorInfo.category)) return;

		// Classify the melee hit type and determine knockoff chance.
		float knockoffChance = 0.0f;
		const char* hitType = nullptr;

		const bool isBash = (hd.flags & kHitFlag_Bash) != 0;

		if (isBash && a_weap && a_weap->weaponData.type == RE::WEAPON_TYPE::kGun) {
			// Gun bash with a firearm -- check if it's non-pistol caliber
			if (!settings->melee.gunBashKnockoff) return;

			// Try to classify the gun's ammo to see if it's a pistol
			const auto* ammo = hd.ammo;
			if (ammo) {
				auto ammoEntry = AmmoClassifier::GetSingleton()->Classify(ammo);
				if (ammoEntry.caliber == Caliber::Pistol || ammoEntry.caliber == Caliber::Excluded)
					return;
			}
			knockoffChance = settings->melee.gunBashKnockoffChance;
			hitType = "gun bash";
		} else if (a_weap) {
			const auto type = a_weap->weaponData.type;
			switch (type) {
			case RE::WEAPON_TYPE::kTwoHandSword:
			case RE::WEAPON_TYPE::kTwoHandAxe:
				knockoffChance = settings->melee.meleeKnockoffChanceLarge;
				hitType = "large melee";
				break;
			case RE::WEAPON_TYPE::kOneHandSword:
			case RE::WEAPON_TYPE::kOneHandDagger:
			case RE::WEAPON_TYPE::kOneHandAxe:
			case RE::WEAPON_TYPE::kOneHandMace:
				knockoffChance = settings->melee.meleeKnockoffChanceMedium;
				hitType = "medium melee";
				break;
			default:
				return;  // fists, grenades, etc. -- no knockoff
			}
		} else {
			return;  // no weapon form
		}

		if (knockoffChance <= 0.0f) return;

		// Inspect head armor
		HelmetInfo helmet = HelmetHandler::GetSingleton()->InspectHead(a_target);
		if (!helmet.hasHeadArmor || !helmet.headArmor) return;

		// PA helmets are immune to melee knockoff (bolted to frame)
		if (helmet.isPowerArmor) return;

		// Player-specific gating
		if (actorInfo.isPlayer && !settings->playerHelmetKnockoffEnabled) return;
		if (actorInfo.isPlayer && settings->playerHelmetKnockoffMult != 1.0f) {
			knockoffChance = std::clamp(knockoffChance * settings->playerHelmetKnockoffMult, 0.0f, 100.0f);
		}

		const bool passed = RollPercent(knockoffChance);

		if (settings->debugLogging) {
			const char* helmetEdid = helmet.headArmor->GetFormEditorID();
			logger::info("[HSK]   melee knockoff: type='{}' chance={:.1f}%% passed={} helmet='{}' isPlayer={}",
				hitType, knockoffChance, passed,
				helmetEdid ? helmetEdid : "<no edid>",
				actorInfo.isPlayer);
		}

		if (!passed) return;

		// Raw strike direction: hitDir (local) or aggressor->target line (world XY).
		RE::NiPoint3 rawFly{ 0.0f, 0.0f, 0.0f };
		const auto& imp = hd.impactData;
		if (std::abs(imp.hitDirX) + std::abs(imp.hitDirY) + std::abs(imp.hitDirZ) > 0.01f) {
			rawFly = { imp.hitDirX, imp.hitDirY, imp.hitDirZ };
		} else {
			auto* aggressor = a_event.cause ? a_event.cause->As<RE::Actor>() : nullptr;
			if (aggressor) {
				rawFly.x = a_target->data.location.x - aggressor->data.location.x;
				rawFly.y = a_target->data.location.y - aggressor->data.location.y;
				rawFly.z = 0.0f;
			}
		}
		const RE::NiPoint3 flyDir = BuildHelmetFlyDirection(
			rawFly, settings->helmet.dropFlyAgainstShot, settings->helmet.dropFlyUpLift);

		HelmetHandler::GetSingleton()->KnockOff(a_target, helmet.headArmor,
			actorInfo.isPlayer, actorInfo.isFollower, flyDir);
		RecordKnockoffGrace(a_target->GetFormID());
	}

	// =================================================================
	// Filters
	// =================================================================
	bool HeadshotLogic::PassesGlobalFilters(const RE::TESHitEvent& a_event,
		RE::Actor* a_target, const RE::TESObjectWEAP* a_weap, const RE::TESAmmo* a_ammo)
	{
		const auto& hd = a_event.hitData;

		// Must be ranged gun, not melee or explosion
		if (!IsRanged(hd, a_weap)) return false;

		// Must have ammo (excludes weapons that don't use ammo, like flamers in some setups)
		if (!a_ammo) return false;

		// Race blocklist
		if (ActorClassifier::GetSingleton()->IsRaceBlocklisted(a_target)) return false;

		// User-defined immune keywords (default: ActorTypeRobot)
		if (ActorClassifier::GetSingleton()->HasImmuneKeyword(a_target)) return false;

		return true;
	}

	bool HeadshotLogic::PassesVictimFilters(const ActorInfo& a_info)
	{
		const auto* settings = Settings::GetSingleton();

		// Player / follower toggle
		if (!settings->applyToPlayerAndFollowers) {
			if (a_info.isPlayer || a_info.isFollower) return false;
		}

		// Level gap
		if (settings->levelGapThreshold > 0) {
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (player) {
				auto* npc = player->GetNPC();
				const std::uint16_t playerLvl = npc ? npc->actorData.level : 1;
				if (a_info.level > playerLvl + static_cast<std::uint16_t>(settings->levelGapThreshold)) {
					return false;
				}
			}
		}

		return true;
	}

	bool HeadshotLogic::IsHeadshot(const RE::HitData& a_hd, RE::Actor* a_target, ActorCategory a_cat)
	{
		const auto limb = GetDamageLimb(a_hd);
		if (IsHeadLimb(limb)) return true;

		// For armored creatures (Mirelurks, Radscorpions) the engine may not register
		// the limb as "head" outside VATS because the head is hidden behind their armor
		// plates. We use a geometric fallback to check whether the impact location is
		// near the head/face node.
		if (a_cat == ActorCategory::ArmoredCreature) {
			return DetectFaceHitGeometric(a_hd, a_target);
		}

		return false;
	}

	bool HeadshotLogic::DetectFaceHitGeometric(const RE::HitData& a_hd, RE::Actor* a_target)
	{
		if (!a_target) return false;

		// Impact location is the first NiPoint4 (xyz) in the impactData buffer.
		const RE::NiPoint3 impact{
			a_hd.impactData.hitPosX,
			a_hd.impactData.hitPosY,
			a_hd.impactData.hitPosZ
		};

		// Look for common head node names. F4 head bones are typically named "Head",
		// "HEAD", or "FaceTargetSource"; creatures use race-specific names.
		static constexpr const char* kHeadNames[] = {
			"Head", "HEAD", "head", "Head_skin",
			"FaceTargetSource", "Mirelurk_HeadOne", "Mirelurk_Head",
			"radScorpion_Head", "Bug_HeadJaw", "Bug_Head",
		};

		// 3D might be a NiNode pointer. Use Get3D() if available.
		auto* obj3D = a_target->Get3D();
		if (!obj3D) return false;

		for (auto* name : kHeadNames) {
			const RE::BSFixedString fname{ name };
			if (auto* node = obj3D->GetObjectByName(fname); node) {
				const auto& wp = node->world.translate;
				const float dx = wp.x - impact.x;
				const float dy = wp.y - impact.y;
				const float dz = wp.z - impact.z;
				const float distSq = dx * dx + dy * dy + dz * dz;
				// 30 unit radius around the head node should cover crab/scorpion faces.
				if (distSq < 30.0f * 30.0f) {
					return true;
				}
			}
		}

		return false;
	}

	bool HeadshotLogic::DetectHeadshotFromAim(RE::Actor* a_target, RE::Actor* a_attacker)
	{
		if (!a_target || !a_attacker) return false;

		RE::NiPoint3 eyePos{}, eyeDir{};
		try {
			a_attacker->GetEyeVector(eyePos, eyeDir, false);
		} catch (...) {
			return false;
		}

		const float dirLen = std::sqrt(
			eyeDir.x * eyeDir.x + eyeDir.y * eyeDir.y + eyeDir.z * eyeDir.z);
		if (dirLen < 0.001f) return false;
		eyeDir.x /= dirLen;
		eyeDir.y /= dirLen;
		eyeDir.z /= dirLen;

		auto* obj3D = a_target->Get3D();
		if (!obj3D) return false;

		static constexpr const char* kHeadNames[] = {
			"Head", "HEAD", "head", "Head_skin",
			"FaceTargetSource", "NPC Head [Head]",
			"Mirelurk_HeadOne", "Mirelurk_Head",
			"radScorpion_Head", "Bug_HeadJaw", "Bug_Head",
		};

		RE::NiPoint3 headPos{};
		bool foundHead = false;
		for (auto* name : kHeadNames) {
			const RE::BSFixedString fname{ name };
			if (auto* node = obj3D->GetObjectByName(fname); node) {
				headPos = node->world.translate;
				foundHead = true;
				break;
			}
		}
		if (!foundHead) return false;

		const float toHeadX = headPos.x - eyePos.x;
		const float toHeadY = headPos.y - eyePos.y;
		const float toHeadZ = headPos.z - eyePos.z;

		const float t = toHeadX * eyeDir.x + toHeadY * eyeDir.y + toHeadZ * eyeDir.z;
		if (t < 0.0f) return false;

		const float closestX = eyePos.x + eyeDir.x * t;
		const float closestY = eyePos.y + eyeDir.y * t;
		const float closestZ = eyePos.z + eyeDir.z * t;

		const float dx = closestX - headPos.x;
		const float dy = closestY - headPos.y;
		const float dz = closestZ - headPos.z;
		const float distSq = dx * dx + dy * dy + dz * dz;

		constexpr float kHeadRadius = 20.0f;
		const bool hit = distSq <= (kHeadRadius * kHeadRadius);

		if (Settings::GetSingleton()->debugLogging) {
			logger::info("[HSK]   aim-headshot check: dist={:.1f} threshold={:.1f} result={}",
				std::sqrt(distSq), kHeadRadius, hit);
		}

		return hit;
	}

	// =================================================================
	// Chance calculation
	// =================================================================
	float HeadshotLogic::ComputeChance(
		const ChanceContext& a_ctx,
		const ActorInfo& a_actorInfo,
		const HelmetInfo& a_helmetInfo)
	{
		const auto* s = Settings::GetSingleton();

		// Feral ghoul (race pattern): instakill chance ignores head armor / PA buckets.
		HelmetInfo helEff = a_helmetInfo;
		if (a_actorInfo.isFeralGhoul) {
			helEff.hasHeadArmor = false;
			helEff.ballisticAR = 0.0f;
			helEff.energyAR = 0.0f;
			helEff.isPowerArmor = false;
		}

		// ---- I: Base chance by category, with deathclaw/queen overrides ----
		float base = 0.0f;
		switch (a_actorInfo.category) {
		case ActorCategory::Humanoid:
			base = a_actorInfo.isFeralGhoul ? s->chances.feralGhoul : s->chances.humanoid;
			break;
		case ActorCategory::SuperMutant:
			base = s->chances.superMutant;
			break;
		case ActorCategory::SmallCreature:
			base = s->chances.smallCreature;
			break;
		case ActorCategory::ArmoredCreature:
			base = s->chances.armoredCreature;
			break;
		case ActorCategory::LargeCreature:
			if (a_actorInfo.isDeathclaw) {
				base = s->chances.deathclaw;
			} else if (a_actorInfo.isMirelurkQueen) {
				base = s->chances.mirelurkQueen;
			} else {
				base = s->chances.largeCreature;
			}
			break;
		case ActorCategory::Synth:
			base = s->chances.synth;
			break;
		default:
			return 0.0f;
		}

		// ---- Caliber modifiers ----
		float caliberMul = 1.0f;
		if (a_actorInfo.isFeralGhoul && a_actorInfo.category == ActorCategory::Humanoid) {
			switch (a_ctx.caliber) {
			case Caliber::Pistol:     caliberMul = s->caliberMods.pistolVsFeralGhoul; break;
			case Caliber::Shotgun:    caliberMul = s->caliberMods.shotgunVsFeralGhoul; break;
			case Caliber::Rifle:      caliberMul = s->caliberMods.rifleVsFeralGhoul; break;
			case Caliber::LargeRifle: caliberMul = s->caliberMods.largeRifleVsFeralGhoul; break;
			default: /* Excluded */   caliberMul = 0.0f; break;
			}
		} else {
			const bool isSynthBucket =
				a_actorInfo.category == ActorCategory::Synth && !helEff.isPowerArmor;
			const bool isLargeBucket =
				(a_actorInfo.category == ActorCategory::LargeCreature ||
				a_actorInfo.category == ActorCategory::SuperMutant) && !isSynthBucket;
			const bool isArmoredBucket =
				(a_actorInfo.category == ActorCategory::ArmoredCreature ||
				((a_actorInfo.category == ActorCategory::Humanoid || a_actorInfo.category == ActorCategory::Synth)
					&& helEff.hasHeadArmor && !helEff.isPowerArmor)) && !isSynthBucket;
			const bool isPABucket = helEff.isPowerArmor;

			if (isPABucket) {
				switch (a_ctx.caliber) {
				case Caliber::Pistol:     caliberMul = s->caliberMods.pistolVsPA; break;
				case Caliber::Shotgun:    caliberMul = s->caliberMods.shotgunVsPA; break;
				case Caliber::Rifle:      caliberMul = s->caliberMods.rifleVsPA; break;
				case Caliber::LargeRifle: caliberMul = s->caliberMods.largeRifleVsPA; break;
				default: caliberMul = 0.0f; break;
				}
			} else if (isSynthBucket) {
				switch (a_ctx.caliber) {
				case Caliber::Pistol:     caliberMul = s->caliberMods.pistolVsSynth; break;
				case Caliber::Shotgun:    caliberMul = s->caliberMods.shotgunVsSynth; break;
				case Caliber::Rifle:      caliberMul = s->caliberMods.rifleVsSynth; break;
				case Caliber::LargeRifle: caliberMul = s->caliberMods.largeRifleVsSynth; break;
				default: caliberMul = 0.0f; break;
				}
			} else if (isLargeBucket) {
				switch (a_ctx.caliber) {
				case Caliber::Pistol:     caliberMul = s->caliberMods.pistolVsLarge; break;
				case Caliber::Shotgun:    caliberMul = s->caliberMods.shotgunVsLarge; break;
				case Caliber::Rifle:      caliberMul = s->caliberMods.rifleVsLarge; break;
				case Caliber::LargeRifle: caliberMul = s->caliberMods.largeRifleVsLarge; break;
				default: caliberMul = 0.0f; break;
				}
			} else if (isArmoredBucket) {
				switch (a_ctx.caliber) {
				case Caliber::Pistol:     caliberMul = s->caliberMods.pistolVsArmored; break;
				case Caliber::Shotgun:    caliberMul = s->caliberMods.shotgunVsArmored; break;
				case Caliber::Rifle:      caliberMul = s->caliberMods.rifleVsArmored; break;
				case Caliber::LargeRifle: caliberMul = s->caliberMods.largeRifleVsArmored; break;
				default: caliberMul = 0.0f; break;
				}
			}
		}

		float chance = base * caliberMul;

		// ---- A+C: Sigmoid armor scaling with separate ballistic/energy AR ----
		if (helEff.hasHeadArmor && !helEff.isPowerArmor) {
			float effectiveAR = 0.0f;
			float halfAR = 0.0f;

			if (a_ctx.dmgType == DamageType::Energy) {
				effectiveAR = helEff.energyAR +
					helEff.ballisticAR * s->armorScaling.crossResistFactor;
				halfAR = s->armorScaling.energyHalfAR;
			} else {
				effectiveAR = helEff.ballisticAR;
				halfAR = s->armorScaling.ballisticHalfAR;
			}

			if (halfAR > 0.0f && effectiveAR > 0.0f) {
				const float scale = 1.0f / (1.0f + (effectiveAR / halfAR));
				chance *= scale;
			}
		}

		// ---- D: Ammo damage scaling ----
		if (s->advanced.ammoDamageInfluence > 0.0f && a_ctx.ammoDamage > 0.0f &&
			s->advanced.ammoDamageReferenceDamage > 0.0f)
		{
			const float ratio = a_ctx.ammoDamage / s->advanced.ammoDamageReferenceDamage;
			const float dmgMul = std::lerp(1.0f, std::clamp(ratio, 0.2f, 3.0f),
				s->advanced.ammoDamageInfluence);
			chance *= dmgMul;
		}

		// ---- E: Distance falloff ----
		if (s->advanced.enableDistanceFalloff && a_ctx.distance > 0.0f) {
			const float fullDist = s->advanced.distanceFullChanceUnits;
			const float zeroDist = s->advanced.distanceZeroChanceUnits;
			if (a_ctx.distance > fullDist && zeroDist > fullDist) {
				const float t = (a_ctx.distance - fullDist) / (zeroDist - fullDist);
				const float distScale = std::clamp(1.0f - t, 0.0f, 1.0f);
				chance *= distScale;
			}
		}

		// ---- F: Stealth bonus ----
		if (a_ctx.isSneaking && s->advanced.sneakBonusMul > 1.0f) {
			chance *= s->advanced.sneakBonusMul;
		}

		// ---- G: Critical hit bonus (outside VATS) ----
		if (a_ctx.isCritical && !a_ctx.isInVATS && s->advanced.criticalBonusChance > 0.0f) {
			chance += s->advanced.criticalBonusChance;
		}

		return std::clamp(chance, 0.0f, 100.0f);
	}

	// =================================================================
	// Two-shot tracking helpers (player / follower)
	// =================================================================
	bool HeadshotLogic::IsWithinTwoShotWindow(std::uint32_t a_actorID)
	{
		std::lock_guard lk(_twoShotMutex);
		auto it = _twoShotMap.find(a_actorID);
		if (it == _twoShotMap.end()) return false;

		const float windowSec = Settings::GetSingleton()->twoShotWindowSeconds;
		const auto elapsed = std::chrono::steady_clock::now() - it->second.firstHitTime;
		const auto elapsedSec = std::chrono::duration<float>(elapsed).count();
		return elapsedSec <= windowSec;
	}

	void HeadshotLogic::RecordFirstShot(std::uint32_t a_actorID)
	{
		std::lock_guard lk(_twoShotMutex);
		_twoShotMap[a_actorID] = { std::chrono::steady_clock::now() };
	}

	void HeadshotLogic::ClearTwoShot(std::uint32_t a_actorID)
	{
		std::lock_guard lk(_twoShotMutex);
		_twoShotMap.erase(a_actorID);
	}

	// =================================================================
	// Knockoff grace period -- 200ms window after a helmet is knocked
	// off during which the same actor cannot be instakilled.  Prevents
	// multi-hit weapons (shotgun pellets, rapid-fire) from both
	// knocking the helmet off AND killing on the now-bare head.
	// =================================================================
	static constexpr float kKnockoffGraceSeconds = 0.125f;

	void HeadshotLogic::RecordKnockoffGrace(std::uint32_t a_actorID)
	{
		std::lock_guard lk(_knockoffGraceMutex);
		_knockoffGrace[a_actorID] = std::chrono::steady_clock::now();
	}

	bool HeadshotLogic::InKnockoffGrace(std::uint32_t a_actorID)
	{
		std::lock_guard lk(_knockoffGraceMutex);
		auto it = _knockoffGrace.find(a_actorID);
		if (it == _knockoffGrace.end()) return false;
		const auto elapsed = std::chrono::steady_clock::now() - it->second;
		if (std::chrono::duration<float>(elapsed).count() > kKnockoffGraceSeconds) {
			_knockoffGrace.erase(it);
			return false;
		}
		return true;
	}

	// =================================================================
	// Kill impulse: insert a fake joint above the head/neck and rotate
	// it to create a head snap-back. The animation system doesn't know
	// about the inserted node, so the rotation persists and layers on
	// top of death animations. A background thread only sleeps between
	// steps; each rotation write runs on the game thread and must
	// re-resolve the NiNode by actor FormID — holding NiPointer across
	// death/ragdoll/despawn can leave dangling nodes that crash Ni
	// updates while walking near corpses (Buffout AV on HSK_HeadImpulse).
	// =================================================================
	static constexpr const char* kHeadImpulseBoneName = "HSK_HeadImpulse";

	static void BuildAxisAngleRotation(RE::NiMatrix3& a_out, const RE::NiPoint3& a_axis, float a_radians)
	{
		const float c = std::cos(a_radians);
		const float s = std::sin(a_radians);
		const float t = 1.0f - c;
		const float x = a_axis.x, y = a_axis.y, z = a_axis.z;

		a_out.entry[0].v = { t * x * x + c,     t * x * y - s * z, t * x * z + s * y, 0.0f };
		a_out.entry[1].v = { t * x * y + s * z,  t * y * y + c,     t * y * z - s * x, 0.0f };
		a_out.entry[2].v = { t * x * z - s * y,  t * y * z + s * x, t * z * z + c,     0.0f };
	}

	static RE::NiNode* GetOrInsertHeadImpulseBone(RE::NiAVObject* a_3D, RE::NiAVObject* a_headBone)
	{
		if (!a_3D || !a_headBone) return nullptr;

		const RE::BSFixedString insertedName{ kHeadImpulseBoneName };

		// Check if we already inserted one on this skeleton
		auto* existing = a_3D->GetObjectByName(insertedName);
		if (existing) {
			auto* existingNode = existing->IsNode();
			if (existingNode) return existingNode;
		}

		// The head bone must have a parent to insert between
		RE::NiNode* headParent = a_headBone->parent;
		if (!headParent) return nullptr;

		// Create new intermediate node
		auto* inserted = new RE::NiNode(1);
		if (!inserted) return nullptr;

		inserted->name = insertedName;
		inserted->local.translate = RE::NiPoint3{ 0.0f, 0.0f, 0.0f };
		inserted->local.rotate.MakeIdentity();
		inserted->local.scale = 1.0f;

		// Insert between parent and head:
		//   Before: headParent -> headBone
		//   After:  headParent -> inserted -> headBone
		headParent->AttachChild(inserted, true);
		inserted->parent = headParent;
		inserted->AttachChild(a_headBone, true);

		return inserted;
	}

	static RE::NiNode* FindHeadImpulseNode(RE::Actor* a_actor)
	{
		if (!a_actor) return nullptr;
		auto* obj3D = a_actor->Get3D();
		if (!obj3D) return nullptr;
		const RE::BSFixedString name{ kHeadImpulseBoneName };
		auto* obj = obj3D->GetObjectByName(name);
		return obj ? obj->IsNode() : nullptr;
	}

	void HeadshotLogic::ApplyKillImpulse(RE::Actor* a_target, RE::Actor* a_aggressor,
		RE::NiPoint3 a_impactDir)
	{
		const auto* settings = Settings::GetSingleton();
		if (!settings->killImpulse.enabled || !a_target) return;

		auto* obj3D = a_target->Get3D();
		if (!obj3D) return;

		// Find head bone
		static const RE::BSFixedString kHead{ "Head" };
		static const RE::BSFixedString kNeck{ "Neck" };
		auto* headNode = obj3D->GetObjectByName(kHead);
		if (!headNode) headNode = obj3D->GetObjectByName(kNeck);
		if (!headNode) {
			if (settings->debugLogging)
				logger::info("[HSK]   kill impulse: no Head/Neck node found");
			return;
		}

		// Insert (or find existing) fake joint above the head bone
		auto* impulseNode = GetOrInsertHeadImpulseBone(obj3D, headNode);
		if (!impulseNode) {
			if (settings->debugLogging)
				logger::info("[HSK]   kill impulse: failed to insert/find impulse bone");
			return;
		}

		// Build shot direction (where the bullet/strike was MOVING at impact).
		// Priority:
		//   1. Provided impact direction from HitData (full 3D, includes
		//      vertical component from arcs / sniper-from-above / etc.)
		//   2. Aggressor->target line in XY (legacy fallback)
		// The impact normal is ~ -shotDir; we want the head to rotate AWAY
		// from the impact normal => around an axis perpendicular to shotDir.
		RE::NiPoint3 shotDir{ 0.0f, 1.0f, 0.0f };
		bool haveImpact = false;
		{
			const float impLenSq = a_impactDir.x * a_impactDir.x +
				a_impactDir.y * a_impactDir.y + a_impactDir.z * a_impactDir.z;
			if (impLenSq > 1e-6f) {
				const float invLen = 1.0f / std::sqrt(impLenSq);
				shotDir.x = a_impactDir.x * invLen;
				shotDir.y = a_impactDir.y * invLen;
				shotDir.z = a_impactDir.z * invLen;
				haveImpact = true;
			} else {
				RE::TESObjectREFR* shooter = a_aggressor
					? static_cast<RE::TESObjectREFR*>(a_aggressor)
					: static_cast<RE::TESObjectREFR*>(RE::PlayerCharacter::GetSingleton());
				if (shooter) {
					shotDir.x = a_target->data.location.x - shooter->data.location.x;
					shotDir.y = a_target->data.location.y - shooter->data.location.y;
					shotDir.z = 0.0f;
					const float len = std::sqrt(shotDir.x * shotDir.x + shotDir.y * shotDir.y);
					if (len > 1.0f) {
						shotDir.x /= len;
						shotDir.y /= len;
					}
				}
			}
		}

		// Rotation axis: perpendicular to shotDir AND to the world-up vector
		// projected so that rotating the head around it moves the top of the
		// head in the +shotDir direction (i.e. AWAY from the impact normal,
		// in the same direction the projectile was carrying force).
		// Math: rotAxis = worldUp x shotDir (right-hand rule). For a horizontal
		// shotDir = (sx, sy, 0) this evaluates to (-sy, sx, 0) (the existing
		// formula). For arbitrary 3D shotDir it correctly tilts the head
		// pitching forward/back AND rolling sideways for off-axis hits.
		RE::NiPoint3 rotAxis{ -shotDir.y, shotDir.x, 0.0f };
		const float axLen = std::sqrt(rotAxis.x * rotAxis.x +
			rotAxis.y * rotAxis.y + rotAxis.z * rotAxis.z);
		if (axLen > 0.001f) {
			rotAxis.x /= axLen;
			rotAxis.y /= axLen;
			rotAxis.z /= axLen;
		} else {
			// shotDir is purely vertical (rare: shot from directly above/below).
			// Pick an arbitrary horizontal axis so we still get *some* snap.
			rotAxis = { 1.0f, 0.0f, 0.0f };
		}

		// Add upward bias so the head snaps back AND up
		rotAxis.z += settings->killImpulse.upwardBias;
		const float axLen2 = std::sqrt(rotAxis.x * rotAxis.x +
			rotAxis.y * rotAxis.y + rotAxis.z * rotAxis.z);
		if (axLen2 > 0.001f) {
			rotAxis.x /= axLen2;
			rotAxis.y /= axLen2;
			rotAxis.z /= axLen2;
		}

		// Convert degrees to radians
		const float angleRad = settings->killImpulse.magnitude * 0.01745329f;

		// Bump the generation stored on the node. Any in-flight decay tasks
		// from an older impulse will see this newer value and bail, so the
		// old & new decays don't fight over impulseNode->local.rotate.
		// userData is a uintptr_t on NiAVObject; we own it on this inserted
		// bone (we created it; nothing else writes here).
		const std::uintptr_t myGen = ++impulseNode->userData;

		// Apply rotation to the inserted bone (initial pose, on main thread)
		BuildAxisAngleRotation(impulseNode->local.rotate, rotAxis, angleRad);

		if (settings->debugLogging) {
			logger::info("[HSK]   kill impulse (fake joint): shotDir=({:.2f},{:.2f},{:.2f}) "
				"axis=({:.2f},{:.2f},{:.2f}) angle={:.1f}deg src={} gen={}",
				shotDir.x, shotDir.y, shotDir.z,
				rotAxis.x, rotAxis.y, rotAxis.z, settings->killImpulse.magnitude,
				haveImpact ? "impactDir" : "aggressor-line", myGen);
		}

		// Decay the rotation back to identity over the configured duration.
		// Each step posts to the main thread to safely modify the node.
		// Do NOT capture NiPointer<NiNode>: after kill the skeleton can be
		// torn down or rebuilt while decay timers still fire; a stale node
		// pointer crashes the Ni scene graph (RCX=0 / AV under Buffout4
		// while iterating past HSK_HeadImpulse). Re-resolve by actor FormID
		// and bone name each tick; skip if the actor or bone is gone.
		const float decayDuration = settings->killImpulse.decayDuration;
		const int   steps = std::max(5, static_cast<int>(decayDuration * 20.0f)); // ~20 steps/sec
		const float stepDelay = decayDuration / static_cast<float>(steps);

		const std::uint32_t actorID = a_target->GetFormID();

		std::thread([actorID, rotAxis, angleRad, steps, stepDelay, myGen]() {
			for (int i = 1; i <= steps; ++i) {
				std::this_thread::sleep_for(
					std::chrono::milliseconds(static_cast<int>(stepDelay * 1000.0f)));

				const float t = static_cast<float>(i) / static_cast<float>(steps);
				const float curAngle = angleRad * (1.0f - t);
				const int curStep = i;

				F4SE::GetTaskInterface()->AddTask([actorID, rotAxis, curAngle, curStep, steps, myGen]() {
					auto* actor = RE::TESForm::GetFormByID<RE::Actor>(actorID);
					if (!actor) return;
					auto* node = FindHeadImpulseNode(actor);
					if (!node) return;
					if (node->userData != myGen) return;
					if (curStep >= steps) {
						node->local.rotate.MakeIdentity();
					} else {
						BuildAxisAngleRotation(node->local.rotate, rotAxis, curAngle);
					}
				});
			}
		}).detach();
	}

	// =================================================================
	// Kill scheduling
	// =================================================================
	void HeadshotLogic::ScheduleKill(RE::Actor* a_target, RE::Actor* a_aggressor,
		bool a_isPlayerOrFollower, bool a_applyHeadSnap, RE::NiPoint3 a_impactDir)
	{
		if (!a_target) return;
		auto* task = F4SE::GetTaskInterface();
		if (!task) return;

		const std::uint32_t targetID    = a_target->GetFormID();
		const std::uint32_t aggressorID = a_aggressor ? a_aggressor->GetFormID() : 0;

		// For player / followers, check two-shot state before queuing.
		bool twoShotKill = false;
		if (a_isPlayerOrFollower) {
			if (IsWithinTwoShotWindow(targetID)) {
				twoShotKill = true;
				ClearTwoShot(targetID);
			} else {
				RecordFirstShot(targetID);
			}
		}

		const bool isPlayerOrFollower = a_isPlayerOrFollower;
		const bool applyHeadSnap      = a_applyHeadSnap;
		const RE::NiPoint3 impactDir  = a_impactDir;
		task->AddTask([targetID, aggressorID, isPlayerOrFollower, twoShotKill, applyHeadSnap, impactDir]() {
			auto* target = RE::TESForm::GetFormByID<RE::Actor>(targetID);
			if (!target || target->IsDead(true)) return;

			const auto* settings = Settings::GetSingleton();
			auto* tracker = LegendaryTracker::GetSingleton();

			const bool legendary = tracker && settings->legendary.respectLegendaryMutation;
			if (legendary) {
				if (tracker->WasJustMutated(targetID) ||
					tracker->HasMutatedByHealth(target, settings->legendary.mutationHealthRatioThreshold)) {
					tracker->StartCooldown(targetID, settings->legendary.legendaryCooldownSeconds);
					if (settings->debugLogging) {
						logger::info("[HSK]   deferred kill canceled -- legendary just mutated (cooldown started)");
					}
					return;
				}
			}

			auto* spell = HeadshotSpell::GetSingleton();
			if (!spell->IsReady()) {
				logger::warn("[HSK]   kill-spell not ready yet -- skipping damage");
				return;
			}

			auto* aggressor = aggressorID
				? RE::TESForm::GetFormByID<RE::Actor>(aggressorID)
				: nullptr;

			if (isPlayerOrFollower && !twoShotKill) {
				spell->ApplyNearDeathDamage(target, aggressor,
					settings->twoShotNearDeathRatio);
				if (settings->debugLogging) {
					logger::info("[HSK]   two-shot rule: first shot -> near-death (actor=0x{:08X}, window={:.0f}s)",
						targetID, settings->twoShotWindowSeconds);
				}
			} else {
				// Apply the head-snap BEFORE the kill damage. Doing skeleton
				// surgery (inserting the fake joint) on an actor that's already
				// in the death/ragdoll transition causes intermittent visual
				// glitches (head pops to wrong position, detaches, etc.).
				// Modifying a still-living, stable skeleton then killing them
				// makes the engine bring the death animation up *with* our
				// rotation already in the parent chain.
				if (applyHeadSnap) {
					ApplyKillImpulse(target, aggressor, impactDir);
				}
				spell->ApplyKillDamage(target, aggressor);
			}
		});
	}
}

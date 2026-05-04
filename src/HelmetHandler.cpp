#include "HelmetHandler.h"
#include "GameDefinitions.h"
#include "RE/Bethesda/SendHUDMessage.h"
#include "RE/Bethesda/Script.h"

namespace HSK
{
	// =====================================================================
	// ApplyEffectShader resolution (PostNG only -- ID 2205201)
	// =====================================================================
	void HelmetHandler::ResolveEngineFunctions()
	{
		if (!IsNextGen()) {
			logger::info("[HSK] HelmetHandler: ApplyEffectShader skipped (PreNG -- ID not available)");
			return;
		}

		try {
			_applyShaderFn = REL::Relocation<ApplyEffectShaderFn>{ REL::ID(2205201) }.get();
			logger::info("[HSK] HelmetHandler: ApplyEffectShader resolved (PostNG)");
		} catch (...) {
			_applyShaderFn = nullptr;
			logger::warn("[HSK] HelmetHandler: Failed to resolve ApplyEffectShader");
		}
	}

	void HelmetHandler::ApplyHelmetShader(RE::TESObjectREFR* a_ref) const
	{
		if (!a_ref) return;

		const auto* settings = Settings::GetSingleton();
		if (!settings->helmetShaderEnabled) return;

		// PreNG: ApplyEffectShader isn't available; use blink fallback.
		if (!_applyShaderFn) {
			ApplyHelmetBlink(a_ref);
			return;
		}

		auto* shader = RE::TESForm::GetFormByEditorID<RE::TESEffectShader>(settings->helmetShaderEditorID);
		if (!shader) {
			logger::warn("[HSK] HelmetHandler: Could not find TESEffectShader '{}' -- falling back to blink",
				settings->helmetShaderEditorID);
			ApplyHelmetBlink(a_ref);
			return;
		}

		_applyShaderFn(a_ref, shader, settings->helmetShaderDuration, nullptr, false, false, nullptr, false);

		if (settings->debugLogging) {
			logger::info("[HSK] Applied shader '{}' to dropped helmet ref 0x{:08X} (duration: {}s)",
				settings->helmetShaderEditorID, a_ref->GetFormID(), settings->helmetShaderDuration);
		}
	}

	// =====================================================================
	// PreNG visual fallback: toggle the ref's 3D visibility on a timer so
	// the player can spot the dropped helmet.  Lightweight -- the blink
	// thread sleeps for ~400ms between frames and exits when:
	//   - the ref no longer resolves (picked up / cleaned up)
	//   - the configured shader duration elapses
	// Uses F4SE task queue for each flip so SetAppCulled is called on the
	// main thread.
	// =====================================================================
	void HelmetHandler::ApplyHelmetBlink(RE::TESObjectREFR* a_ref) const
	{
		if (!a_ref) return;

		const auto* settings = Settings::GetSingleton();
		const std::uint32_t refID = a_ref->GetFormID();
		// Treat negative duration as "indefinite" -- we cap it at a reasonable
		// max so the blink thread doesn't run forever on ref forms the engine
		// never cleans up.
		const float totalSec = settings->helmetShaderDuration < 0.0f
			? 600.0f
			: settings->helmetShaderDuration;

		if (settings->debugLogging) {
			logger::info("[HSK] PreNG fallback: blink visual on helmet ref 0x{:08X} for {}s",
				refID, totalSec);
		}

		std::thread([refID, totalSec]() {
			constexpr int kBlinkMs = 400;
			const int    kTotalMs = static_cast<int>(totalSec * 1000.0f);
			int elapsed = 0;
			bool culled = false;

			while (elapsed < kTotalMs) {
				std::this_thread::sleep_for(std::chrono::milliseconds(kBlinkMs));
				elapsed += kBlinkMs;

				const bool wantCulled = !culled;
				culled = wantCulled;

				F4SE::GetTaskInterface()->AddTask([refID, wantCulled]() {
					auto* ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>(refID);
					if (!ref || ref->IsDeleted()) return;
					auto* obj3D = ref->Get3D();
					if (!obj3D) return;
					obj3D->SetAppCulled(wantCulled);
				});

				// Check if the ref is gone; if so, break out early.
				// (Done inside the polling loop rather than the task to avoid
				// hopping threads just for a liveness check.)
				auto* ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>(refID);
				if (!ref || ref->IsDeleted()) break;
			}

			// Final pass: make sure the ref is visible when we stop.
			F4SE::GetTaskInterface()->AddTask([refID]() {
				auto* ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>(refID);
				if (!ref) return;
				auto* obj3D = ref->Get3D();
				if (!obj3D) return;
				obj3D->SetAppCulled(false);
			});
		}).detach();
	}

	// =====================================================================
	// Helmet HUD tracker -- shows periodic compass direction + distance.
	// Runs on a detached thread, stops when:
	//   - Helmet is picked up (_playerDroppedRefID becomes 0)
	//   - Ref is destroyed/deleted
	//   - Maximum duration (shader duration) elapses
	// =====================================================================
	void HelmetHandler::StartHelmetTracker()
	{
		bool expected = false;
		if (!_trackerRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
			return;  // already running
		}

		std::thread([this]() {
			const auto* settings = Settings::GetSingleton();
			const float intervalSec = std::max(1.0f, settings->helmetTrackerIntervalSec);
			const float maxDuration = settings->helmetShaderDuration < 0.0f ? 600.0f : settings->helmetShaderDuration;
			const int intervalMs = static_cast<int>(intervalSec * 1000.0f);
			float elapsed = 0.0f;

			while (elapsed < maxDuration) {
				std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
				elapsed += intervalSec;

				const std::uint32_t refID = _playerDroppedRefID.load(std::memory_order_acquire);
				if (refID == 0) break;

				F4SE::GetTaskInterface()->AddTask([this, refID]() {
					auto* ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>(refID);
					if (!ref || ref->IsDeleted()) {
						_playerDroppedRefID.store(0, std::memory_order_release);
						return;
					}

					auto* player = RE::PlayerCharacter::GetSingleton();
					if (!player) return;

					const auto& hp = player->data.location;
					const auto& rp = ref->data.location;
					const float dx = rp.x - hp.x;
					const float dy = rp.y - hp.y;
					const float dz = rp.z - hp.z;
					const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

					// Convert to meters (1 Bethesda unit ≈ 1.4cm → /71.12 = meters)
					const float distMeters = dist / 71.12f;

					// Compass direction based on angle from player facing → helmet
					const float angle = std::atan2(dx, dy);  // radians, 0=N, pi/2=E
					// Player's facing direction (Z rotation in radians)
					const float playerYaw = player->data.angle.z;
					float relAngle = angle - playerYaw;
					// Normalize to [-pi, pi]
					while (relAngle > 3.14159f)  relAngle -= 6.28318f;
					while (relAngle < -3.14159f) relAngle += 6.28318f;

					// Pick a compass arrow based on relative angle
					const char* arrow = nullptr;
					if (relAngle >= -0.3927f && relAngle < 0.3927f)
						arrow = "^";       // ahead
					else if (relAngle >= 0.3927f && relAngle < 1.1781f)
						arrow = ">";       // front-right
					else if (relAngle >= 1.1781f && relAngle < 1.9635f)
						arrow = ">>";      // right
					else if (relAngle >= 1.9635f || relAngle < -1.9635f)
						arrow = "v";       // behind
					else if (relAngle >= -1.9635f && relAngle < -1.1781f)
						arrow = "<<";      // left
					else
						arrow = "<";       // front-left

					// Vertical hint
					const char* vert = "";
					if (dz > 100.0f) vert = " (above)";
					else if (dz < -100.0f) vert = " (below)";

					char msg[128];
					snprintf(msg, sizeof(msg), "[%s] Helmet: %.0fm%s", arrow, distMeters, vert);
					RE::SendHUDMessage::ShowHUDMessage(msg, nullptr, true, true);
				});
			}

			_trackerRunning.store(false, std::memory_order_release);
		}).detach();
	}

	// =====================================================================
	// Point light placement via console command PlaceAtMe.
	// Uses Script::CompileAndRun to execute the command on the helmet ref.
	// =====================================================================
	void HelmetHandler::PlaceHelmetLight(RE::TESObjectREFR* a_helmetRef)
	{
		if (!a_helmetRef) return;

		const auto* settings = Settings::GetSingleton();
		if (!settings->helmetLightEnabled) return;

		// Look up the light form by EditorID
		auto* lightForm = RE::TESForm::GetFormByEditorID(settings->helmetLightEditorID);
		if (!lightForm) {
			logger::warn("[HSK] Helmet light: could not find TESObjectLIGH '{}'",
				settings->helmetLightEditorID);
			return;
		}

		const std::uint32_t lightFormID = lightForm->GetFormID();
		const std::uint32_t helmetRefID = a_helmetRef->GetFormID();

		// Execute PlaceAtMe on the helmet ref using Script::CompileAndRun.
		// The console command "PlaceAtMe <formID> 1" spawns a ref of the given
		// form at the target ref's location.
		const bool debugLog = settings->debugLogging;
		const std::string lightEdid = settings->helmetLightEditorID;
		F4SE::GetTaskInterface()->AddTask([this, helmetRefID, lightFormID, debugLog, lightEdid]() {
			auto* helmetRef = RE::TESForm::GetFormByID<RE::TESObjectREFR>(helmetRefID);
			if (!helmetRef) return;

			char cmd[64];
			snprintf(cmd, sizeof(cmd), "PlaceAtMe %08X 1", lightFormID);

			// Use a static Script instance to avoid TESForm construction complexity.
			// CompileAndRun is a non-virtual REL::Relocation call; it just needs
			// the text field to be set correctly.
			alignas(RE::Script) static std::byte scriptBuf[sizeof(RE::Script)]{};
			static bool scriptInited = false;
			if (!scriptInited) {
				std::memset(scriptBuf, 0, sizeof(scriptBuf));
				// Stamp the real Script vtable so Script::Init virtual dispatch works.
				static const uintptr_t kVtable = REL::Relocation<uintptr_t>{ REL::ID(20936) }.address();
				*reinterpret_cast<uintptr_t*>(scriptBuf) = kVtable;
				scriptInited = true;
			}
			auto* script = reinterpret_cast<RE::Script*>(scriptBuf);
			// Clear previous text before setting new command
			if (script->text) {
				RE::free(script->text);
				script->text = nullptr;
			}
			script->SetText(cmd);

			try {
				script->CompileAndRun(nullptr, RE::COMPILER_NAME::kSystemWindow, helmetRef);
			} catch (...) {
				logger::warn("[HSK] Helmet light: CompileAndRun threw -- light placement failed");
			}

			if (debugLog) {
				logger::info("[HSK] Helmet light: placed '{}' (0x{:08X}) at helmet ref 0x{:08X}",
					lightEdid, lightFormID, helmetRefID);
			}
		});
	}

	void HelmetHandler::CleanupHelmetLight()
	{
		const std::uint32_t lightRefID = _playerHelmetLightRefID.exchange(0, std::memory_order_acq_rel);
		if (lightRefID == 0) return;

		F4SE::GetTaskInterface()->AddTask([lightRefID]() {
			auto* ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>(lightRefID);
			if (!ref || ref->IsDeleted()) return;
			ref->Disable();
			ref->SetDelete(true);
		});
	}

	// =====================================================================
	// Fallout 4 BipedObjectSlot bit map
	// =====================================================================
	// BipedObjectSlots is a 32-bit mask. Bit N corresponds to slot (30 + N).
	// Reference (Creation Kit wiki: Biped Object / Slot Numbers):
	//
	//   bit  0  = slot 30 -- HAIRTOP       (most helmets / hats)
	//   bit  1  = slot 31 -- HAIRLONG      (often paired with HAIRTOP)
	//   bit  2  = slot 32 -- FaceGenHead   (full-face helms, gas masks)
	//   bit  3  = slot 33 -- BODY
	//   bit  4  = slot 34 -- L Hand
	//   bit  5  = slot 35 -- R Hand
	//   bit  6..10 = slots 36..40 -- [U] underwear pieces
	//   bit 11  = slot 41 -- [A] TORSO     (body armor!  NOT a helmet)
	//   bit 12..15 = slots 42..45 -- [A] LArm/RArm/LLeg/RLeg (limb armor)
	//   bit 16  = slot 46 -- HEADBAND      (bandanas; rarely armored)
	//   bit 17  = slot 47 -- EYES          (glasses)
	//   bit 18  = slot 48 -- BEARD
	//   bit 19  = slot 49 -- MOUTH
	//   bit 20  = slot 50 -- NECK
	//   bit 21  = slot 51 -- RING
	//   bit 22  = slot 52 -- SCALP
	//   bit 23  = slot 53 -- DECAPITATE
	//   bit 24..28 = slots 54..58 -- Unnamed
	//   bit 29  = slot 59 -- Shield
	//   bit 30  = slot 60 -- Pipboy        (NOT a PA helmet -- there is NO
	//                                       distinct PA-helmet slot in F4)
	//   bit 31  = slot 61 -- FX
	//
	// IMPORTANT: Power-Armor helmets do NOT live in their own slot. Both
	// regular helmets and PA helmets equip into slots 30/31/32 just like
	// any hat. The only reliable way to differentiate them is:
	//   1. The actor has the kPowerArmor ExtraData attached.
	//   2. The helmet armor record carries the "ArmorTypePower" keyword.
	// =====================================================================
	static constexpr std::uint32_t kSlot30HairTop  = 1u << 0;
	static constexpr std::uint32_t kSlot31HairLong = 1u << 1;
	static constexpr std::uint32_t kSlot32FaceHead = 1u << 2;

	// What we consider "armored head coverage". Headband (slot 46) is
	// excluded -- bandanas don't stop bullets, and we don't want to mark
	// the head as armored just because the actor has a bandana on.
	static constexpr std::uint32_t kHeadCoveringMask =
		kSlot30HairTop | kSlot31HairLong | kSlot32FaceHead;

	static RE::BGSKeyword* GetArmorTypePowerKeyword()
	{
		static RE::BGSKeyword* cached = nullptr;
		static bool tried = false;
		if (!tried) {
			cached = RE::TESForm::GetFormByEditorID<RE::BGSKeyword>("ArmorTypePower");
			tried  = true;
		}
		return cached;
	}

	// Known FO4 BGSDamageType FormIDs (from Fallout4.esm)
	static constexpr std::uint32_t kDtPhysicalFormID = 0x00060A81;
	static constexpr std::uint32_t kDtEnergyFormID   = 0x00060A85;

	// Read per-damage-type resistance from the InstanceData's damageTypes array.
	static void ReadDamageTypeResistances(const RE::TESObjectARMO* a_armo, float& a_ballisticOut, float& a_energyOut)
	{
		a_ballisticOut = 0.0f;
		a_energyOut    = 0.0f;

		if (!a_armo) return;

		const auto* dmgTypes = a_armo->data.damageTypes;
		if (!dmgTypes) return;

		for (std::uint32_t i = 0; i < dmgTypes->size(); ++i) {
			const auto& tuple = (*dmgTypes)[i];
			if (!tuple.first) continue;
			const auto fid = tuple.first->GetFormID();
			if (fid == kDtPhysicalFormID) {
				a_ballisticOut = static_cast<float>(tuple.second.i);
			} else if (fid == kDtEnergyFormID) {
				a_energyOut = static_cast<float>(tuple.second.i);
			}
		}
	}

	// =====================================================================
	// HelmetInfo extraction
	//
	// Strategy:
	//   1. Walk every equipped TESObjectARMO on the actor.
	//   2. Among pieces occupying any head slot (30/31/32), pick the one
	//      with the highest armor rating -- that's "the helmet".
	//   3. Decide PA-helmet vs regular helmet by checking, in order:
	//        a) the picked helmet's keywords for "ArmorTypePower"
	//        b) the actor's kPowerArmor ExtraData
	// =====================================================================
	HelmetInfo HelmetHandler::InspectHead(RE::Actor* a_actor) const
	{
		HelmetInfo info{};
		if (!a_actor) return info;

		auto* invList = a_actor->inventoryList;
		if (!invList) return info;

		RE::TESObjectARMO* helmetPick = nullptr;
		float              bestCombinedAR = -1.0f;

		for (auto& item : invList->data) {
			auto* obj = item.object;
			if (!obj || obj->formType != RE::ENUM_FORM_ID::kARMO) continue;

			auto* armo = static_cast<RE::TESObjectARMO*>(obj);

			bool isEquipped = false;
			for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
				if (stack->IsEquipped()) {
					isEquipped = true;
					break;
				}
			}
			if (!isEquipped) continue;

			const std::uint32_t slots = armo->RE::BGSBipedObjectForm::GetFilledSlots();
			if (!(slots & kHeadCoveringMask)) continue;

			float bal = 0.0f, ene = 0.0f;
			ReadDamageTypeResistances(armo, bal, ene);
			const float combinedAR = bal + ene;
			if (!helmetPick || combinedAR > bestCombinedAR) {
				helmetPick    = armo;
				bestCombinedAR = combinedAR;
			}
		}

		const bool extraSaysPA =
			a_actor->extraList && a_actor->extraList->HasType(RE::EXTRA_DATA_TYPE::kPowerArmor);

		if (helmetPick) {
			bool helmetIsPA = false;
			if (auto* paKw = GetArmorTypePowerKeyword()) {
				helmetIsPA = helmetPick->HasKeyword(paKw, nullptr);
			}
			if (!helmetIsPA && extraSaysPA) {
				helmetIsPA = true;
			}

			float bal = 0.0f, ene = 0.0f;
			ReadDamageTypeResistances(helmetPick, bal, ene);

			info.hasHeadArmor = true;
			info.isPowerArmor = helmetIsPA;
			info.ballisticAR  = bal;
			info.energyAR     = ene;
			info.headArmor    = helmetPick;
			return info;
		}

		// No head piece equipped, but actor is in a PA frame -- treat as
		// armored bucket for the chance calc, with no removable piece.
		if (extraSaysPA) {
			info.hasHeadArmor = true;
			info.isPowerArmor = true;
			info.ballisticAR  = 0.0f;
			info.energyAR     = 0.0f;
			info.headArmor    = nullptr;
			return info;
		}

		return info;
	}

	// =====================================================================
	// Chance roll
	// =====================================================================
	HelmetHandler::KnockoffResult HelmetHandler::ShouldKnockOff(Caliber a_caliber, bool a_isPowerArmor) const
	{
		const auto* settings = Settings::GetSingleton();
		if (!settings->helmet.enableHelmetKnockoff)
			return { false, 0.0f };

		float chance = 0.0f;
		if (a_isPowerArmor) {
			switch (a_caliber) {
			case Caliber::Rifle:      chance = settings->helmet.paKnockoffChanceRifle;      break;
			case Caliber::LargeRifle: chance = settings->helmet.paKnockoffChanceLargeRifle; break;
			default: return { false, 0.0f };
			}
		} else {
			switch (a_caliber) {
			case Caliber::Pistol:     chance = settings->helmet.knockoffChancePistol;  break;
			case Caliber::Shotgun:    chance = settings->helmet.knockoffChanceShotgun; break;
			case Caliber::Rifle:      chance = settings->helmet.knockoffChanceRifle;   break;
			case Caliber::LargeRifle: chance = settings->helmet.knockoffChanceRifle;   break;
			default: return { false, 0.0f };
			}
		}

		if (chance <= 0.0f)   return { false, chance };
		if (chance >= 100.0f) return { true, chance };

		thread_local std::mt19937 gen{ std::random_device{}() };
		std::uniform_real_distribution<float> dist(0.0f, 100.0f);
		return { dist(gen) < chance, chance };
	}

	// =====================================================================
	// Knockoff implementation (deferred to game thread)
	// =====================================================================
	void HelmetHandler::KnockOff(RE::Actor* a_actor, RE::TESObjectARMO* a_helmet,
		bool a_isPlayer, bool a_isFollower, RE::NiPoint3 a_flyDir)
	{
		if (!a_actor || !a_helmet) return;
		auto* task = F4SE::GetTaskInterface();
		if (!task) return;

		const std::uint32_t actorID  = a_actor->GetFormID();
		const std::uint32_t helmetID = a_helmet->GetFormID();

		// Track helmet for player auto-reequip
		if (a_isPlayer) {
			_playerKnockedHelmetID.store(helmetID, std::memory_order_release);
		}

		const auto* settings = Settings::GetSingleton();
		const bool isPlayer         = a_isPlayer;
		const bool notifyPlayer     = a_isPlayer && settings->helmetNotifyPlayer;
		const bool applyShader      = a_isPlayer && settings->helmetShaderEnabled;
		const bool startTracker     = a_isPlayer && settings->helmetTrackerEnabled;
		const bool recordForFollower = a_isFollower && settings->followerHelmetRestore;
		const float linearImpulse   = settings->helmet.dropLinearImpulse;
		const float spawnHeight     = settings->helmet.dropSpawnHeight;
		const RE::NiPoint3 flyDir   = a_flyDir;

		task->AddTask([this, actorID, helmetID, isPlayer, notifyPlayer, applyShader, startTracker, recordForFollower, linearImpulse, spawnHeight, flyDir]() {
			auto* actor  = RE::TESForm::GetFormByID<RE::Actor>(actorID);
			auto* helmet = RE::TESForm::GetFormByID<RE::TESObjectARMO>(helmetID);
			if (!actor || !helmet) return;

			auto* equipMgr = RE::ActorEquipManager::GetSingleton();
			if (!equipMgr) return;

			RE::BGSObjectInstance objInst{};
			objInst.object = helmet;

			equipMgr->UnequipObject(
				actor,                       // actor
				&objInst,                    // BGSObjectInstance*
				1u,                          // number
				nullptr,                     // BGSEquipSlot* (let engine pick)
				0u,                          // stackID
				false,                       // queueEquip
				true,                        // forceEquip
				false,                       // playSounds
				true,                        // applyNow
				nullptr);                    // slotBeingReplaced

			// Find the head node to use as the spawn origin.
			RE::NiPoint3 spawnPos{ actor->data.location.x, actor->data.location.y, actor->data.location.z + 120.0f };
			if (auto* obj3D = actor->Get3D()) {
				static const RE::BSFixedString headName{ "Head" };
				if (auto* headNode = obj3D->GetObjectByName(headName); headNode) {
					spawnPos = headNode->world.translate;
				}
			}

			// Normalise fly direction.
			RE::NiPoint3 flyUnit{ 0.0f, 0.0f, 1.0f };
			const float flyLen = std::sqrt(flyDir.x * flyDir.x + flyDir.y * flyDir.y + flyDir.z * flyDir.z);
			if (flyLen > 0.01f) {
				const float invLen = 1.0f / flyLen;
				flyUnit = { flyDir.x * invLen, flyDir.y * invLen, flyDir.z * invLen };
			}

			// Spawn the helmet slightly above the head so it clears the skull
			// geometry before the Havok impulse is applied one frame later.
			// Controlled by fDropSpawnHeight in the INI / UI slider.
			spawnPos.z += spawnHeight;

			// Random initial rotation so helmets don't land identically oriented.
			// DropObject takes Euler angles in radians.
			static thread_local std::mt19937 rng{ std::random_device{}() };
			std::uniform_real_distribution<float> rotDist{ -1.5708f, 1.5708f };
			RE::NiPoint3 spawnRot{ rotDist(rng), rotDist(rng), rotDist(rng) };

			if (Settings::GetSingleton()->debugLogging) {
				logger::info("[HSK]   helmet drop: actor=0x{:08X} flyUnit=({:.2f},{:.2f},{:.2f}) impulse={:.1f} "
					"spawnPos=({:.0f},{:.0f},{:.0f})",
					actorID, flyUnit.x, flyUnit.y, flyUnit.z, linearImpulse,
					spawnPos.x, spawnPos.y, spawnPos.z);
			}

			RE::BGSObjectInstance dropInst{};
			dropInst.object = helmet;
			auto refHandle = actor->DropObject(dropInst, nullptr, 1, &spawnPos, &spawnRot);

			RE::TESObjectREFR* droppedRef = nullptr;
			if (refHandle) {
				if (auto refPtr = refHandle.get(); refPtr) {
					droppedRef = refPtr.get();
				}
			}

			// Apply Havok impulse on the next game-thread frame so the engine
			// has one tick to initialise the dropped ref's physics body.
			// This gives the helmet a real initial velocity - it flies in
			// flyUnit direction with magnitude linearImpulse - rather than
			// the old broken behaviour of spawning at an offset and falling.
			if (droppedRef && linearImpulse > 0.0f) {
				const std::uint32_t dropRefID = droppedRef->GetFormID();
				const float ix = flyUnit.x, iy = flyUnit.y, iz = flyUnit.z;
				const float imag = linearImpulse;
				F4SE::GetTaskInterface()->AddTask([dropRefID, ix, iy, iz, imag]() {
					auto* ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>(dropRefID);
					if (!ref) return;

					char cmd[128];
					snprintf(cmd, sizeof(cmd), "applyHavokImpulse %.4f %.4f %.4f %.4f",
						ix, iy, iz, imag);

			alignas(RE::Script) static std::byte sBuf[sizeof(RE::Script)]{};
				static bool sInited = false;
				if (!sInited) {
					std::memset(sBuf, 0, sizeof(sBuf));
					// Script derives from TESForm (vtable at offset 0). The zero-init
					// leaves vtable=null, causing Script::Init to crash on virtual dispatch.
					// Stamp the real Script vtable (REL::ID 20936) before first use.
					static const uintptr_t kVtable = REL::Relocation<uintptr_t>{ REL::ID(20936) }.address();
					*reinterpret_cast<uintptr_t*>(sBuf) = kVtable;
					sInited = true;
				}
				auto* script = reinterpret_cast<RE::Script*>(sBuf);
					if (script->text) { RE::free(script->text); script->text = nullptr; }
					script->SetText(cmd);
					try {
						script->CompileAndRun(nullptr, RE::COMPILER_NAME::kSystemWindow, ref);
					} catch (...) {
						logger::warn("[HSK] applyHavokImpulse threw -- impulse skipped for ref 0x{:08X}", dropRefID);
					}
				});
			}

			if (applyShader && droppedRef) {
				ApplyHelmetShader(droppedRef);
			}

			// Record follower knockoff for post-combat restore.
			if (recordForFollower && droppedRef) {
				std::lock_guard lk(_followerMutex);
				RecordFollowerKnockoff(actorID, helmetID, droppedRef->GetFormID());
			}

			// Start HUD tracker for the player's dropped helmet.
			if (startTracker && droppedRef) {
				_playerDroppedRefID.store(droppedRef->GetFormID(), std::memory_order_release);
				StartHelmetTracker();
			}

			// Place a point light at the helmet for visibility in dark environments.
			if (isPlayer && droppedRef) {
				PlaceHelmetLight(droppedRef);
			}

			if (notifyPlayer) {
				const char* helmetName = helmet->GetFullName();
				if (helmetName && helmetName[0]) {
					char msg[256];
					snprintf(msg, sizeof(msg), "%s knocked off!", helmetName);
					RE::SendHUDMessage::ShowHUDMessage(msg, nullptr, true, true);
				} else {
					RE::SendHUDMessage::ShowHUDMessage("Helmet knocked off!", nullptr, true, true);
				}
			}
		});
	}

	// Caller must hold _followerMutex.
	void HelmetHandler::RecordFollowerKnockoff(std::uint32_t a_actorID, std::uint32_t a_helmetID, std::uint32_t a_droppedRefID)
	{
		// Replace any existing entry for this follower -- they can only have one
		// knocked-off helmet at a time (no multi-helmet stacking).
		for (auto& entry : _followerKnockoffs) {
			if (entry.actorFormID == a_actorID) {
				entry.helmetBaseID = a_helmetID;
				entry.droppedRefID = a_droppedRefID;
				StartCombatEndWatcher();
				return;
			}
		}
		_followerKnockoffs.push_back({ a_actorID, a_helmetID, a_droppedRefID });
		StartCombatEndWatcher();
	}

	// =====================================================================
	// Combat-end watcher.
	//
	// Spawns a detached thread that polls the player's IsInCombat() state
	// once per ~750ms. Fires RestoreFollowerHelmets() on the
	// in-combat -> out-of-combat transition. Exits when either:
	//   - transition detected + restore fired, OR
	//   - pending queue becomes empty (e.g. cleared by PostLoadGame path).
	//
	// Only one watcher runs at a time; concurrent calls are no-ops.
	// =====================================================================
	void HelmetHandler::StartCombatEndWatcher()
	{
		bool expected = false;
		if (!_watcherRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
			return;  // already running
		}

		std::thread([this]() {
			bool wasInCombat = false;
			int  idleTicks   = 0;
			constexpr int kPollMs = 750;
			constexpr int kMaxIdleTicks = 40;  // ~30s max wait for combat start, then give up

			while (true) {
				std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));

				// Early exit: queue drained (e.g. PostLoadGame restored them)
				{
					std::lock_guard lk(_followerMutex);
					if (_followerKnockoffs.empty()) break;
				}

				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player) {
					if (++idleTicks > kMaxIdleTicks) break;
					continue;
				}

				const bool inCombat = player->IsInCombat();

				if (inCombat) {
					wasInCombat = true;
					idleTicks   = 0;
				} else if (wasInCombat) {
					// Transition: in-combat -> out-of-combat.  Restore and exit.
					RestoreFollowerHelmets();
					break;
				} else {
					// Never entered combat; cap wait time to avoid lingering forever.
					if (++idleTicks > kMaxIdleTicks) {
						RestoreFollowerHelmets();
						break;
					}
				}
			}

			_watcherRunning.store(false, std::memory_order_release);
		}).detach();
	}

	// =====================================================================
	// Follower helmet restore -- called on player combat-end and PostLoadGame.
	//
	// For each tracked entry, resolve the dropped world ref and use
	// Actor::PickUpObject to move THAT exact ref (with its ExtraDataList --
	// legendary mods, condition, attached mods, etc.) back into the follower's
	// inventory, then re-equip it.  If the ref is gone (cell unloaded or
	// otherwise cleaned up), we skip and log -- we DO NOT create a duplicate.
	// =====================================================================
	void HelmetHandler::RestoreFollowerHelmets()
	{
		// Snapshot + clear under lock so new knockoffs during the restore
		// don't get dropped or double-processed.
		std::vector<FollowerKnockoffEntry> pending;
		{
			std::lock_guard lk(_followerMutex);
			if (_followerKnockoffs.empty()) return;
			pending.swap(_followerKnockoffs);
		}

		auto* task = F4SE::GetTaskInterface();
		if (!task) {
			// Best effort: put them back and let the next trigger retry.
			std::lock_guard lk(_followerMutex);
			_followerKnockoffs = std::move(pending);
			return;
		}

		const bool verbose = Settings::GetSingleton()->debugLogging;

		task->AddTask([pending = std::move(pending), verbose]() {
			auto* equipMgr = RE::ActorEquipManager::GetSingleton();
			if (!equipMgr) {
				if (verbose) logger::warn("[HSK] RestoreFollowerHelmets: ActorEquipManager unavailable");
				return;
			}

			for (const auto& entry : pending) {
				auto* actor = RE::TESForm::GetFormByID<RE::Actor>(entry.actorFormID);
				if (!actor) {
					if (verbose) {
						logger::info("[HSK] follower-restore: actor 0x{:08X} not found (unloaded?); skipping",
							entry.actorFormID);
					}
					continue;
				}
				if (actor->IsDead(true)) {
					if (verbose) {
						logger::info("[HSK] follower-restore: actor 0x{:08X} is dead; skipping",
							entry.actorFormID);
					}
					continue;
				}

				auto* droppedRef = RE::TESForm::GetFormByID<RE::TESObjectREFR>(entry.droppedRefID);
				if (!droppedRef || droppedRef->IsDeleted()) {
					logger::warn("[HSK] follower-restore: dropped helmet ref 0x{:08X} not found or cleaned up; "
						"follower 0x{:08X} will remain bare-headed (no duplicate created)",
						entry.droppedRefID, entry.actorFormID);
					continue;
				}

				// Move the exact ref back into inventory. This preserves the
				// ExtraDataList (legendary, mods, condition).
				actor->PickUpObject(droppedRef, 1, false);

				// Now equip it.  The base form is what EquipObject needs to
				// locate the stack inside the inventory.
				auto* helmet = RE::TESForm::GetFormByID<RE::TESObjectARMO>(entry.helmetBaseID);
				if (helmet) {
					RE::BGSObjectInstance objInst{};
					objInst.object = helmet;
					equipMgr->EquipObject(
						actor,
						objInst,
						0u,      // stackID
						1u,      // number
						nullptr, // BGSEquipSlot*
						false,   // queueEquip
						true,    // forceEquip
						false,   // playSounds
						true,    // applyNow
						false);  // locked
				}

				if (verbose) {
					logger::info("[HSK] follower-restore: actor 0x{:08X} <- helmet ref 0x{:08X} (base 0x{:08X})",
						entry.actorFormID, entry.droppedRefID, entry.helmetBaseID);
				}
			}
		});
	}

	// =====================================================================
	// Auto-reequip on pickup
	// =====================================================================
	void HelmetHandler::OnPlayerItemAdded(std::uint32_t a_baseFormID)
	{
		const std::uint32_t tracked = _playerKnockedHelmetID.load(std::memory_order_acquire);
		if (tracked == 0 || tracked != a_baseFormID) return;

		if (!Settings::GetSingleton()->helmetAutoReequip) return;

		// Clear tracking immediately to stop tracker + prevent double-equip
		_playerKnockedHelmetID.store(0, std::memory_order_release);
		_playerDroppedRefID.store(0, std::memory_order_release);
		CleanupHelmetLight();

		auto* task = F4SE::GetTaskInterface();
		if (!task) return;

		task->AddTask([a_baseFormID]() {
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* helmet = RE::TESForm::GetFormByID<RE::TESObjectARMO>(a_baseFormID);
			if (!player || !helmet) return;

			auto* equipMgr = RE::ActorEquipManager::GetSingleton();
			if (!equipMgr) return;

			RE::BGSObjectInstance objInst{};
			objInst.object = helmet;

			equipMgr->EquipObject(
				player,
				objInst,
				0u,      // stackID
				1u,      // number
				nullptr, // BGSEquipSlot*
				false,   // queueEquip
				true,    // forceEquip
				false,   // playSounds
				true,    // applyNow
				false);  // locked

			if (Settings::GetSingleton()->debugLogging) {
				logger::info("[HSK] Auto-reequipped knocked-off helmet: 0x{:08X}", a_baseFormID);
			}
		});
	}

	// =====================================================================
	// Bare-head + recent-hit tracking
	// =====================================================================
	void HelmetHandler::MarkBareHead(std::uint32_t a_actorFormID)
	{
		std::unique_lock lk{ _mutex };
		_bareHeads.insert(a_actorFormID);
	}

	bool HelmetHandler::IsBareHead(std::uint32_t a_actorFormID) const
	{
		std::shared_lock lk{ _mutex };
		return _bareHeads.contains(a_actorFormID);
	}

	void HelmetHandler::ClearBareHead(std::uint32_t a_actorFormID)
	{
		std::unique_lock lk{ _mutex };
		_bareHeads.erase(a_actorFormID);
	}

	void HelmetHandler::NoteHit(std::uint32_t a_actorFormID)
	{
		std::unique_lock lk{ _mutex };
		_lastHitTime[a_actorFormID] = Now();
	}

	bool HelmetHandler::RecentHit(std::uint32_t a_actorFormID, float a_windowSec) const
	{
		std::shared_lock lk{ _mutex };
		auto it = _lastHitTime.find(a_actorFormID);
		if (it == _lastHitTime.end()) return false;
		return (Now() - it->second) <= a_windowSec;
	}

	float HelmetHandler::Now()
	{
		using clock = std::chrono::steady_clock;
		const auto t = clock::now().time_since_epoch();
		return std::chrono::duration<float>(t).count();
	}
}

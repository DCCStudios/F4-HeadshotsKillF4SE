#include "HeadshotSpell.h"

namespace
{
	// ── MagicSystem / EffectArchetypes enum constants ────────────────────
	// These enums are only forward-declared in our CommonLibF4 tree.
	// We use static_cast from the known integer values (verified against
	// the PreNG / SSE CommonLib headers which share the same engine).

	constexpr auto kCastingFireAndForget =
		static_cast<RE::MagicSystem::CastingType>(1);
	constexpr auto kDeliveryTargetActor =
		static_cast<RE::MagicSystem::Delivery>(3);
	constexpr auto kSpellTypeSpell =
		static_cast<RE::MagicSystem::SpellType>(0);
	constexpr auto kArchetypeValueModifier =
		static_cast<RE::EffectArchetypes::ArchetypeID>(0);

	// ── EffectSetting flag bits (from UESP / CK, shared across BGS engines) ─
	constexpr std::uint32_t kMGEF_Hostile     = 1u << 0;   // 0x00000001
	constexpr std::uint32_t kMGEF_Detrimental = 1u << 2;   // 0x00000004
	constexpr std::uint32_t kMGEF_Recover     = 1u << 1;   // 0x00000002 (absent = no recovery)
	constexpr std::uint32_t kMGEF_NoHitEvent  = 1u << 4;   // 0x00000010
	constexpr std::uint32_t kMGEF_NoResist    = 1u << 20;  // 0x00100000

	// ── MagicItem::Data flag bits ───────────────────────────────────────
	constexpr std::uint32_t kSPEL_IgnoreResistance = 1u << 20;  // 0x00100000
	constexpr std::uint32_t kSPEL_NoAbsorb         = 1u << 21;  // 0x00200000

	// Magnitude applied by the instakill spell.  Effectively "99999 health
	// damage" — far more than any actor in the game can survive.
	constexpr float kKillMagnitude = 99999.0f;

	// ── SpellItem::Cast wrapper ─────────────────────────────────────────
	// The main CommonLibF4 doesn't ship SpellItem::Cast.  The PreNG copy
	// has it at REL::ID(1511987) — the address library resolves this to
	// the correct address for both OG (1.10.163) and Next-Gen builds.
	bool CastSpell(RE::SpellItem* a_spell,
		RE::TESObjectREFR* a_caster,
		RE::TESObjectREFR* a_target,
		RE::Actor*         a_castingActor)
	{
		using func_t = bool (*)(RE::SpellItem*, RE::TESObjectREFR*,
			RE::TESObjectREFR*, RE::Actor*,
			RE::BSScript::IVirtualMachine*);
		static REL::Relocation<func_t> func{ REL::ID(1511987) };
		return func(a_spell, a_caster, a_target, a_castingActor, nullptr);
	}
}

namespace HSK
{
	void HeadshotSpell::Init()
	{
		if (_ready.load(std::memory_order_acquire)) return;

		logger::info("[HSK] HeadshotSpell::Init -- creating runtime kill spell");

		// ── 1. Create EffectSetting (MGEF) ──────────────────────────────
		auto* mgefFactory = RE::ConcreteFormFactory<RE::EffectSetting>::GetFormFactory();
		if (!mgefFactory) {
			logger::error("[HSK] HeadshotSpell: ConcreteFormFactory<EffectSetting> is null");
			return;
		}

		_effect = mgefFactory->Create();
		if (!_effect) {
			logger::error("[HSK] HeadshotSpell: EffectSetting factory->Create() returned null");
			return;
		}

		auto& ed = _effect->data;
		ed.flags      = kMGEF_Hostile | kMGEF_Detrimental | kMGEF_NoHitEvent;
		ed.baseCost   = 0.0f;
		ed.archetype  = kArchetypeValueModifier;
		ed.castingType = kCastingFireAndForget;
		ed.delivery    = kDeliveryTargetActor;

		// Target the Health actor value.
		auto* avSingleton = RE::ActorValue::GetSingleton();
		if (!avSingleton || !avSingleton->health) {
			logger::error("[HSK] HeadshotSpell: ActorValue singleton or health AV is null");
			return;
		}
		ed.primaryAV      = avSingleton->health;
		ed.secondaryAV    = nullptr;
		ed.resistVariable = nullptr;  // no resistance variable = bypasses all resist

		// Zero out visual / sound / misc fields that the factory may leave dirty.
		ed.associatedForm   = nullptr;
		ed.associatedSkill  = nullptr;
		ed.light            = nullptr;
		ed.effectShader     = nullptr;
		ed.enchantEffect    = nullptr;
		ed.projectileBase   = nullptr;
		ed.explosion        = nullptr;
		ed.castingArt       = nullptr;
		ed.hitEffectArt     = nullptr;
		ed.impactDataSet    = nullptr;
		ed.dualCastData     = nullptr;
		ed.enchantEffectArt = nullptr;
		ed.hitVisuals       = nullptr;
		ed.enchantVisuals   = nullptr;
		ed.equipAbility     = nullptr;
		ed.imageSpaceMod    = nullptr;
		ed.perk             = nullptr;

		ed.taperWeight      = 1.0f;
		ed.taperCurve       = 1.0f;
		ed.taperDuration    = 0.0f;
		ed.secondaryAVWeight = 0.0f;
		ed.skillUsageMult   = 0.0f;
		ed.dualCastScale    = 1.0f;
		ed.minimumSkill     = 0;
		ed.spellmakingArea  = 0;
		ed.spellmakingChargeTime = 0.0f;
		ed.numCounterEffects = 0;

		logger::info("[HSK]   EffectSetting created (formID=0x{:08X})", _effect->GetFormID());

		// ── 2. Create SpellItem (SPEL) ──────────────────────────────────
		auto* spelFactory = RE::ConcreteFormFactory<RE::SpellItem>::GetFormFactory();
		if (!spelFactory) {
			logger::error("[HSK] HeadshotSpell: ConcreteFormFactory<SpellItem> is null");
			return;
		}

		_spell = spelFactory->Create();
		if (!_spell) {
			logger::error("[HSK] HeadshotSpell: SpellItem factory->Create() returned null");
			return;
		}

		auto& sd            = _spell->data;
		sd.costOverride     = 0;
		sd.flags            = kSPEL_IgnoreResistance | kSPEL_NoAbsorb;
		sd.spellType        = kSpellTypeSpell;
		sd.chargeTime       = 0.0f;
		sd.castingType      = kCastingFireAndForget;
		sd.delivery         = kDeliveryTargetActor;
		sd.castDuration     = 0.0f;
		sd.range            = 0.0f;
		sd.castingPerk      = nullptr;

		// ── 3. Create EffectItem and attach to the spell ────────────────
		auto* ei = new RE::EffectItem();
		ei->data.magnitude    = kKillMagnitude;
		ei->data.area         = 0;
		ei->data.duration     = 0;   // instant
		ei->effectSetting     = _effect;
		ei->rawCost           = 0.0f;

		_spell->listOfEffects.push_back(ei);
		_spell->hostileCount = 1;

		logger::info("[HSK]   SpellItem created (formID=0x{:08X}), magnitude={:.0f}",
			_spell->GetFormID(), kKillMagnitude);
		logger::info("[HSK] HeadshotSpell ready.");

		_ready.store(true, std::memory_order_release);
	}

	void HeadshotSpell::ApplyNearDeathDamage(RE::Actor* a_target,
		RE::Actor* a_aggressor, float a_remainRatio)
	{
		if (!_ready.load(std::memory_order_acquire) || !_spell || !a_target) return;

		auto* avSingleton = RE::ActorValue::GetSingleton();
		if (!avSingleton || !avSingleton->health) return;

		const float curHP = a_target->GetActorValue(*avSingleton->health);
		const float maxHP = a_target->GetPermanentActorValue(*avSingleton->health);
		if (maxHP <= 0.0f || curHP <= 0.0f) return;

		const float targetHP = maxHP * std::clamp(a_remainRatio, 0.01f, 0.50f);
		float dmg = curHP - targetHP;
		if (dmg <= 0.0f) return;  // already at or below near-death

		if (!_spell->listOfEffects.empty() && _spell->listOfEffects[0]) {
			_spell->listOfEffects[0]->data.magnitude = dmg;
		}

		RE::TESObjectREFR* caster = a_aggressor
			? static_cast<RE::TESObjectREFR*>(a_aggressor)
			: static_cast<RE::TESObjectREFR*>(RE::PlayerCharacter::GetSingleton());

		if (!caster) return;

		const bool ok = CastSpell(_spell, caster, a_target, a_aggressor);

		if (Settings::GetSingleton()->debugLogging) {
			logger::info("[HSK]   near-death spell on 0x{:08X}: curHP={:.0f} maxHP={:.0f} dmg={:.0f} remain={:.0f} result={}",
				a_target->GetFormID(), curHP, maxHP, dmg, targetHP, ok);
		}
	}

	void HeadshotSpell::ApplyKillDamage(RE::Actor* a_target, RE::Actor* a_aggressor)
	{
		if (!_ready.load(std::memory_order_acquire) || !_spell || !a_target) return;

		// Pull the current magnitude from the user's Settings so the
		// "Kill damage" slider in the menu is respected in real time.
		if (!_spell->listOfEffects.empty() && _spell->listOfEffects[0]) {
			_spell->listOfEffects[0]->data.magnitude =
				Settings::GetSingleton()->killDamage;
		}

		RE::TESObjectREFR* caster = a_aggressor
			? static_cast<RE::TESObjectREFR*>(a_aggressor)
			: static_cast<RE::TESObjectREFR*>(RE::PlayerCharacter::GetSingleton());

		if (!caster) {
			logger::warn("[HSK] HeadshotSpell::ApplyKillDamage -- no caster available");
			return;
		}

		const bool ok = CastSpell(_spell, caster, a_target, a_aggressor);

		if (Settings::GetSingleton()->debugLogging) {
			logger::info("[HSK]   kill-spell cast on 0x{:08X} (caster=0x{:08X}) result={}",
				a_target->GetFormID(),
				caster->GetFormID(),
				ok);
		}
	}
}

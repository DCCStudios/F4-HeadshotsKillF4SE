#pragma once

#include "PCH.h"

// =====================================================================
// GameDefinitions.h
//
// Forward-declares HitData / TESHitEvent for both OG (1.10.163) and NG
// (1.10.984+) Fallout 4 in a way that lets us register a single event
// sink and read fields safely on either version.
//
// Why this is needed:
//   The canonical CommonLibF4 (PluginTemplate/CommonLibF4) does not
//   define HitData / TESHitEvent. The PreNG-style HitData is 0xD8 bytes
//   while the PostNG layout is 0xE0 bytes. Their TESHitEvent wrappers
//   are 0x108 vs 0x110. By using the larger (NG) size and offset-based
//   accessors, we read the fields we need at the correct location for
//   both versions.
//
// Field offsets that match in BOTH OG and NG (verified against
//   PluginTemplate/Native-Animation-Framework PreNG defs and
//   PluginTemplate/fo4test PostNG defs):
//     - source/weapon (BGSObjectInstance first 8 bytes) @ 0x58
//     - ammo                                            @ 0x80
//     - flags                                           @ 0xC4
//     - target/victim (TESObjectREFR* in TESHitEvent)   @ 0xE0 (after HitData padding)
//
// Field offsets that DIFFER:
//     - bodypartType (OG int32)        @ 0xD0
//     - damageLimb   (NG EnumSet u32)  @ 0xD4
//
// We register sinks via the `BSTEventSink<TESHitEvent>` C++ template,
// matching mangled name with the engine since the type is in RE namespace.
// =====================================================================

namespace RE
{
	// =================================================================
	// TESContainerChangedEvent (not defined in this CommonLibF4 variant)
	// REL::ID(242538) for GetEventSource -- same on PreNG and PostNG.
	// =================================================================
	struct TESContainerChangedEvent_Compat
	{
		std::uint32_t oldContainerFormID;  // 0x00 -- source (0 = world)
		std::uint32_t newContainerFormID;  // 0x04 -- destination (0x14 = player)
		std::uint32_t baseObjFormID;       // 0x08 -- base form of the item
		std::int32_t  itemCount;           // 0x0C
		std::uint32_t referenceFormID;     // 0x10
		std::uint16_t uniqueID;            // 0x14
		std::uint16_t pad16;               // 0x16
	};
	static_assert(sizeof(TESContainerChangedEvent_Compat) == 0x18);

	// Forward declarations only -- BGSTypedFormValuePair lives in a namespace
	// in CommonLibF4 (FormComponents.h) so we don't redeclare it here.
	class BGSAttackData;
	class BGSEquipIndex;
	class BGSKeyword;
	class MagicItem;
	class SpellItem;
	class TESAmmo;
	class TESForm;
	class TESObjectREFR;
	class TESObjectWEAP;
	class VATSCommand;
	class bhkNPCollisionObject;

	// =================================================================
	// DamageImpactData -- both versions are 0x40-byte aligned at the
	// next field boundary (OG has 8 bytes of trailing padding before
	// the handles start at 0x40).
	// =================================================================
	struct DamageImpactData_Compat
	{
		float    hitPosX;             // 0x00
		float    hitPosY;             // 0x04
		float    hitPosZ;             // 0x08
		float    hitPosW;             // 0x0C
		float    hitDirX;             // 0x10
		float    hitDirY;             // 0x14
		float    hitDirZ;             // 0x18
		float    hitDirW;             // 0x1C
		float    projectileDirX;      // 0x20
		float    projectileDirY;      // 0x24
		float    projectileDirZ;      // 0x28
		float    projectileDirW;      // 0x2C
		void*    collisionObj;        // 0x30
		std::uint64_t pad38;          // 0x38 (NG impact data is 0x40, OG handles start at 0x40 after gap)
	};
	static_assert(sizeof(DamageImpactData_Compat) == 0x40);

	// =================================================================
	// HitData -- defined at the larger (NG) size 0xE0 so we can safely
	// access either layout. Use the field-accessor functions below
	// rather than direct member access for fields that differ between
	// OG and NG.
	// =================================================================
	class HitData
	{
	public:
		DamageImpactData_Compat impactData;          // 0x00 (size 0x40)
		std::uint32_t           aggressorHandle;     // 0x40
		std::uint32_t           targetHandle;        // 0x44
		std::uint32_t           sourceRefHandle;     // 0x48
		std::uint32_t           pad4C;               // 0x4C
		void*                   attackData;          // 0x50
		TESForm*                weaponForm;          // 0x58 (first 8 bytes of BGSObjectInstance)
		void*                   weaponExtra;         // 0x60
		MagicItem*              effect;              // 0x68
		SpellItem*              spellItem;           // 0x70
		void*                   vatsCommand;         // 0x78
		const TESAmmo*          ammo;                // 0x80
		void*                   damageTypes;         // 0x88
		float                   damageA;             // 0x90 (OG calculatedBaseDamage / NG healthDamage)
		float                   damageB;             // 0x94 (OG baseDamage / NG totalDamage)
		float                   damageC;             // 0x98 (OG totalDamage / NG physicalDamage)
		float                   damageD;             // 0x9C (OG blockedDamage / NG targetedLimbDamage)
		float                   damageE;             // 0xA0
		float                   damageF;             // 0xA4
		float                   damageG;             // 0xA8
		float                   damageH;             // 0xAC
		float                   damageI;             // 0xB0
		float                   damageJ;             // 0xB4
		float                   damageK;             // 0xB8
		float                   damageL;             // 0xBC
		float                   criticalDamageMult;  // 0xC0
		std::uint32_t           flags;               // 0xC4 (kMeleeAttack=0x4, kExplosion=0x80, etc.)
		std::uint32_t           equipIndex;          // 0xC8
		std::uint32_t           padCC;               // 0xCC
		std::uint32_t           materialOrLimb_NG;   // 0xD0 (NG: material, OG: bodypartType)
		std::uint32_t           limb_NG_or_pad;      // 0xD4 (NG: damageLimb, OG: padding)
		std::uint32_t           padD8;               // 0xD8
		std::uint32_t           padDC;               // 0xDC
	};
	static_assert(sizeof(HitData) == 0xE0);

	// =================================================================
	// TESHitEvent -- defined at NG size (0x110). On OG (event size 0x108)
	// the engine still writes only the first 0x108 bytes; reading
	// target/cause at 0xE0 / 0xE8 is correct in BOTH versions because
	// OG has 8 bytes of explicit gap padding after HitData[0xD8].
	// =================================================================
	class TESHitEvent
	{
	public:
		HitData         hitData;            // 0x00 (size 0xE0)
		TESObjectREFR*  target;             // 0xE0 (NiPointer is just a pointer wrapper)
		TESObjectREFR*  cause;              // 0xE8
		std::uint64_t   materialName;       // 0xF0 (BSFixedString; we don't use it)
		std::uint32_t   sourceFormID;       // 0xF8
		std::uint32_t   projectileFormID;   // 0xFC
		bool            usesHitData;        // 0x100
		std::uint8_t    pad101[7];          // 0x101
		std::uint64_t   pad108;             // 0x108 (NG has 8 extra bytes here)
	};
	static_assert(sizeof(TESHitEvent) == 0x110);
}

// =====================================================================
// Event sink registration helpers (version-aware)
// =====================================================================
namespace HSK
{
	// Captured runtime version. Set by main.cpp at F4SEPlugin_Load time
	// (since CommonLibF4 doesn't expose F4SE::GetLoadInterface()).
	inline REL::Version g_runtimeVersion{ 0, 0, 0, 0 };

	// Returns true if running on Next Gen (1.10.980+).
	[[nodiscard]] inline bool IsNextGen()
	{
		const auto v = g_runtimeVersion;
		// NG starts at 1.10.980; anything below is OG.
		if (v[0] != 1) return v[0] > 1;
		if (v[1] != 10) return v[1] > 10;
		return v[2] >= 980;
	}

	// =================================================================
	// HitData accessors that pick the correct offset based on version.
	// Pass the const HitData& we receive in ProcessEvent.
	// =================================================================

	// Damage limb -- OG int32 at 0xD0, NG EnumSet u32 at 0xD4.
	[[nodiscard]] inline std::uint32_t GetDamageLimb(const RE::HitData& a_hd)
	{
		const auto* base = reinterpret_cast<const std::uint8_t*>(&a_hd);
		if (IsNextGen()) {
			return *reinterpret_cast<const std::uint32_t*>(base + 0xD4);
		}
		return *reinterpret_cast<const std::uint32_t*>(base + 0xD0);
	}

	// Material -- OG u32 at 0xCC, NG u32 at 0xD0.
	[[nodiscard]] inline std::uint32_t GetMaterial(const RE::HitData& a_hd)
	{
		const auto* base = reinterpret_cast<const std::uint8_t*>(&a_hd);
		if (IsNextGen()) {
			return *reinterpret_cast<const std::uint32_t*>(base + 0xD0);
		}
		return *reinterpret_cast<const std::uint32_t*>(base + 0xCC);
	}

	// Total damage -- OG totalDamage at 0x98, NG totalDamage at 0x94.
	[[nodiscard]] inline float GetTotalDamage(const RE::HitData& a_hd)
	{
		const auto* base = reinterpret_cast<const std::uint8_t*>(&a_hd);
		if (IsNextGen()) {
			return *reinterpret_cast<const float*>(base + 0x94);
		}
		return *reinterpret_cast<const float*>(base + 0x98);
	}

	// HitData::Flag bits (matches both OG and NG -- verified against
	// PluginTemplate/fo4test/PostNG/Events.h). Stored as u32 at 0xC4.
	enum HitFlag : std::uint32_t
	{
		kHitFlag_NoDamage    = 1u << 0,
		kHitFlag_Bash        = 1u << 1,
		kHitFlag_Sneak       = 1u << 2,
		kHitFlag_Recoil      = 1u << 3,
		kHitFlag_Explosion   = 1u << 4,
		kHitFlag_Melee       = 1u << 5,
		kHitFlag_Ranged      = 1u << 6,
		kHitFlag_Critical    = 1u << 7,
		kHitFlag_PowerAttack = 1u << 8,
	};

	[[nodiscard]] inline bool IsExplosion(const RE::HitData& a_hd)
	{
		return (a_hd.flags & kHitFlag_Explosion) != 0;
	}

	[[nodiscard]] inline bool IsMelee(const RE::HitData& a_hd)
	{
		return (a_hd.flags & kHitFlag_Melee) != 0;
	}

	// =================================================================
	// LIMB_ENUM (BGSBodyPartDefs::LIMB_ENUM is forward-declared in
	// CommonLibF4 with no values exposed; the engine uses the BodyPartData
	// record's own enumeration where the head is typically index 1 or 2
	// depending on the body part data record. We treat both as "head".
	// =================================================================
	enum LIMB_ENUM : std::uint32_t
	{
		kLimb_None  = 0xFFFFFFFFu,
		kLimb_Torso = 0,
		// Head can map to either 1 or 2 depending on the BPD record;
		// see DefaultBodyPartData (vanilla F4 humans) where head=1, and
		// CreatureBodyPartData where head can be 2.
		kLimb_Head1 = 1,
		kLimb_Head2 = 2,
	};

	[[nodiscard]] inline bool IsHeadLimb(std::uint32_t a_limb)
	{
		return a_limb == 1u || a_limb == 2u;
	}

	// =================================================================
	// Event source resolution (REL::Relocation does not throw on missing
	// IDs — it fails fast — so we must pick IDs that exist per runtime).
	//
	// Post-NG: TESHitEvent::GetEventSource() thunk  -> REL::ID(1411899)
	// Pre-NG:  HitEventSource global singleton ptr -> REL::ID(989868)
	// Legacy RegisterForHit / UnregisterForHit thunks: 1240328 / 973940
	// =================================================================

	[[nodiscard]] inline RE::BSTEventSource<RE::TESHitEvent>* GetHitEventSource()
	{
		if (IsNextGen()) {
			using func_t = RE::BSTEventSource<RE::TESHitEvent>* (*)();
			REL::Relocation<func_t> func{ REL::ID(1411899) };
			return func();
		}
		// Pre-NG: HitEventSource* at fixed RVA (CommonLibF4PreNG Events.h).
		struct HitEventSourceSingleton;
		REL::Relocation<HitEventSourceSingleton*> singleton{ REL::ID(989868) };
		return reinterpret_cast<RE::BSTEventSource<RE::TESHitEvent>*>(singleton.get());
	}

	// Returns true on successful registration via either path.
	inline bool RegisterHitEventSink(RE::BSTEventSink<RE::TESHitEvent>* a_sink)
	{
		if (auto* src = GetHitEventSource()) {
			src->RegisterSink(a_sink);
			return true;
		}
		// Legacy path (OG): RegisterForHit static thunk
		try {
			using func_t = void (*)(RE::BSTEventSink<RE::TESHitEvent>*);
			REL::Relocation<func_t> func{ REL::ID(1240328) };
			func(a_sink);
			return true;
		} catch (...) {
			return false;
		}
	}

	inline void UnregisterHitEventSink(RE::BSTEventSink<RE::TESHitEvent>* a_sink)
	{
		if (auto* src = GetHitEventSource()) {
			src->UnregisterSink(a_sink);
			return;
		}
		try {
			using func_t = void (*)(RE::BSTEventSink<RE::TESHitEvent>*);
			REL::Relocation<func_t> func{ REL::ID(973940) };
			func(a_sink);
		} catch (...) {
			// no-op
		}
	}


	// =================================================================
	// TESMagicEffectApplyEvent (legendary mutation detection).
	// Post-NG: GetEventSource() thunk -> REL::ID(1327824)
	// Pre-NG:  MGEFApplyEventSource*  -> REL::ID(1481228) (PreNG Events.h)
	// =================================================================
	[[nodiscard]] inline RE::BSTEventSource<RE::TESMagicEffectApplyEvent>* GetMagicEffectApplyEventSource()
	{
		if (IsNextGen()) {
			using func_t = RE::BSTEventSource<RE::TESMagicEffectApplyEvent>* (*)();
			REL::Relocation<func_t> func{ REL::ID(1327824) };
			return func();
		}
		struct MGEFApplyEventSourceSingleton;
		REL::Relocation<MGEFApplyEventSourceSingleton*> singleton{ REL::ID(1481228) };
		return reinterpret_cast<RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*>(singleton.get());
	}

	inline bool RegisterMagicEffectApplyEventSink(RE::BSTEventSink<RE::TESMagicEffectApplyEvent>* a_sink)
	{
		if (auto* src = GetMagicEffectApplyEventSource()) {
			src->RegisterSink(a_sink);
			return true;
		}
		return false;
	}

	inline void UnregisterMagicEffectApplyEventSink(RE::BSTEventSink<RE::TESMagicEffectApplyEvent>* a_sink)
	{
		if (auto* src = GetMagicEffectApplyEventSource()) {
			src->UnregisterSink(a_sink);
		}
	}

	// =================================================================
	// TESContainerChangedEvent registration (REL::ID(242538))
	// =================================================================
	[[nodiscard]] inline RE::BSTEventSource<RE::TESContainerChangedEvent_Compat>* GetContainerChangedEventSource()
	{
		try {
			using func_t = RE::BSTEventSource<RE::TESContainerChangedEvent_Compat>* (*)();
			REL::Relocation<func_t> func{ REL::ID(242538) };
			return func();
		} catch (...) {
			return nullptr;
		}
	}

	inline bool RegisterContainerChangedEventSink(RE::BSTEventSink<RE::TESContainerChangedEvent_Compat>* a_sink)
	{
		if (auto* src = GetContainerChangedEventSource()) {
			src->RegisterSink(a_sink);
			return true;
		}
		return false;
	}

	inline void UnregisterContainerChangedEventSink(RE::BSTEventSink<RE::TESContainerChangedEvent_Compat>* a_sink)
	{
		if (auto* src = GetContainerChangedEventSource()) {
			src->UnregisterSink(a_sink);
		}
	}
}

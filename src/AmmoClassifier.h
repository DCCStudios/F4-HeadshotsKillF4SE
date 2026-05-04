#pragma once

#include "PCH.h"
#include "Settings.h"

namespace HSK
{
	// =====================================================================
	// AmmoClassifier
	//
	// Loads ammo_calibers.json, scans every TESAmmo form on game data
	// ready, classifies each into a Caliber + DamageType, and caches
	// the result keyed by ammo FormID for O(1) lookup at hit time.
	//
	// Classification priority (highest first):
	//   1. User INI override     (Settings::ammoOverrides)
	//   2. JSON exact match      ("PluginName.esp:EditorID")
	//   3. JSON EditorID match   (ignore source plugin)
	//   4. Heuristic substrings  (EditorID + projectile type + weapon keywords)
	//   5. Default               -> Pistol / Ballistic
	// =====================================================================
	class AmmoClassifier
	{
	public:
		static AmmoClassifier* GetSingleton()
		{
			static AmmoClassifier singleton;
			return &singleton;
		}

		// Loads JSON and scans all TESAmmo forms. Idempotent; safe to call again on PostLoadGame.
		void Init();

		// Re-categorize without reloading JSON (e.g. when user toggles overrides in the UI).
		void Recategorize();

		// True after the first ScanAllAmmo() has populated _cache. Used by the
		// menu UI to show "loading..." instead of an empty list at main menu.
		[[nodiscard]] bool IsInitialized() const { return _initialized; }

		// O(1) lookup. Returns Pistol/Ballistic if not found (safe fallback).
		[[nodiscard]] AmmoEntry Classify(const RE::TESAmmo* a_ammo) const;

		// Direct category-only lookup (faster path for hot loop).
		[[nodiscard]] Caliber GetCaliber(const RE::TESAmmo* a_ammo) const;
		[[nodiscard]] DamageType GetDamageType(const RE::TESAmmo* a_ammo) const;
		[[nodiscard]] bool       IsExcluded(const RE::TESAmmo* a_ammo) const;

		// Snapshot of all classified ammo for the menu UI.
		[[nodiscard]] std::vector<AmmoEntry> GetAllEntries() const;

		// User override interface (called from menu).
		void SetOverride(const std::string& a_pluginColonEdid, Caliber a_c, DamageType a_dt);
		void ClearOverride(const std::string& a_pluginColonEdid);

		// Build the override key for an ammo form: "PluginName.esp:EditorID" (lowercase).
		[[nodiscard]] static std::string MakeOverrideKey(const RE::TESAmmo* a_ammo);
		[[nodiscard]] static std::string MakeOverrideKey(std::string_view a_plugin, std::string_view a_edid);

	private:
		AmmoClassifier() = default;
		~AmmoClassifier() = default;
		AmmoClassifier(const AmmoClassifier&) = delete;
		AmmoClassifier& operator=(const AmmoClassifier&) = delete;

		struct JsonEntry
		{
			std::string sourcePlugin;  // "" = match by EditorID only
			std::string editorID;
			Caliber     caliber{ Caliber::Pistol };
			DamageType  damageType{ DamageType::Ballistic };
		};

		struct Heuristics
		{
			std::vector<std::string> excludeSubs;
			std::vector<std::string> shotgunSubs;
			std::vector<std::string> pistolSubs;
			std::vector<std::string> rifleSubs;
			std::vector<std::string> largeRifleSubs;
			std::vector<std::string> energySubs;
		};

		void LoadJson();
		void ScanAllAmmo();      // public-ish entry; takes lock
		void ScanAllAmmoImpl();  // caller must hold _mutex

		// ClassifyOne returns the entry. The reason string explains where the
		// classification came from (override / json-exact / json-edid / heuristic /
		// default) and is logged when debug logging is enabled.
		AmmoEntry ClassifyOne(const RE::TESAmmo* a_ammo, std::string* a_outReason = nullptr) const;
		bool      ApplyJsonMatch(const std::string& a_pluginColonEdid, const std::string& a_edid, AmmoEntry& a_out, std::string* a_outReason = nullptr) const;
		// Returns a non-empty reason string if heuristics produced a result.
		std::string ApplyHeuristics(const RE::TESAmmo* a_ammo, const std::string& a_edidLower, AmmoEntry& a_out) const;

		mutable std::shared_mutex                  _mutex;
		mutable std::unordered_map<std::uint32_t, AmmoEntry> _cache;          // formID -> entry
		std::unordered_map<std::string, JsonEntry> _jsonByPluginEdid; // "plugin:edid" lowercase
		std::unordered_map<std::string, JsonEntry> _jsonByEdid;       // edid lowercase
		Heuristics                                 _heuristics;
		std::atomic<bool>                          _initialized{ false };
	};
}

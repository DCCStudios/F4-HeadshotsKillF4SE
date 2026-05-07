#pragma once

#include "PCH.h"

namespace HSK
{
	// Caliber categories (drives all chance calculations)
	enum class Caliber : std::uint8_t
	{
		Excluded   = 0,  // launchers, mines, mininukes, flamer, junk, etc.
		Pistol     = 1,  // .38, 10mm, .44, 9mm, etc.
		Shotgun    = 2,  // 12 gauge, 20 gauge, .410, etc.
		Rifle      = 3,  // 5.56, 5mm, .45, .380, .357 SIG, .30-30, harpoons, railway spikes, etc.
		LargeRifle = 4,  // .50, .308, .338, 7.62x51, 7.62x54r, etc.
	};

	// Damage type for chance calculation. Energy uses energy resistance instead of ballistic AR.
	enum class DamageType : std::uint8_t
	{
		Ballistic = 0,
		Energy    = 1,
	};

	struct AmmoEntry
	{
		std::uint32_t formID{ 0 };          // resolved at runtime (light master pages applied)
		std::string   editorID;             // for display + manual override matching
		std::string   sourcePlugin;         // for source-mod heuristics ("Munitions.esm", etc.)
		Caliber       caliber{ Caliber::Pistol };
		DamageType    damageType{ DamageType::Ballistic };
		bool          autoClassified{ true };  // false if user has overridden
		// Filled at scan time -- explains how this entry got its caliber. Used
		// by the menu UI's per-row tooltip and by the debug log.
		std::string   classificationReason;
		// Raw ammo damage from BGSAmmoData. Useful context in the browser.
		float         ammoDamage{ 0.0f };
	};

	// Per-actor-category instakill chance settings.
	struct ChanceSettings
	{
		float humanoid{ 100.0f };
		float feralGhoul{ 100.0f };  // Humanoid races matched by feralGhoulRacePatterns; helmet ignored for chance
		float smallCreature{ 100.0f };
		float armoredCreature{ 60.0f };
		float largeCreature{ 25.0f };
		float superMutant{ 20.0f };
		float deathclaw{ 15.0f };
		float mirelurkQueen{ 10.0f };
		float synth{ 25.0f };      // gen-3 synths -- treated similarly to large creatures
	};

	// Per-caliber-vs-target multipliers (0.0-1.0+ multipliers on the base chance).
	struct CaliberModifierSettings
	{
		// Armored target (helmet/PA)
		float pistolVsArmored{ 0.0f };       // pistol vs helmeted humanoid -> just helmet knockoff
		float shotgunVsArmored{ 0.0f };
		float rifleVsArmored{ 1.0f };
		float largeRifleVsArmored{ 1.5f };

		// Large/super-mutant/deathclaw targets
		float pistolVsLarge{ 0.0f };
		float shotgunVsLarge{ 0.0f };
		float rifleVsLarge{ 0.5f };
		float largeRifleVsLarge{ 1.0f };

		// Synth (gen-3) targets -- only large rifle by default
		float pistolVsSynth{ 0.0f };
		float shotgunVsSynth{ 0.0f };
		float rifleVsSynth{ 0.0f };
		float largeRifleVsSynth{ 1.0f };

		// Power Armor target (helmet has PA flag)
		float pistolVsPA{ 0.0f };
		float shotgunVsPA{ 0.0f };
		float rifleVsPA{ 0.0f };             // can knock helmet off but cannot kill
		float largeRifleVsPA{ 0.4f };

		// Feral ghoul (and modded races matched by feralGhoulRacePatterns): default 1.0
		// so pistol/shotgun/rifle/large rifle can all contribute (Excluded ammo stays 0).
		float pistolVsFeralGhoul{ 1.0f };
		float shotgunVsFeralGhoul{ 1.0f };
		float rifleVsFeralGhoul{ 1.0f };
		float largeRifleVsFeralGhoul{ 1.0f };
	};

	struct HelmetSettings
	{
		bool  enableHelmetKnockoff{ true };
		float knockoffChancePistol{ 30.0f };
		float knockoffChanceRifle{ 50.0f };
		float knockoffChanceShotgun{ 40.0f };
		float paKnockoffChanceRifle{ 15.0f };
		float paKnockoffChanceLargeRifle{ 30.0f };
		float dropLinearImpulse{ 500.0f };  // Havok impulse magnitude (ApplyHavokImpulse; typical range 100–1000)
		float dropAngularImpulse{ 0.10f };
		float dropSpawnHeight{ 20.0f };     // upward bias (game units) added to head position on spawn
		// 0 = impulse along shot travel; 1 = opposite (helmet flies back from impact). Default 1.
		float dropFlyAgainstShot{ 1.0f };
		// Added to Z after horizontal dir is normalized; re-normalized in KnockOff. Default 0.4.
		float dropFlyUpLift{ 0.4f };
		// Window (seconds) within which subsequent shotgun pellets are grouped with the first hit
		float shotgunPelletGroupWindowSec{ 0.10f };
	};

	struct MeleeSettings
	{
		bool  enableMeleeKnockoff{ true };
		float meleeKnockoffChanceMedium{ 10.0f };  // one-handed melee weapons
		float meleeKnockoffChanceLarge{ 20.0f };   // two-handed / heavy melee weapons
		bool  gunBashKnockoff{ true };             // gun bash (melee with a firearm)
		float gunBashKnockoffChance{ 15.0f };      // chance when bashing with a non-pistol firearm
	};

	struct ArmorScalingSettings
	{
		// Sigmoid half-life: the AR at which headshot chance drops to 50%.
		// Uses formula:  scale = 1 / (1 + ar / halfAR)
		float ballisticHalfAR{ 18.0f };
		float energyHalfAR{ 60.0f };
		// When energy weapons hit, ballistic AR still contributes this fraction
		// to the effective AR (e.g., 0.3 means 30% of ballistic AR is added).
		float crossResistFactor{ 0.3f };
	};

	struct AdvancedChanceSettings
	{
		// D: Ammo damage scaling -- higher-damage rounds get a chance boost.
		//    Formula: chance *= lerp(1.0, ammoDamage / referenceDamage, influence)
		float ammoDamageReferenceDamage{ 30.0f };  // "baseline" ammo damage (roughly a .308)
		float ammoDamageInfluence{ 0.5f };          // 0 = disabled, 1 = full influence

		// E: Distance falloff
		bool  enableDistanceFalloff{ true };
		float distanceFullChanceUnits{ 3000.0f };  // ~50m in Bethesda units (1 unit = ~1.4cm)
		float distanceZeroChanceUnits{ 15000.0f }; // ~210m, beyond this chance is 0

		// F: Stealth/sneak bonus
		float sneakBonusMul{ 1.25f };  // multiplier when attacker is sneaking (undetected)

		// H: VATS / critical hit settings
		bool  vatsRequiresCritical{ true };   // in VATS, only critical hits trigger headshot eval
		float criticalBonusChance{ 15.0f };   // outside VATS, critical hits add this flat % bonus to instakill chance
	};

	struct VisualEffectLayer
	{
		bool        enabled{ true };
		std::string imodEditorID{};
		float       duration{ 2.0f };
		float       strength{ 1.0f };
	};

	struct PlayerFeedbackSettings
	{
		bool  enableFeedback{ true };

		// Tinnitus sound (custom WAV played via Win32)
		bool        enableTinnitusSound{ true };
		float       tinnitusSoundVolume{ 0.7f };
		std::string tinnitusSoundFile{ "headshot_tinnitus.wav" };

		// Audio muffle: low-pass filter on game audio that fades back to normal
		bool  enableAudioMuffle{ true };
		float muffleIntensity{ 0.15f };     // frequency mult at peak muffle (0.0 = full mute, 1.0 = no effect)
		float muffleFadeDuration{ 3.0f };   // seconds to fade from muffled back to normal

		// Visual effects: three independent layers applied simultaneously
		VisualEffectLayer concussion{ true, "ImageSpaceConcussion", 1.5f, 1.0f };
		VisualEffectLayer impactFlash{ true, "WhiteoutImodToNormal", 1.0f, 0.5f };
		VisualEffectLayer greyscale{ true, "zd_ScopeTargetingRecon", 2.5f, 1.0f };

		// Cooldown to prevent spam from rapid headshot pellets / multi-hits
		float       feedbackCooldown{ 5.0f };     // minimum seconds between triggers
	};

	struct KillImpulseSettings
	{
		bool  enabled{ true };
		bool  applyOnAllHeadshots{ false }; // true = every headshot on non-player humanoids (incl. ferals), false = instakill only
		float magnitude{ 25.0f };           // head snap angle in degrees
		float upwardBias{ 0.3f };           // upward tilt added to rotation axis (head tilts back + up)
		float decayDuration{ 1.5f };        // seconds to hold/decay the snap back to neutral
	};

	struct LegendarySettings
	{
		bool  respectLegendaryMutation{ true };
		float legendaryCooldownSeconds{ 3.0f };
		// How much the health ratio must increase to count as a "mutation" (Layer 3 fallback)
		float mutationHealthRatioThreshold{ 0.20f };
	};

	class Settings
	{
	public:
		static Settings* GetSingleton()
		{
			static Settings singleton;
			return &singleton;
		}

		void Load();
		void Save();
		// Persists ONLY the [AmmoOverrides] section to disk -- preserves
		// any other section as it currently exists on disk. This is what
		// the ammo browser uses so that pending (unsaved) changes on
		// other pages aren't accidentally committed when the user tweaks
		// an ammo override.
		void SaveAmmoOverridesOnly();

		// Public flat settings -------------------------------------------------

		// [General]
		bool  enabled{ true };
		bool  debugLogging{ false };
		float killDamage{ 99999.0f };

		// [Victims]
		bool  applyToPlayerAndFollowers{ false };
		float twoShotWindowSeconds{ 120.0f };   // player/follower two-shot rule: 2nd headshot within this many seconds kills
		float twoShotNearDeathRatio{ 0.05f };   // first shot leaves the target at this fraction of max HP
		int   levelGapThreshold{ 20 };  // skip enemies more than this many levels above the player
		int   essentialMode{ 0 };        // 0 = no kill on essentials, 1 = bleedout, 2 = ignore essential flag

		// [Chances]
		ChanceSettings chances{};

		// [CaliberModifiers]
		CaliberModifierSettings caliberMods{};

		// [Helmet]
		HelmetSettings helmet{};

		// [Melee]
		MeleeSettings melee{};

		// [ArmorScaling]
		ArmorScalingSettings armorScaling{};

		// [Advanced]
		AdvancedChanceSettings advanced{};

		// [PlayerFeedback]
		PlayerFeedbackSettings playerFeedback{};

		// [KillImpulse]
		KillImpulseSettings killImpulse{};

	// [Helmet]  -- player-specific extensions
	bool  playerHelmetKnockoffEnabled{ true }; // master toggle: can the player's helmet be knocked off?
	float playerHelmetKnockoffMult{ 1.0f };    // multiplier applied to the base knockoff chance when target is player
	bool  playerHelmetProtection{ false };     // if true, player can only be instakilled while bare-headed
	float playerInstakillMinAR{ 8.0f };        // (toggle OFF) player can only be instakilled if head ballistic AR < this value
	bool  helmetNotifyPlayer{ true };    // show HUD notification when player's helmet is knocked off
	bool  helmetAutoReequip{ true };     // auto-equip helmet when player picks it back up
	bool  helmetShaderEnabled{ true };   // apply a glow shader to dropped helmet so player can find it
	std::string helmetShaderEditorID{ "WorkshopHighlightShader" }; // EditorID of the TESEffectShader
	float helmetShaderDuration{ 120.0f }; // seconds to keep shader active (-1 = indefinite)
	bool  helmetLightEnabled{ false };   // place an unshadowed point light at the dropped helmet
	std::string helmetLightEditorID{ "DefaultLight01NSFill" }; // EditorID of a TESObjectLIGH form to place
	bool  helmetTrackerEnabled{ true };  // show periodic HUD compass/distance indicator for dropped helmet
	float helmetTrackerIntervalSec{ 3.0f }; // how often the tracker message refreshes (seconds)
	bool  followerHelmetRestore{ true }; // auto-restore knocked-off helmet on followers when combat ends

		// [Legendary]
		LegendarySettings legendary{};

		// [Lists]
		// Comma-separated race EditorIDs (or just plain text patterns) to exclude from this mod entirely.
		std::vector<std::string> raceBlocklist{};
		// Substrings of TESRace EditorID; if matched and final category is Humanoid, actor is
		// treated as feral ghoul (separate base chance + caliber row; helmet ignored for instakill chance).
		std::vector<std::string> feralGhoulRacePatterns{ "FeralGhoul" };
		// Keywords that immediately exempt the actor (matched on RE::BGSKeyword EditorID).
		std::vector<std::string> keywordImmuneList{ "ActorTypeRobot" };
		// Per-race category overrides. Keyed by a case-insensitive substring
		// of the Race EditorID (e.g. "CWYaoGuai" or "bear"). Value is the
		// integer form of ActorCategory (1=Humanoid … 7=Synth).
		// Checked FIRST in Classify(), before keywords or built-in heuristics.
		std::unordered_map<std::string, int> raceCategoryOverrides{};

		// Per-form-ID user overrides for ammo categorization.
		// Keyed by the ammo's source plugin + ":" + EditorID (so we don't depend on load-order FormIDs).
		// value: integer Caliber value (0=Excluded ... 4=LargeRifle)
		std::unordered_map<std::string, int> ammoOverrides{};

		// Path constants
		static constexpr const char* kIniPath  = "Data\\F4SE\\Plugins\\HeadshotsKillF4SE.ini";
		static constexpr const char* kJsonPath = "Data\\F4SE\\Plugins\\HeadshotsKillF4SE\\ammo_calibers.json";

	private:
		Settings() = default;
		~Settings() = default;
		Settings(const Settings&) = delete;
		Settings(Settings&&) = delete;
		Settings& operator=(const Settings&) = delete;
		Settings& operator=(Settings&&) = delete;

		mutable std::shared_mutex _mutex;
	};

	// Helper: split a comma-separated string into a vector of trimmed strings.
	std::vector<std::string> SplitCommaList(const std::string& a_str);
	std::string              JoinCommaList(const std::vector<std::string>& a_list);
	std::string              CaliberToString(Caliber a_c);
	Caliber                  CaliberFromString(std::string_view a_s);
	const char*              CaliberDisplay(Caliber a_c);
}

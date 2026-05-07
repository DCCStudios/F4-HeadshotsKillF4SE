#include "Menu.h"
#include "Settings.h"
#include "AmmoClassifier.h"
#include "ActorClassifier.h"

using namespace ImGuiMCP;

namespace HSK::Menu
{
	// =====================================================================
	// Helpers
	// =====================================================================

	// Attach a tooltip to the most-recently-drawn ImGui item.
	static void Tooltip(const char* a_text)
	{
		if (!a_text || !*a_text) return;
		if (ImGuiMCP::IsItemHovered(0)) {
			ImGuiMCP::SetTooltip("%s", a_text);
		}
	}

	static bool ContainsCaseInsensitive(std::string_view a_haystack, std::string_view a_needle)
	{
		if (a_needle.empty() || a_haystack.size() < a_needle.size()) return false;
		auto it = std::search(
			a_haystack.begin(), a_haystack.end(),
			a_needle.begin(), a_needle.end(),
			[](char a, char b) {
				return std::tolower(static_cast<unsigned char>(a)) ==
					   std::tolower(static_cast<unsigned char>(b));
			});
		return it != a_haystack.end();
	}

	void DrawSaveStatus()
	{
		if (State::saveStatusTimer > 0.0f) {
			State::saveStatusTimer -= 0.016f;  // assume ~60fps decay
			ImGuiMCP::TextDisabled("%s", State::saveStatusMsg.c_str());
		}
	}

	void MarkDirty()
	{
		State::dirty = true;
	}

	void CommitPending()
	{
		Settings::GetSingleton()->Save();
		State::dirty = false;
		State::saveStatusMsg = "Settings saved to HeadshotsKillF4SE.ini";
		State::saveStatusTimer = 3.0f;
	}

	void DiscardPending()
	{
		Settings::GetSingleton()->Load();

		// Refresh text input buffers so the UI matches the on-disk values.
		const auto& s = *Settings::GetSingleton();
		const std::string r = JoinCommaList(s.raceBlocklist);
		std::strncpy(State::raceBlacklistInput, r.c_str(), sizeof(State::raceBlacklistInput) - 1);
		State::raceBlacklistInput[sizeof(State::raceBlacklistInput) - 1] = '\0';
		const std::string fg = JoinCommaList(s.feralGhoulRacePatterns);
		std::strncpy(State::feralGhoulRacePatternsInput, fg.c_str(), sizeof(State::feralGhoulRacePatternsInput) - 1);
		State::feralGhoulRacePatternsInput[sizeof(State::feralGhoulRacePatternsInput) - 1] = '\0';
		const std::string k = JoinCommaList(s.keywordImmuneList);
		std::strncpy(State::keywordImmuneInput, k.c_str(), sizeof(State::keywordImmuneInput) - 1);
		State::keywordImmuneInput[sizeof(State::keywordImmuneInput) - 1] = '\0';

		// Player feedback inputs
		std::strncpy(State::tinnitusSoundFileInput, s.playerFeedback.tinnitusSoundFile.c_str(),
			sizeof(State::tinnitusSoundFileInput) - 1);
		State::tinnitusSoundFileInput[sizeof(State::tinnitusSoundFileInput) - 1] = '\0';
		std::strncpy(State::concussionIModInput, s.playerFeedback.concussion.imodEditorID.c_str(),
			sizeof(State::concussionIModInput) - 1);
		State::concussionIModInput[sizeof(State::concussionIModInput) - 1] = '\0';
		std::strncpy(State::impactFlashIModInput, s.playerFeedback.impactFlash.imodEditorID.c_str(),
			sizeof(State::impactFlashIModInput) - 1);
		State::impactFlashIModInput[sizeof(State::impactFlashIModInput) - 1] = '\0';
		std::strncpy(State::greyscaleIModInput, s.playerFeedback.greyscale.imodEditorID.c_str(),
			sizeof(State::greyscaleIModInput) - 1);
		State::greyscaleIModInput[sizeof(State::greyscaleIModInput) - 1] = '\0';

		State::dirty = false;
		State::saveStatusMsg = "Discarded unsaved changes; reloaded from disk.";
		State::saveStatusTimer = 3.0f;
	}

	// Persistent toolbar shown at the top of every settings page.
	void RenderSaveBar()
	{
		const bool dirty = State::dirty;

		// Status pill (left).
		if (dirty) {
			ImGuiMCP::TextColored({ 1.00f, 0.70f, 0.20f, 1.0f },
				"* Unsaved changes (active in this session, not yet on disk)");
		} else {
			ImGuiMCP::TextDisabled("All settings saved.");
		}
		Tooltip(dirty
			? "You have edits in memory. They are LIVE in the current game "
			  "session (the hit logic uses them right now), but they are not "
			  "yet written to HeadshotsKillF4SE.ini. Click 'Save settings' "
			  "to persist or 'Discard' to revert from disk."
			: "Nothing to save. Slide a value or toggle a checkbox to begin "
			  "making changes.");

		// Right-aligned button row.
		ImGuiMCP::SameLine(0.0f, 20.0f);
		if (dirty) {
			if (ImGuiMCP::Button("Save settings")) {
				CommitPending();
			}
			Tooltip("Write every checkbox / slider / list on every page to "
				"HeadshotsKillF4SE.ini. Ammo overrides save automatically and "
				"are not affected by this button.");

			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button("Discard changes")) {
				DiscardPending();
			}
			Tooltip("Throw away your unsaved changes and reload all values "
				"from HeadshotsKillF4SE.ini.");
		} else {
			ImGuiMCP::BeginDisabled(true);
			(void)ImGuiMCP::Button("Save settings");
			ImGuiMCP::SameLine();
			(void)ImGuiMCP::Button("Discard changes");
			ImGuiMCP::EndDisabled();
			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button("Reload from disk")) {
				DiscardPending();
				State::saveStatusMsg = "Reloaded settings from disk.";
				State::saveStatusTimer = 3.0f;
			}
			Tooltip("Re-read HeadshotsKillF4SE.ini from disk. Useful if you "
				"hand-edited the INI outside the game.");
		}
		ImGuiMCP::Separator();
		ImGuiMCP::Spacing();
	}

	// Page header: title + description + the persistent save toolbar.
	static void PageHeader(const char* a_title, const char* a_description)
	{
		ImGuiMCP::SeparatorText(a_title);
		if (a_description && *a_description) {
			ImGuiMCP::TextWrapped("%s", a_description);
			ImGuiMCP::Spacing();
		}
		RenderSaveBar();
	}

	// Section divider with optional one-line caption + tooltip.
	static void SectionHeader(const char* a_title, const char* a_tooltip = nullptr)
	{
		ImGuiMCP::Spacing();
		ImGuiMCP::SeparatorText(a_title);
		if (a_tooltip) Tooltip(a_tooltip);
	}

	// Slider / checkbox helpers. Mark dirty on change instead of writing to
	// disk immediately. The user must click Save settings (in the toolbar at
	// the top of the page) to persist.
	static bool SliderFloatSave(const char* label, float* v, float min, float max, const char* a_tooltip = nullptr, const char* format = "%.2f")
	{
		const bool changed = ImGuiMCP::SliderFloat(label, v, min, max, format);
		Tooltip(a_tooltip);
		if (changed) MarkDirty();
		return changed;
	}

	static bool SliderIntSave(const char* label, int* v, int min, int max, const char* a_tooltip = nullptr)
	{
		const bool changed = ImGuiMCP::SliderInt(label, v, min, max, "%d");
		Tooltip(a_tooltip);
		if (changed) MarkDirty();
		return changed;
	}

	static bool CheckboxSave(const char* label, bool* v, const char* a_tooltip = nullptr)
	{
		const bool changed = ImGuiMCP::Checkbox(label, v);
		Tooltip(a_tooltip);
		if (changed) MarkDirty();
		return changed;
	}

	// Forward declarations
	void __stdcall RenderGeneral();
	void __stdcall RenderChances();
	void __stdcall RenderCaliberMods();
	void __stdcall RenderCaliberRules();
	void __stdcall RenderHelmet();
	void __stdcall RenderAmmoBrowser();
	void __stdcall RenderRaceBlacklist();
	void __stdcall RenderKillImpulse();
	void __stdcall RenderLegendary();
	void __stdcall RenderPlayerFeedback();
	void __stdcall RenderDebug();
	void __stdcall RenderAbout();

	// =====================================================================
	// Registration
	// =====================================================================
	void Register()
	{
		if (State::initialized) return;
		State::initialized = true;

		if (!F4SEMenuFramework::IsInstalled()) {
			logger::warn("[HSK] F4SE Menu Framework not installed -- in-game UI will not be available");
			return;
		}

		F4SEMenuFramework::SetSection("HeadshotsKill F4SE");
		F4SEMenuFramework::AddSectionItem("General",        RenderGeneral);
		F4SEMenuFramework::AddSectionItem("Chances",        RenderChances);
		F4SEMenuFramework::AddSectionItem("Caliber Mods",   RenderCaliberMods);
		F4SEMenuFramework::AddSectionItem("Caliber Rules",  RenderCaliberRules);
		F4SEMenuFramework::AddSectionItem("Helmets",        RenderHelmet);
		F4SEMenuFramework::AddSectionItem("Ammo Browser",   RenderAmmoBrowser);
		F4SEMenuFramework::AddSectionItem("Race Blacklist", RenderRaceBlacklist);
		F4SEMenuFramework::AddSectionItem("Kill Impulse",     RenderKillImpulse);
		F4SEMenuFramework::AddSectionItem("Legendary",        RenderLegendary);
		F4SEMenuFramework::AddSectionItem("Player Feedback", RenderPlayerFeedback);
		F4SEMenuFramework::AddSectionItem("Debug",            RenderDebug);
		F4SEMenuFramework::AddSectionItem("About",           RenderAbout);

		// Seed the race blacklist input from settings
		const auto& s = *Settings::GetSingleton();
		std::string joinedRaces = JoinCommaList(s.raceBlocklist);
		std::strncpy(State::raceBlacklistInput, joinedRaces.c_str(), sizeof(State::raceBlacklistInput) - 1);
		std::string joinedFeral = JoinCommaList(s.feralGhoulRacePatterns);
		std::strncpy(State::feralGhoulRacePatternsInput, joinedFeral.c_str(), sizeof(State::feralGhoulRacePatternsInput) - 1);
		State::feralGhoulRacePatternsInput[sizeof(State::feralGhoulRacePatternsInput) - 1] = '\0';
		std::string joinedKWs = JoinCommaList(s.keywordImmuneList);
		std::strncpy(State::keywordImmuneInput, joinedKWs.c_str(), sizeof(State::keywordImmuneInput) - 1);

		// Player feedback inputs
		std::strncpy(State::tinnitusSoundFileInput, s.playerFeedback.tinnitusSoundFile.c_str(),
			sizeof(State::tinnitusSoundFileInput) - 1);
		std::strncpy(State::concussionIModInput, s.playerFeedback.concussion.imodEditorID.c_str(),
			sizeof(State::concussionIModInput) - 1);
		std::strncpy(State::impactFlashIModInput, s.playerFeedback.impactFlash.imodEditorID.c_str(),
			sizeof(State::impactFlashIModInput) - 1);
		std::strncpy(State::greyscaleIModInput, s.playerFeedback.greyscale.imodEditorID.c_str(),
			sizeof(State::greyscaleIModInput) - 1);

		logger::info("[HSK] Menu registered with F4SE Menu Framework");
	}

	// =====================================================================
	// Pages
	// =====================================================================
	void __stdcall RenderGeneral()
	{
		auto* s = Settings::GetSingleton();

		PageHeader("HeadshotsKill F4SE -- General",
			"Master controls for the mod. Use this page to enable/disable the entire "
			"system, raise the killing damage value, and decide which actors are even "
			"considered eligible for an instant-kill headshot. Hover any control for a "
			"detailed description.");

		CheckboxSave("Enable mod", &s->enabled,
			"Master switch. When unchecked, no headshot evaluation runs and the mod is "
			"effectively dormant. Useful for quick A/B testing without removing the DLL.");
		CheckboxSave("Debug logging", &s->debugLogging,
			"Writes verbose decisions to HeadshotsKillF4SE.log -- per-hit evaluation, ammo "
			"classification reasons, helmet inspection, etc. Leave OFF for performance; "
			"turn ON when reporting issues or tuning the ammo browser.");
		SliderFloatSave("Kill damage", &s->killDamage, 1000.0f, 9999999.0f,
			"Raw damage applied on a successful headshot. Default 99999 is enough to drop "
			"any vanilla actor in a single hit. Increase if you run difficulty mods that "
			"give enemies massive HP pools; decrease if you want survivable headshots.",
			"%.0f");

		ImGuiMCP::Spacing();
		SectionHeader("Victim filters",
			"Conditions that decide whether the actor on the receiving end is even "
			"eligible for a headshot kill.");
		CheckboxSave("Apply to player and followers", &s->applyToPlayerAndFollowers,
			"If OFF (default), the mod ignores hits on the player and on companions/teammates "
			"so you can't accidentally headshot Dogmeat.\n\n"
			"If ON, a TWO-SHOT rule applies to keep things fair:\n"
			"  - 1st headshot: brings you to near-death (proportional to your current HP) but "
			"does NOT kill.\n"
			"  - 2nd headshot within the window below: kills.\n"
			"  - If the window expires, the next shot counts as a new 1st shot.\n"
			"Normal caliber-vs-armor rules still apply (helmet protects, PA protects more, etc.), "
			"but the two-shot safety net always kicks in for you and your companions.");
		SliderFloatSave("Two-shot kill window (seconds)", &s->twoShotWindowSeconds, 10.0f, 600.0f,
			"How many seconds after the first headshot the second shot can actually kill the "
			"player or follower. Default 120 (2 minutes). After this window expires the tracker "
			"resets and the next headshot is treated as a fresh 'first shot' again.",
			"%.0f");
		SliderFloatSave("Near-death HP ratio", &s->twoShotNearDeathRatio, 0.01f, 0.50f,
			"What fraction of max HP the first headshot leaves you at. Default 0.05 = 5%% of "
			"your max health -- enough to survive but in critical condition. Set higher (e.g. "
			"0.20) for a more forgiving experience, or lower (0.01) for razor-thin survival.",
			"%.2f");
		SliderIntSave("Level gap threshold", &s->levelGapThreshold, 0, 100,
			"Maximum allowed level difference (target_level - player_level). If the target "
			"is more than this many levels above you, the headshot is suppressed -- this "
			"prevents a 1-shot kill on, e.g., a Mythic Deathclaw at level 5. Set 100 to "
			"effectively disable the filter.");
		SliderIntSave("Essential mode (0=skip 1=bleedout 2=ignore)", &s->essentialMode, 0, 2,
			"How to handle Essential / Protected NPCs:\n"
			"  0 = Skip (don't apply damage, leave them be)\n"
			"  1 = Bleedout (apply damage, the engine will knock them down rather than kill)\n"
			"  2 = Ignore essential flag (apply full damage; will fail to kill truly essential actors but tries)");

		ImGuiMCP::Spacing();
		SectionHeader("VATS & critical hits",
			"Controls how headshot instakills interact with VATS and critical hits.");
		CheckboxSave("VATS requires critical hit", &s->advanced.vatsRequiresCritical,
			"When ON (default), headshots fired in VATS only trigger the instakill "
			"evaluation if the shot is a critical hit. Normal VATS headshots are ignored.\n\n"
			"When OFF, every VATS headshot is evaluated for instakill normally. Because "
			"VATS makes headshots trivially easy, this essentially guarantees instakills "
			"on unarmored targets.");
		SliderFloatSave("Critical hit bonus (%%)", &s->advanced.criticalBonusChance, 0.0f, 100.0f,
			"Flat %% added to the instakill chance when the shot is a critical hit "
			"OUTSIDE of VATS. Works with mods that allow critical hits in free-aim.\n\n"
			"Example: base chance is 60%%, critical bonus is 15%% -> final chance = 75%%.\n"
			"In VATS, critical hits use the normal chance (no additional bonus on top) "
			"because they already bypassed the 'VATS requires critical' gate.",
			"%.0f");

		ImGuiMCP::Spacing();
		SectionHeader("Combat modifiers",
			"Other modifiers that affect headshot chance based on combat context.");
		SliderFloatSave("Sneak bonus multiplier", &s->advanced.sneakBonusMul, 1.0f, 3.0f,
			"Chance multiplier when the attacker is sneaking (undetected). Default 1.25 = "
			"25%% increased instakill chance when sniping from stealth. Rewards careful "
			"positioning and punishes run-and-gun.",
			"%.2f");

		ImGuiMCP::Spacing();
		SectionHeader("Ammo damage scaling",
			"Higher-damage rounds get a better instakill chance. A .50 BMG deals more "
			"damage than a .308, so even though both are 'Large Rifle', the .50 should "
			"have an edge.");
		SliderFloatSave("Reference damage", &s->advanced.ammoDamageReferenceDamage, 1.0f, 200.0f,
			"The 'baseline' ammo damage value (a round at exactly this damage gets no "
			"bonus or penalty). Rounds above this get a proportional boost; below get "
			"a penalty. A .308 is roughly 30 damage.",
			"%.0f");
		SliderFloatSave("Damage influence", &s->advanced.ammoDamageInfluence, 0.0f, 1.0f,
			"How much the ammo damage ratio affects the chance. 0 = disabled (all rounds "
			"of the same caliber category behave identically). 1.0 = full influence (a "
			"round with 2x reference damage gets 2x chance boost).",
			"%.2f");

		ImGuiMCP::Spacing();
		SectionHeader("Distance falloff",
			"Headshots at extreme range are less likely to be instantly lethal.");
		CheckboxSave("Enable distance falloff", &s->advanced.enableDistanceFalloff,
			"When ON, headshot chance linearly decreases between the full-chance distance "
			"and the zero-chance distance. Point-blank is always full chance.");
		SliderFloatSave("Full chance distance (units)", &s->advanced.distanceFullChanceUnits, 100.0f, 10000.0f,
			"Distance in game units below which full chance applies. 3000 units is "
			"roughly 50 meters / 150 feet.",
			"%.0f");
		SliderFloatSave("Zero chance distance (units)", &s->advanced.distanceZeroChanceUnits, 1000.0f, 50000.0f,
			"Distance beyond which headshot instakill chance drops to 0%%. 15000 units is "
			"roughly 210 meters / 700 feet.",
			"%.0f");

		DrawSaveStatus();
	}

	void __stdcall RenderChances()
	{
		auto* s = Settings::GetSingleton();

		PageHeader("Instakill chances",
			"Per-target-category base chance (in %) that a confirmed headshot triggers "
			"the instant-kill damage application. The chance is then modulated by caliber "
			"vs. armor (see the 'Caliber Mods' page) and by helmet AR (see 'Helmets'). "
			"Set to 100 to always instakill, set to 0 to disable a category entirely.");

		SectionHeader("Per-category instakill chance (base %)",
			"Base success chance before caliber/armor modifiers are applied.");
		SliderFloatSave("Humanoid",         &s->chances.humanoid,        0.0f, 100.0f,
			"Humans, ghouls, and other bipedal humanoid enemies (Raiders, Gunners, BoS, "
			"etc.). Default 100 means a head hit with any qualifying caliber kills outright "
			"unless armor downgrades it. Gen-3 Synths have their own category below.");
		SliderFloatSave("Feral ghoul (race patterns)", &s->chances.feralGhoul, 0.0f, 100.0f,
			"Humanoids whose TESRace EditorID matches a substring from the Feral ghoul race "
			"patterns field (same menu page as the race blacklist). Default pattern 'FeralGhoul' "
			"matches FeralGhoulRace, glowing variants, etc. Instakill uses this base chance instead "
			"of Humanoid, ignores helmet AR on the roll, and uses the Feral ghoul caliber row "
			"(Caliber Mods; defaults 1.0 for pistol through large rifle).");
		SliderFloatSave("Small creature",   &s->chances.smallCreature,   0.0f, 100.0f,
			"Bloatflies, Bloodbugs, Stingwings, Molerats, dogs, Yao Guai, Vicious Dogs, etc. "
			"Most are 100 by default since head hits should easily one-shot them.");
		SliderFloatSave("Armored creature", &s->chances.armoredCreature, 0.0f, 100.0f,
			"Radscorpions and Mirelurks/Mirelurk Hunters. These have shells/plates so we "
			"only count head hits in the unarmored 'face' region (geometric check outside "
			"VATS, automatic in VATS).");
		SliderFloatSave("Large creature",   &s->chances.largeCreature,   0.0f, 100.0f,
			"Big creatures with thick skulls -- Deathclaws, Bears (Yao Guai), Mirelurk Queens, "
			"Behemoths. Lower chance because realistically you shouldn't one-shot a Deathclaw "
			"with a 9mm. Only Rifle/Large Rifle calibers proceed here by default.");
		SliderFloatSave("Super mutant",     &s->chances.superMutant,     0.0f, 100.0f,
			"Super Mutants and Nightkin. They have augmented physiology so the chance is "
			"lower than a regular humanoid; combine with the caliber multipliers in 'Caliber Mods'.");
		SliderFloatSave("Deathclaw (override)", &s->chances.deathclaw,    0.0f, 100.0f,
			"Specific override for Deathclaws (and variants). Takes precedence over the "
			"'Large creature' value when matched. Set to 0 to make Deathclaws immune to "
			"instakills via this mod.");
		SliderFloatSave("Mirelurk Queen (override)", &s->chances.mirelurkQueen, 0.0f, 100.0f,
			"Specific override for Mirelurk Queens. Takes precedence over the 'Large creature' "
			"value when matched.");
		SliderFloatSave("Synth (all gens)", &s->chances.synth,            0.0f, 100.0f,
			"All Synths (Gen-1, Gen-2, Gen-3). They have reinforced synthetic frames/skulls "
			"so the base chance is lower. Combined with the synth caliber multipliers on "
			"'Caliber Mods', only large rifle rounds can instakill by default.\n\n"
			"Gen-3 synths can wear helmets (knockoff applies normally). Gen-1/Gen-2 are "
			"mechanical but still share the same caliber vulnerability rules.");

		ImGuiMCP::Spacing();
		SectionHeader("Armor scaling (sigmoid curve)",
			"Uses the formula: chance_scale = 1 / (1 + AR / halfAR).\n"
			"At AR = 0 the chance is full; at AR = halfAR the chance drops to 50%%.\n"
			"Vanilla non-PA helmets range from 2-35 ballistic AR.");
		SliderFloatSave("Ballistic half-AR", &s->armorScaling.ballisticHalfAR, 1.0f, 100.0f,
			"Ballistic AR at which headshot chance drops to 50%%. Vanilla Combat Helmet is "
			"10 AR, so with halfAR=18 it drops chance to ~64%%. Wastelander Heavy (35 AR) "
			"drops it to ~34%%.",
			"%.0f");
		SliderFloatSave("Energy half-AR", &s->armorScaling.energyHalfAR, 1.0f, 500.0f,
			"Energy AR at which headshot chance drops to 50%% for energy weapons. PA helmets "
			"have 150 energy AR; most non-PA helmets have 0-15.",
			"%.0f");
		SliderFloatSave("Cross-resist factor", &s->armorScaling.crossResistFactor, 0.0f, 1.0f,
			"When an energy weapon hits, this fraction of the helmet's ballistic AR is "
			"added to the effective AR. 0.3 means a helmet with 10 ballistic + 0 energy "
			"still contributes 3 points of effective AR against energy weapons.",
			"%.2f");

		DrawSaveStatus();
	}

	void __stdcall RenderCaliberMods()
	{
		auto* s = Settings::GetSingleton();

		PageHeader("Caliber modifiers",
			"Multipliers applied to the base instakill chance from the 'Chances' page, "
			"based on what kind of caliber the round is and what the target is wearing.\n\n"
			"Each value is a chance multiplier:\n"
			"  0.0 = always fail (no instakill possible for that combination)\n"
			"  1.0 = use base chance unchanged\n"
			"  2.0 = double the base chance (capped at 100%)\n\n"
			"Calibers come from the 'Ammo Browser' page. The category 'Armored Humanoid' "
			"only applies when the target is wearing a helmet with non-trivial AR.");

		SectionHeader("Caliber multiplier vs Armored Humanoid (helmet has AR)",
			"Used when the head-armor's AR exceeds the threshold on the 'Chances' page. "
			"Default lets pistols/shotguns through at reduced chance, rifles unchanged, "
			"large rifles boosted.");
		SliderFloatSave("Pistol",      &s->caliberMods.pistolVsArmored,     0.0f, 2.0f,
			"Pistol-caliber rounds vs. an armored humanoid head. Default 0.0 = pistol "
			"shots cannot instakill a helmeted humanoid (they have to knock the helmet off first).");
		SliderFloatSave("Shotgun",     &s->caliberMods.shotgunVsArmored,    0.0f, 2.0f,
			"Shotgun shells vs. an armored humanoid head. Default 0.0 = pellets are "
			"individually low-energy and can't punch through the helmet.");
		SliderFloatSave("Rifle",       &s->caliberMods.rifleVsArmored,      0.0f, 2.0f,
			"Standard rifle rounds (.45/.5.56/.7.62) vs. an armored humanoid head. "
			"Default 1.0 = base chance applies.");
		SliderFloatSave("Large rifle", &s->caliberMods.largeRifleVsArmored, 0.0f, 2.0f,
			"Anti-materiel / .308 / .50 BMG vs. an armored humanoid head. Default 1.0+ = "
			"these go right through; the headgear barely matters.");

		ImGuiMCP::Spacing();
		SectionHeader("Caliber multiplier vs Feral ghoul (race pattern match)",
			"Only applies when the actor is classified as Humanoid AND the race EditorID "
			"matches a feral ghoul pattern (default substring FeralGhoul). Helmet AR does not "
			"reduce instakill chance for these targets; these multipliers replace the normal "
			"armored / PA buckets. Default 1.0 for all four tiers = any pistol/rifle caliber "
			"can contribute at full weight (Excluded ammo types stay at 0).");
		SliderFloatSave("Pistol##FG",      &s->caliberMods.pistolVsFeralGhoul,     0.0f, 2.0f,
			"Pistol-caliber vs. feral ghoul race match.");
		SliderFloatSave("Shotgun##FG",     &s->caliberMods.shotgunVsFeralGhoul,    0.0f, 2.0f,
			"Shotgun vs. feral ghoul race match.");
		SliderFloatSave("Rifle##FG",       &s->caliberMods.rifleVsFeralGhoul,      0.0f, 2.0f,
			"Standard rifle round vs. feral ghoul race match.");
		SliderFloatSave("Large rifle##FG", &s->caliberMods.largeRifleVsFeralGhoul, 0.0f, 2.0f,
			"Large / anti-materiel rifle vs. feral ghoul race match.");

		ImGuiMCP::Spacing();
		SectionHeader("Caliber multiplier vs Large / SuperMutant",
			"Applied when the target is a Super Mutant, Behemoth, Deathclaw, Bear, "
			"Mirelurk Queen, etc. Default lets only rifles through at meaningful chance.");
		SliderFloatSave("Pistol##L",      &s->caliberMods.pistolVsLarge,     0.0f, 2.0f,
			"Pistol-caliber vs. large/super mutant target. Default 0.0 = a 10mm cannot "
			"one-shot a Deathclaw via headshot.");
		SliderFloatSave("Shotgun##L",     &s->caliberMods.shotgunVsLarge,    0.0f, 2.0f,
			"Shotgun pellets vs. large/super mutant target. Default 0.0 = same reason.");
		SliderFloatSave("Rifle##L",       &s->caliberMods.rifleVsLarge,      0.0f, 2.0f,
			"Standard rifle round vs. large/super mutant target. Default 1.0 = base chance.");
		SliderFloatSave("Large rifle##L", &s->caliberMods.largeRifleVsLarge, 0.0f, 2.0f,
			"Anti-materiel rifle vs. large/super mutant target. Default 1.5+ = boosted chance.");

		ImGuiMCP::Spacing();
		SectionHeader("Caliber multiplier vs Synth (all gens)",
			"Applied when the target is any Synth (Gen-1, Gen-2, or Gen-3). Synths have "
			"tough synthetic frames; only large rifle rounds can reliably penetrate by "
			"default. Gen-3 synths CAN wear helmets (helmet knockoff applies normally).");
		SliderFloatSave("Pistol##S",      &s->caliberMods.pistolVsSynth,     0.0f, 2.0f,
			"Pistol-caliber vs. Synth. Default 0.0 = pistols cannot headshot-kill a Gen-3 Synth.");
		SliderFloatSave("Shotgun##S",     &s->caliberMods.shotgunVsSynth,    0.0f, 2.0f,
			"Shotgun vs. Synth. Default 0.0 = buckshot cannot penetrate synth skull.");
		SliderFloatSave("Rifle##S",       &s->caliberMods.rifleVsSynth,      0.0f, 2.0f,
			"Standard rifle round vs. Synth. Default 0.0 = regular rifle rounds are "
			"insufficient against synthetic bone plating.");
		SliderFloatSave("Large rifle##S", &s->caliberMods.largeRifleVsSynth, 0.0f, 2.0f,
			"Anti-materiel rifle vs. Synth. Default 1.0 = only these have the energy "
			"to destroy a synth's cranial component in one shot.");

		ImGuiMCP::Spacing();
		SectionHeader("Caliber multiplier vs Power Armor",
			"Applied when the headshot lands on a Power Armor frame's helmet. PA helmets "
			"are extremely tough; only rifles and especially large rifles have a meaningful chance.");
		SliderFloatSave("Pistol##PA",      &s->caliberMods.pistolVsPA,     0.0f, 2.0f,
			"Pistol-caliber vs. PA helmet. Default 0.0 = no chance.");
		SliderFloatSave("Shotgun##PA",     &s->caliberMods.shotgunVsPA,    0.0f, 2.0f,
			"Shotgun vs. PA helmet. Default 0.0 = no chance.");
		SliderFloatSave("Rifle##PA",       &s->caliberMods.rifleVsPA,      0.0f, 2.0f,
			"Standard rifle round vs. PA helmet. Default low value -- can chip away at the "
			"helmet but rarely instakills.");
		SliderFloatSave("Large rifle##PA", &s->caliberMods.largeRifleVsPA, 0.0f, 2.0f,
			"Anti-materiel rifle vs. PA helmet. Default ~1.0 = full chance because that's "
			"what these rounds are for.");

		DrawSaveStatus();
	}

	// =========================================================================
	// Caliber Rules overview -- shows a read-friendly matrix of what kills what
	// =========================================================================
	void __stdcall RenderCaliberRules()
	{
		auto* s = Settings::GetSingleton();

		PageHeader("Caliber rules overview",
			"Quick-reference table showing which calibers can instakill which actor category.\n\n"
			"A green checkmark means the caliber multiplier is > 0 (instakill possible).\n"
			"A red X means the multiplier is 0 (instakill impossible for that combination).\n"
			"The number in parentheses is the multiplier applied to the base chance.\n\n"
			"Edit these values on the 'Caliber Mods' page. Base chances per category are "
			"configured on the 'Chances' page.");

		struct Row {
			const char* name;
			float baseChance;
			float pistol;
			float shotgun;
			float rifle;
			float largeRifle;
		};

		Row rows[] = {
			{ "Humanoid (bare)",      s->chances.humanoid,        1.0f, 1.0f, 1.0f, 1.0f },
			{ "Humanoid (helmet)",    s->chances.humanoid,
				s->caliberMods.pistolVsArmored, s->caliberMods.shotgunVsArmored,
				s->caliberMods.rifleVsArmored,  s->caliberMods.largeRifleVsArmored },
			{ "Feral ghoul (pattern)", s->chances.feralGhoul,
				s->caliberMods.pistolVsFeralGhoul, s->caliberMods.shotgunVsFeralGhoul,
				s->caliberMods.rifleVsFeralGhoul,  s->caliberMods.largeRifleVsFeralGhoul },
			{ "Synth (all gens)",     s->chances.synth,
				s->caliberMods.pistolVsSynth, s->caliberMods.shotgunVsSynth,
				s->caliberMods.rifleVsSynth,  s->caliberMods.largeRifleVsSynth },
			{ "Super Mutant",         s->chances.superMutant,
				s->caliberMods.pistolVsLarge, s->caliberMods.shotgunVsLarge,
				s->caliberMods.rifleVsLarge,  s->caliberMods.largeRifleVsLarge },
			{ "Large Creature",       s->chances.largeCreature,
				s->caliberMods.pistolVsLarge, s->caliberMods.shotgunVsLarge,
				s->caliberMods.rifleVsLarge,  s->caliberMods.largeRifleVsLarge },
			{ "Deathclaw",            s->chances.deathclaw,
				s->caliberMods.pistolVsLarge, s->caliberMods.shotgunVsLarge,
				s->caliberMods.rifleVsLarge,  s->caliberMods.largeRifleVsLarge },
			{ "Mirelurk Queen",       s->chances.mirelurkQueen,
				s->caliberMods.pistolVsLarge, s->caliberMods.shotgunVsLarge,
				s->caliberMods.rifleVsLarge,  s->caliberMods.largeRifleVsLarge },
			{ "Armored Creature",     s->chances.armoredCreature,
				s->caliberMods.pistolVsArmored, s->caliberMods.shotgunVsArmored,
				s->caliberMods.rifleVsArmored,  s->caliberMods.largeRifleVsArmored },
			{ "Small Creature",       s->chances.smallCreature, 1.0f, 1.0f, 1.0f, 1.0f },
			{ "Power Armor",          s->chances.humanoid,
				s->caliberMods.pistolVsPA, s->caliberMods.shotgunVsPA,
				s->caliberMods.rifleVsPA,  s->caliberMods.largeRifleVsPA },
		};

		const char* calHeaders[] = { "Pistol", "Shotgun", "Rifle", "Large Rifle" };

		if (ImGuiMCP::BeginTable("##CaliberRulesTable", 6,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {

			ImGuiMCP::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 150.0f);
			ImGuiMCP::TableSetupColumn("Base %",   ImGuiTableColumnFlags_WidthFixed, 60.0f);
			for (int i = 0; i < 4; ++i) {
				ImGuiMCP::TableSetupColumn(calHeaders[i], ImGuiTableColumnFlags_WidthFixed, 90.0f);
			}
			ImGuiMCP::TableHeadersRow();

			for (auto& row : rows) {
				ImGuiMCP::TableNextRow();
				ImGuiMCP::TableNextColumn();
				ImGuiMCP::Text("%s", row.name);
				ImGuiMCP::TableNextColumn();
				ImGuiMCP::Text("%.0f%%", row.baseChance);

				float muls[] = { row.pistol, row.shotgun, row.rifle, row.largeRifle };
				for (int i = 0; i < 4; ++i) {
					ImGuiMCP::TableNextColumn();
					float effective = row.baseChance * muls[i];
					if (effective > 100.0f) effective = 100.0f;
					if (muls[i] <= 0.0f) {
						ImGuiMCP::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), "X (0)");
					} else {
						ImGuiMCP::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f),
							"%.0f%% (x%.1f)", effective, muls[i]);
					}
				}
			}
			ImGuiMCP::EndTable();
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::TextWrapped(
			"Legend: Each cell shows the effective instakill chance = base * multiplier "
			"(capped at 100%%). The multiplier (in parentheses) is editable on 'Caliber Mods'.\n\n"
			"'Robot' category (turrets, Protectrons, Sentry Bots, etc.) is immune by default "
			"(blocked by keyword, not shown here).");
	}

	void __stdcall RenderHelmet()
	{
		auto* s = Settings::GetSingleton();

		PageHeader("Helmets",
			"Helmet knockoff from gunshots, melee, and gun bash; drop physics; protection "
			"for you and followers; and options to spot a helmet after it hits the ground.");

		SectionHeader("Gunshot knockoff",
			"When a bullet headshot fails to instakill because the helmet stopped the round, "
			"the mod can unequip the helmet so a follow-up shot hits bare skin. "
			"Shotgun uses a pellet merge window (see Drop physics).");
		CheckboxSave("Enable gunshot helmet knockoff", &s->helmet.enableHelmetKnockoff,
			"Master switch for ranged knockoff. If OFF, gun headshots never strip helmets.\n"
			"This toggle is also required for melee / gun-bash knockoff (see below).\n"
			"Instakill chance is still evaluated separately when a round connects.");

		SliderFloatSave("Knockoff chance -- pistol",       &s->helmet.knockoffChancePistol,    0.0f, 100.0f,
			"Chance per pistol-caliber head hit that the helmet pops off. Pistols are the "
			"only way to depart a Combat Helmet without a rifle by default.");
		SliderFloatSave("Knockoff chance -- shotgun",      &s->helmet.knockoffChanceShotgun,   0.0f, 100.0f,
			"Chance per shotgun blast (not per pellet -- pellets within the group window "
			"under Drop physics count as one blast).");
		SliderFloatSave("Knockoff chance -- rifle",        &s->helmet.knockoffChanceRifle,     0.0f, 100.0f,
			"Chance per rifle/large-rifle head hit on a regular helmet. Usually high since "
			"rifles deliver enough impulse to defeat the strap.");
		SliderFloatSave("PA knockoff chance -- rifle",     &s->helmet.paKnockoffChanceRifle,   0.0f, 100.0f,
			"Chance per rifle head hit on a Power Armor helmet. Should be low; PA helmets "
			"are bolted to the frame, only sustained fire really shakes them loose.");
		SliderFloatSave("PA knockoff chance -- large rifle", &s->helmet.paKnockoffChanceLargeRifle, 0.0f, 100.0f,
			"Chance per anti-materiel head hit on a Power Armor helmet. Higher than a "
			"normal rifle because the round actually carries enough energy to peel it.");

		SectionHeader("Melee & gun bash knockoff",
			"Melee head hits and firearm bashes can knock helmets off. These never instakill "
			"through this mod. PA helmets are immune (bolted to the frame). "
			"Player knockoff multiplier and master toggles in Player helmet apply here too.");
		ImGuiMCP::TextWrapped(
			"Melee and bash require \"Enable gunshot helmet knockoff\" (above) to be ON "
			"— the plugin uses that as the shared master gate.");
		CheckboxSave("Enable melee / bash knockoff", &s->melee.enableMeleeKnockoff,
			"If ON, melee head hits and qualifying gun bashes can knock off helmets using "
			"the chances below.\n"
			"If OFF, the mod ignores melee and bash for knockoff.");
		SliderFloatSave("Melee -- medium weapon (%)", &s->melee.meleeKnockoffChanceMedium, 0.0f, 100.0f,
			"Per head hit: one-handed melee (machete, combat knife, pipe wrench, 1H bat, etc.). "
			"Default 10%: lighter swings.",
			"%.0f");
		SliderFloatSave("Melee -- large weapon (%)", &s->melee.meleeKnockoffChanceLarge, 0.0f, 100.0f,
			"Per head hit: two-handed melee (super sledge, 2H bat, sledgehammer, Grognak's Axe, etc.). "
			"Default 20%: heavier swings.",
			"%.0f");
		CheckboxSave("Gun bash can knock off helmets", &s->melee.gunBashKnockoff,
			"If ON, bashing with a non-pistol firearm counts for knockoff. Pistol-caliber "
			"weapons and excluded ammo types are filtered out.");
		SliderFloatSave("Gun bash knockoff (%)", &s->melee.gunBashKnockoffChance, 0.0f, 100.0f,
			"Per qualifying gun-bash head hit. Default 15%: between medium and large melee.",
			"%.0f");

		SectionHeader("Drop physics",
			"How the helmet object moves when it detaches, plus shotgun pellet grouping.");
		SliderFloatSave("Drop spawn height",    &s->helmet.dropSpawnHeight,    0.0f, 80.0f,
			"Height above the head node (in game units) where the helmet reference spawns. "
			"A small offset clears the skull geometry so the Havok impulse doesn't clip. "
			"~20 units is a good default; increase if the helmet clips into the head on spawn.");
		SliderFloatSave("Fly vs. incoming shot", &s->helmet.dropFlyAgainstShot, 0.0f, 1.0f,
			"Blends the impulse direction between along the shot (0) and opposite to the shot (1). "
			"1.0 = helmet flies back from the impact (default). 0.0 = along bullet travel. "
			"Use values in between if you want a flatter peel-off.",
			"%.2f");
		SliderFloatSave("Fly upward lift", &s->helmet.dropFlyUpLift, 0.0f, 2.0f,
			"Extra upward component mixed into the fly direction before the impulse is applied. "
			"Higher values make the helmet arc up more. Default 0.4.",
			"%.2f");
		SliderFloatSave("Drop linear impulse",  &s->helmet.dropLinearImpulse,  0.0f, 2000.0f,
			"Havok impulse magnitude applied to the dropped helmet via ApplyHavokImpulse. "
			"Controls how fast/far the helmet flies. Typical range 200-1000; "
			"0 = no impulse (drops straight down from spawn position).");
		SliderFloatSave("Drop angular impulse", &s->helmet.dropAngularImpulse, 0.0f, 5.0f,
			"How much spin is added to the dropped helmet. Larger = more tumbling. "
			"Usually 1.0-2.0 looks right.");
		SliderFloatSave("Shotgun pellet group window (s)", &s->helmet.shotgunPelletGroupWindowSec, 0.01f, 1.0f,
			"Hits from the same shotgun blast arrive as N independent hit events (one per "
			"pellet). To avoid checking knockoff N times, hits within this many seconds of "
			"the first pellet are merged into a single 'shot' for knockoff purposes.",
			"%.3f");

		ImGuiMCP::Spacing();
		SectionHeader("Player helmet",
			"Controls whether the player's own helmet can be knocked off when they "
			"take a headshot, and how the mod reacts when it happens.");
		CheckboxSave("Helmet protects player from instakill", &s->playerHelmetProtection,
			"When ON: a headshot can only instakill the player while they are bare-headed. "
			"If the player is wearing any head armor the instakill is blocked entirely -- "
			"the enemy must knock the helmet off first before a follow-up headshot can kill.\n\n"
			"Works with the knockoff system: if helmet knockoff is also enabled, the enemy's "
			"first shot can strip the helmet; a second shot to the head then kills.\n\n"
			"Off by default (default behavior: headshot kill chance is reduced by armor "
			"rating but not blocked outright).");
		SliderFloatSave("Min head AR to block instakill", &s->playerInstakillMinAR, 0.0f, 100.0f,
			"Only active when the toggle above is OFF.\n\n"
			"If the player's head armor ballistic AR is at or above this value, a headshot "
			"instakill is blocked -- the shot is redirected into a knockoff attempt instead.\n\n"
			"This gives partial protection based on armor quality rather than a binary "
			"helmet-on/off check. Examples:\n"
			"   0  = disabled (player is always instakillable regardless of AR)\n"
			"   8  = default: light helmets (AR 8+) block instakill; bare head does not\n"
			"  25  = only medium/heavy helmets protect\n"
			"  50  = only thick military/PA-tier helmets protect\n\n"
			"Has no effect when the toggle above is ON (all head armor protects unconditionally).",
			"%.0f");
		CheckboxSave("Player helmet can be knocked off", &s->playerHelmetKnockoffEnabled,
			"Master toggle for player helmet knockoff. If OFF, the player is completely "
			"immune to knockoff from gunshots, melee, and bash regardless of the chance "
			"sliders. Use this if you want NPCs to lose helmets but you to keep yours.");
		SliderFloatSave("Player knockoff chance multiplier", &s->playerHelmetKnockoffMult, 0.0f, 3.0f,
			"Multiplier applied to knockoff chance when the player is the target "
			"(gunshot caliber chances and melee / bash chances). Examples:\n"
			"  1.0 = same chance as NPCs (default)\n"
			"  0.5 = half the chance\n"
			"  2.0 = double the chance\n"
			"  0.0 = never (same as disabling the toggle above)\n"
			"Only used when the toggle above is ON.",
			"%.2f");
		CheckboxSave("Notify on knockoff", &s->helmetNotifyPlayer,
			"Show a HUD message (e.g. 'Combat Helmet knocked off!') when your helmet "
			"gets knocked off so you know what happened.");
		CheckboxSave("Auto-reequip on pickup", &s->helmetAutoReequip,
			"When you walk up to your dropped helmet and pick it up, automatically "
			"re-equip it so you don't have to open the Pip-Boy inventory. "
			"Only applies to the most recently knocked-off helmet.");

		ImGuiMCP::Spacing();
		SectionHeader("Follower helmet",
			"Followers don't pick things up on their own, so a knocked-off helmet "
			"would be lost. This option auto-restores it after combat using the EXACT "
			"same item reference -- legendary effects, attached mods, and condition "
			"are all preserved (no duplication).");
		CheckboxSave("Restore follower helmet after combat", &s->followerHelmetRestore,
			"When combat ends, put knocked-off helmets back into the owning follower's "
			"inventory and re-equip them. Also runs on save-load so a mid-combat save "
			"doesn't leave your companion bare-headed forever.\n"
			"Edge case: if the dropped helmet ref gets cleaned up by the engine (cell "
			"unload / long distance), the helmet is lost -- we do NOT create a duplicate.");

		ImGuiMCP::Spacing();
		SectionHeader("Dropped helmet -- highlight",
			"Make your dropped helmet easy to spot on the ground.\n"
			"Next-Gen game update (PostNG): applies a TESEffectShader glow.\n"
			"Older versions (PreNG): falls back to a visibility blink on the helmet mesh.");
		CheckboxSave("Enable highlight", &s->helmetShaderEnabled,
			"When the game exposes ApplyEffectShader, uses your TESEffectShader (overlay-style glow). "
			"If that API is unavailable or the form is missing, falls back to a slow visibility pulse on the mesh "
			"(not as nice as a shader, but easier to spot than nothing).");
		{
			static char shaderBuf[128]{};
			static bool shaderBufInit = false;
			if (!shaderBufInit) {
				strncpy_s(shaderBuf, s->helmetShaderEditorID.c_str(), _TRUNCATE);
				shaderBufInit = true;
			}
			ImGuiMCP::SetNextItemWidth(240.0f);
			if (ImGuiMCP::InputText("Shader EditorID", shaderBuf, sizeof(shaderBuf), 0)) {
				s->helmetShaderEditorID = shaderBuf;
				MarkDirty();
			}
			Tooltip("EditorID of the TESEffectShader form to apply. "
				"Vanilla examples: WorkshopHighlightShader, ScrapperHighlightShader.");
		}
		SliderFloatSave("Shader duration (s)", &s->helmetShaderDuration, -1.0f, 300.0f,
			"How many seconds the shader/blink stays active. Also controls how long "
			"the HUD tracker below stays active. -1 = indefinite (until pickup).",
			"%.0f");

		ImGuiMCP::Spacing();
		SectionHeader("Dropped helmet -- point light",
			"Place an actual light source at the helmet's location so it illuminates "
			"the surrounding area. Useful in dark environments where shaders alone "
			"are hard to see. Disabled by default.");
		CheckboxSave("Place point light at helmet", &s->helmetLightEnabled,
			"When ON, spawns a TESObjectLIGH at the helmet's position using the game's reference "
			"creator (same path the CK uses). If that fails, a console PlaceAtMe fallback is tried.\n\n"
			"Requires a valid TESObjectLIGH EditorID in your load order (see below). "
			"The light is cleaned up when you pick up the helmet (when the mod recorded its ref).");
		{
			static char lightEdidBuf[128]{};
			static bool lightEdidBufInit = false;
			if (!lightEdidBufInit) {
				strncpy_s(lightEdidBuf, s->helmetLightEditorID.c_str(), _TRUNCATE);
				lightEdidBufInit = true;
			}
			ImGuiMCP::SetNextItemWidth(240.0f);
			if (ImGuiMCP::InputText("Light EditorID", lightEdidBuf, sizeof(lightEdidBuf), 0)) {
				s->helmetLightEditorID = lightEdidBuf;
				MarkDirty();
			}
			Tooltip("EditorID of a TESObjectLIGH form to place at the helmet. Must exist "
				"in your load order.\n\n"
				"Vanilla examples (no mods required):\n"
				"  DefaultLight01NSFill -- soft omnidirectional fill light\n"
				"  SpotLight01NS -- brighter spot\n"
				"  WorkshopLightBulb01 -- workshop light bulb\n\n"
				"'NS' suffix = No Shadow (unshadowed, better performance).");
		}

		ImGuiMCP::Spacing();
		SectionHeader("Dropped helmet -- HUD tracker",
			"When your helmet is knocked off, a periodic HUD message shows which "
			"direction to look and how far away it is. Works even through walls, "
			"vegetation, and at long range -- never lose your helmet again.");
		CheckboxSave("Enable HUD tracker", &s->helmetTrackerEnabled,
			"Show a compass/distance HUD message pointing you toward your dropped helmet "
			"(arrow ^ > >> v << < vs. your facing, plus meters).\n"
			"The first ping fires as soon as the helmet lands; later pings use the interval below. "
			"A new knockoff always starts a fresh tracker.\n\n"
			"Stops when you pick up the helmet or the shader duration expires.");
		SliderFloatSave("Tracker update interval (s)", &s->helmetTrackerIntervalSec, 0.5f, 10.0f,
			"How often follow-up HUD messages refresh (the first ping is always immediate).\n"
			"  0.5s = very chatty\n"
			"  3s = default\n"
			"  5s+ = subtle reminder",
			"%.1f");

		DrawSaveStatus();
	}

	// =====================================================================
	// Ammo Browser  (redesigned)
	//
	// Layout:
	//   [page description]
	//   [Filter box] [View mode radio] [Category filter combo] [Only overrides toggle]
	//   [Re-scan button]
	//   [legend / counts]
	//   [grouped or flat list of entries; each row has its own caliber dropdown
	//    and a [reset] button if it's an override]
	// =====================================================================

	// Color palette per caliber. Designed to be readable on the ImGui dark theme.
	static ImGuiMCP::ImVec4 CaliberColor(Caliber a_c)
	{
		switch (a_c) {
		case Caliber::Excluded:    return { 0.55f, 0.55f, 0.55f, 1.0f };  // grey
		case Caliber::Pistol:      return { 0.70f, 0.85f, 1.00f, 1.0f };  // light blue
		case Caliber::Shotgun:     return { 1.00f, 0.78f, 0.30f, 1.0f };  // amber
		case Caliber::Rifle:       return { 0.55f, 0.95f, 0.55f, 1.0f };  // green
		case Caliber::LargeRifle:  return { 1.00f, 0.55f, 0.55f, 1.0f };  // red/orange
		}
		return { 1, 1, 1, 1 };
	}

	// Render one row of the ammo browser: colored caliber tag + EditorID +
	// inline caliber dropdown + reset button + tooltip with full context.
	// Returns true if the user changed the caliber on this row this frame.
	static bool DrawAmmoRow(const AmmoEntry& a_e)
	{
		ImGuiMCP::PushID(static_cast<int>(a_e.formID));

		// 1) Coloured [TAG] indicator showing current caliber.
		const auto col = CaliberColor(a_e.caliber);
		ImGuiMCP::TextColored(col, "[%-11s]", CaliberDisplay(a_e.caliber));
		ImGuiMCP::SameLine();

		// 2) EditorID + damage display + override marker.
		const char* edid = a_e.editorID.empty() ? "<no EditorID>" : a_e.editorID.c_str();
		if (a_e.autoClassified) {
			ImGuiMCP::Text("%s (dmg:%.0f)", edid, a_e.ammoDamage);
		} else {
			ImGuiMCP::TextColored({ 1.00f, 0.85f, 0.20f, 1.0f }, "%s (dmg:%.0f) [override]", edid, a_e.ammoDamage);
		}

		// 3) Hover tooltip with all the diagnostic context.
		if (ImGuiMCP::IsItemHovered(0)) {
			ImGuiMCP::SetTooltip(
				"%s\n"
				"Plugin:      %s\n"
				"FormID:      0x%08X\n"
				"Damage type: %s\n"
				"Ammo damage: %.1f\n"
				"Why:         %s\n\n"
				"Use the dropdown on the right to override the caliber.\n"
				"Click [reset] to revert to the auto classification.",
				edid,
				a_e.sourcePlugin.empty() ? "<unknown>" : a_e.sourcePlugin.c_str(),
				a_e.formID,
				(a_e.damageType == DamageType::Energy ? "Energy" : "Ballistic"),
				a_e.ammoDamage,
				a_e.classificationReason.empty() ? "<no reason recorded>" : a_e.classificationReason.c_str());
		}

		// 4) Inline caliber dropdown on the right side of the row.
		ImGuiMCP::SameLine();
		// Width chosen to fully render the longest label ("Large Rifle")
		// plus the dropdown arrow at the default ImGui font size.
		ImGuiMCP::SetNextItemWidth(170.0f);
		const char* calibers[] = { "Excluded", "Pistol", "Shotgun", "Rifle", "Large Rifle" };
		int currentIdx = std::clamp(static_cast<int>(a_e.caliber), 0, 4);
		bool changed = false;
		if (ImGuiMCP::BeginCombo("##caliber", calibers[currentIdx], 0)) {
			for (int i = 0; i < 5; ++i) {
				const bool selected = (i == currentIdx);
				if (ImGuiMCP::Selectable(calibers[i], selected, 0, { 0, 0 })) {
					if (i != currentIdx) {
						const auto key = AmmoClassifier::MakeOverrideKey(a_e.sourcePlugin, a_e.editorID);
						AmmoClassifier::GetSingleton()->SetOverride(
							key, static_cast<Caliber>(i), a_e.damageType);
						State::saveStatusMsg = "Saved override: " + a_e.editorID + " -> " + calibers[i];
						State::saveStatusTimer = 3.0f;
						changed = true;
					}
				}
			}
			ImGuiMCP::EndCombo();
		}
		Tooltip("Pick the caliber category for this round.\n"
			"Ammo overrides save to disk IMMEDIATELY (this is intentional -- it's the\n"
			"only setting that auto-saves; everything else needs the Save button at\n"
			"the top of the page).");

		// 5) [reset] button only on overridden rows.
		if (!a_e.autoClassified) {
			ImGuiMCP::SameLine();
			if (ImGuiMCP::SmallButton("reset")) {
				const auto key = AmmoClassifier::MakeOverrideKey(a_e.sourcePlugin, a_e.editorID);
				AmmoClassifier::GetSingleton()->ClearOverride(key);
				State::saveStatusMsg = "Cleared override on " + a_e.editorID;
				State::saveStatusTimer = 3.0f;
				changed = true;
			}
			Tooltip("Remove the override and let the JSON + heuristics classify this ammo "
				"automatically again.");
		}

		ImGuiMCP::PopID();
		return changed;
	}

	// Apply a caliber to every entry in a list (used by per-plugin bulk buttons).
	static void BulkApplyCaliber(const std::vector<AmmoEntry>& a_entries, Caliber a_c)
	{
		auto* cls = AmmoClassifier::GetSingleton();
		for (const auto& e : a_entries) {
			const auto key = AmmoClassifier::MakeOverrideKey(e.sourcePlugin, e.editorID);
			cls->SetOverride(key, a_c, e.damageType);
		}
		State::saveStatusMsg = "Set " + std::to_string(a_entries.size()) +
			" ammo -> " + std::string(CaliberDisplay(a_c));
		State::saveStatusTimer = 3.0f;
	}

	static void BulkClear(const std::vector<AmmoEntry>& a_entries)
	{
		auto* cls = AmmoClassifier::GetSingleton();
		for (const auto& e : a_entries) {
			if (e.autoClassified) continue;
			const auto key = AmmoClassifier::MakeOverrideKey(e.sourcePlugin, e.editorID);
			cls->ClearOverride(key);
		}
		State::saveStatusMsg = "Cleared overrides in group";
		State::saveStatusTimer = 3.0f;
	}

	void __stdcall RenderAmmoBrowser()
	{
		PageHeader("Ammo Browser",
			"Every ammo type currently loaded in your game with the caliber category the "
			"mod assigned to it. Categories drive which kill chance + caliber multiplier "
			"is used (see the Caliber Mods page).\n\n"
			"  Excluded    -- never triggers an instakill (grenades, missiles, fuel, etc.)\n"
			"  Pistol      -- low-power handgun rounds (.22, .380, 9mm, .357, .44, etc.)\n"
			"  Shotgun     -- multi-pellet shells (any gauge, .410 bore)\n"
			"  Rifle       -- intermediate rifle (.45/5.56/.300 BLK/7.62x39)\n"
			"  Large Rifle -- full-power / anti-materiel (.308/.30-06/.50 BMG/.338)\n\n"
			"How to use:\n"
			"  - Hover any row for full classification details (plugin, formID, damage, why).\n"
			"  - Pick a different caliber in the dropdown on the right of each row to override it.\n"
			"  - Click [reset] (only shown on overridden rows) to revert to auto.\n"
			"  - Per-plugin bulk actions are inside each collapsible section header.");

		// Ammo classification runs on a background thread once
		// kGameDataReady fires, so at the main menu (or for the first ~1s
		// after game data loads) the cache is still empty. Show a friendly
		// status instead of an empty list -- this lets the menu open at
		// the main menu cleanly without any "broken" appearance.
		auto* cls = AmmoClassifier::GetSingleton();
		if (!cls->IsInitialized()) {
			ImGuiMCP::TextDisabled(
				"Scanning ammo... (this happens once on background thread "
				"after the game's data files finish loading).\n"
				"At the main menu before a save is loaded the scan may not "
				"have run yet -- load a save and come back to see all ammo.");
			DrawSaveStatus();
			return;
		}

		// ---- Toolbar ---------------------------------------------------
		ImGuiMCP::SetNextItemWidth(280.0f);
		ImGuiMCP::InputText("Search", State::ammoFilter, sizeof(State::ammoFilter));
		Tooltip("Live substring filter on EditorID OR plugin name (case-insensitive). "
			"Try: 'Munitions', '308', '.esl', 'Fallout4', 'shotgun'.");

		ImGuiMCP::SameLine();
		ImGuiMCP::SetNextItemWidth(150.0f);
		const char* catFilterItems[] = {
			"All categories", "Only Excluded", "Only Pistol",
			"Only Shotgun",   "Only Rifle",    "Only Large Rifle"
		};
		int catFilterIdx = State::ammoCategoryFilter + 1;  // -1..4 -> 0..5
		if (ImGuiMCP::Combo("Show", &catFilterIdx, catFilterItems, 6)) {
			State::ammoCategoryFilter = catFilterIdx - 1;
		}
		Tooltip("Restrict the list to a single caliber category. Useful for auditing 'why "
			"are these all Excluded?' or finding all Pistol entries to bulk-bump them up.");

		ImGuiMCP::SameLine();
		CheckboxSave("Only my overrides", &State::ammoOnlyShowOverrides,
			"When ON, hide every auto-classified row and show only entries you've manually "
			"changed. Quick way to review your overrides or clear them all.");

		ImGuiMCP::SetNextItemWidth(150.0f);
		const char* viewModeItems[] = { "Group by plugin", "Flat alphabetical" };
		ImGuiMCP::Combo("View", &State::ammoViewMode, viewModeItems, 2);
		Tooltip("'Group by plugin' shows collapsible sections per .esp/.esm/.esl with bulk "
			"actions per group. 'Flat alphabetical' is one big sorted list.");

		ImGuiMCP::SameLine();
		if (ImGuiMCP::Button("Re-scan all ammo")) {
			AmmoClassifier::GetSingleton()->Recategorize();
			State::saveStatusMsg = "Ammo re-scanned.";
			State::saveStatusTimer = 3.0f;
		}
		Tooltip("Re-runs JSON + heuristic classification across every TESAmmo form. Run this "
			"after editing ammo_calibers.json or after manually editing the INI.");

		ImGuiMCP::SameLine();
		if (ImGuiMCP::Button("Reset ALL overrides")) {
			auto* s = Settings::GetSingleton();
			s->ammoOverrides.clear();
			s->Save();
			AmmoClassifier::GetSingleton()->Recategorize();
			State::saveStatusMsg = "All ammo overrides cleared.";
			State::saveStatusTimer = 3.0f;
		}
		Tooltip("DANGER: removes every override you've set. Use 'Only my overrides' first if "
			"you want to review what's about to be deleted.");

		ImGuiMCP::Separator();

		// ---- Snapshot + filter -----------------------------------------
		auto entries = AmmoClassifier::GetSingleton()->GetAllEntries();

		std::string filterLower = State::ammoFilter;
		std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		auto matchFilter = [&](const AmmoEntry& e) {
			if (State::ammoCategoryFilter >= 0 &&
				static_cast<int>(e.caliber) != State::ammoCategoryFilter) {
				return false;
			}
			if (State::ammoOnlyShowOverrides && e.autoClassified) return false;
			if (filterLower.empty()) return true;
			std::string edidL = e.editorID;
			std::string plgL  = e.sourcePlugin;
			std::transform(edidL.begin(), edidL.end(), edidL.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::transform(plgL.begin(), plgL.end(), plgL.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return edidL.find(filterLower) != std::string::npos ||
				   plgL.find(filterLower)  != std::string::npos;
		};

		std::vector<AmmoEntry> visible;
		visible.reserve(entries.size());
		for (const auto& e : entries) {
			if (matchFilter(e)) visible.push_back(e);
		}

		// ---- Summary line ----------------------------------------------
		std::array<std::size_t, 5> counts{ 0, 0, 0, 0, 0 };
		std::size_t overrides = 0;
		for (const auto& e : entries) {
			const auto i = static_cast<std::size_t>(e.caliber);
			if (i < counts.size()) counts[i]++;
			if (!e.autoClassified) overrides++;
		}
		ImGuiMCP::Text(
			"%zu total ammo  |  %zu excluded  %zu pistol  %zu shotgun  %zu rifle  %zu large rifle  |  %zu user overrides  |  %zu shown",
			entries.size(),
			counts[static_cast<std::size_t>(Caliber::Excluded)],
			counts[static_cast<std::size_t>(Caliber::Pistol)],
			counts[static_cast<std::size_t>(Caliber::Shotgun)],
			counts[static_cast<std::size_t>(Caliber::Rifle)],
			counts[static_cast<std::size_t>(Caliber::LargeRifle)],
			overrides,
			visible.size());
		ImGuiMCP::Separator();

		if (visible.empty()) {
			ImGuiMCP::TextDisabled("No ammo matches the current filter.");
			DrawSaveStatus();
			return;
		}

		// ---- Render either grouped-by-plugin or flat -------------------
		if (State::ammoViewMode == 0) {
			// Build per-plugin groups in display order. visible is already
			// sorted by (plugin, edid) thanks to GetAllEntries().
			std::string currentPlugin;
			std::vector<AmmoEntry> currentGroup;

			auto flush_group = [&]() {
				if (currentGroup.empty()) return;
				const std::string label = std::format("{}  ({} ammo)##{}",
					currentPlugin.empty() ? "<unknown plugin>" : currentPlugin,
					currentGroup.size(),
					currentPlugin);
				if (ImGuiMCP::CollapsingHeader(label.c_str(), ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGuiMCP::PushID(currentPlugin.c_str());
					// Bulk actions row
					if (ImGuiMCP::SmallButton("All -> Pistol"))     BulkApplyCaliber(currentGroup, Caliber::Pistol);
					ImGuiMCP::SameLine();
					if (ImGuiMCP::SmallButton("All -> Shotgun"))    BulkApplyCaliber(currentGroup, Caliber::Shotgun);
					ImGuiMCP::SameLine();
					if (ImGuiMCP::SmallButton("All -> Rifle"))      BulkApplyCaliber(currentGroup, Caliber::Rifle);
					ImGuiMCP::SameLine();
					if (ImGuiMCP::SmallButton("All -> LargeRifle")) BulkApplyCaliber(currentGroup, Caliber::LargeRifle);
					ImGuiMCP::SameLine();
					if (ImGuiMCP::SmallButton("All -> Excluded"))   BulkApplyCaliber(currentGroup, Caliber::Excluded);
					ImGuiMCP::SameLine();
					if (ImGuiMCP::SmallButton("Reset group"))       BulkClear(currentGroup);
					Tooltip("Removes overrides on every ammo from this plugin. The auto "
						"classification (JSON / heuristic) takes over again.");
					ImGuiMCP::Separator();
					for (const auto& e : currentGroup) {
						DrawAmmoRow(e);
					}
					ImGuiMCP::PopID();
				}
				currentGroup.clear();
			};

			for (const auto& e : visible) {
				if (e.sourcePlugin != currentPlugin) {
					flush_group();
					currentPlugin = e.sourcePlugin;
				}
				currentGroup.push_back(e);
			}
			flush_group();
		} else {
			// Flat mode -- one big list. Cap at 500 rows for performance.
			constexpr std::size_t kMaxFlat = 500;
			for (std::size_t i = 0; i < visible.size() && i < kMaxFlat; ++i) {
				DrawAmmoRow(visible[i]);
			}
			if (visible.size() > kMaxFlat) {
				ImGuiMCP::Spacing();
				ImGuiMCP::TextDisabled("(%zu more entries hidden -- refine the filter or use grouped view)",
					visible.size() - kMaxFlat);
			}
		}

		DrawSaveStatus();
	}

	void __stdcall RenderRaceBlacklist()
	{
		auto* s = Settings::GetSingleton();

		PageHeader("Race blacklist & immune keywords",
			"Two ways to make actors immune to the mod:\n"
			"  - Race blacklist: matched against the actor's TESRace EditorID\n"
			"  - Immune keywords: matched against the actor's BGSKeywords (works for any race)\n\n"
			"Both lists are comma-separated and case-insensitive substrings; partial matches "
			"work, so 'Ghoul' will catch GhoulRace, FeralGhoulRace, GlowingFeralGhoulRace, etc.\n\n"
			"Feral ghoul race patterns (below) are separate: they opt matched Humanoids into "
			"special instakill rules (see Chances / Caliber Mods), not immunity.");

		SectionHeader("Race blacklist",
			"Per-race opt-out by TESRace EditorID. Useful for sparing specific NPCs (Codsworth, "
			"specific Ghoul variants you want to remain durable, etc.).");
		ImGuiMCP::Text("Example: HumanRace,GhoulRace,DogmeatRace");

		if (ImGuiMCP::InputText("Race blacklist", State::raceBlacklistInput, sizeof(State::raceBlacklistInput))) {
			s->raceBlocklist = SplitCommaList(State::raceBlacklistInput);
			MarkDirty();
		}
		Tooltip("Comma-separated list. Each entry is matched as a case-insensitive substring "
			"of the actor's race EditorID. Press Enter or click outside to commit. "
			"Click 'Save settings' at the top to persist.");

		ImGuiMCP::Separator();
		SectionHeader("Feral ghoul race patterns",
			"Comma-separated substrings matched against TESRace EditorID. If the actor's final "
			"category is Humanoid and any pattern matches, they use the Feral ghoul base chance "
			"and caliber row; helmet ballistic/energy AR is ignored for the instakill chance "
			"(PA bucket rules do not apply). Add patterns for modded races in the CK "
			"'Humanoid - Feral Ghoul' family (e.g. MyModFeralGhoulRace).");
		ImGuiMCP::Text("Example: FeralGhoul, GlowingOne");
		if (ImGuiMCP::InputText("Feral ghoul race patterns", State::feralGhoulRacePatternsInput,
				sizeof(State::feralGhoulRacePatternsInput))) {
			s->feralGhoulRacePatterns = SplitCommaList(State::feralGhoulRacePatternsInput);
			if (s->feralGhoulRacePatterns.empty()) {
				s->feralGhoulRacePatterns.push_back("FeralGhoul");
				std::strncpy(State::feralGhoulRacePatternsInput, "FeralGhoul",
					sizeof(State::feralGhoulRacePatternsInput) - 1);
				State::feralGhoulRacePatternsInput[sizeof(State::feralGhoulRacePatternsInput) - 1] = '\0';
			}
			MarkDirty();
		}
		Tooltip("Comma-separated substrings of race EditorID. Default single entry FeralGhoul "
			"covers vanilla feral races. Clearing all entries resets to FeralGhoul.");

		ImGuiMCP::Separator();
		SectionHeader("Immune keywords",
			"Per-keyword opt-out by BGSKeyword EditorID. Default includes ActorTypeRobot so "
			"Sentry Bots, Mr. Handys, Protectrons, and Assaultrons cannot be headshot-killed "
			"(they don't have organic heads).");
		ImGuiMCP::Text("Comma-separated keyword EditorIDs (default: ActorTypeRobot).");
		if (ImGuiMCP::InputText("Immune keywords", State::keywordImmuneInput, sizeof(State::keywordImmuneInput))) {
			s->keywordImmuneList = SplitCommaList(State::keywordImmuneInput);
			MarkDirty();
		}
		Tooltip("Comma-separated list. Each entry is a case-insensitive BGSKeyword EditorID "
			"to match (any keyword on the actor causes immunity). Common ones: ActorTypeRobot, "
			"ActorTypeTurret, ActorTypeSynth. Click 'Save settings' at the top to persist.");

		DrawSaveStatus();
	}

	void __stdcall RenderKillImpulse()
	{
		auto* s = Settings::GetSingleton();

		PageHeader("Kill impulse (head snap)",
			"Rotate the head/neck bone on headshot to create a visible 'snap-back' "
			"effect. Inserts an invisible joint so the rotation layers on top of "
			"animations without forcing ragdoll.");

		CheckboxSave("Enable head snap", &s->killImpulse.enabled,
			"Master toggle. Applies a rotational impulse to the head bone on headshot "
			"for a 'snap-back' visual. If OFF, no head snap is ever applied.");
		CheckboxSave("Apply on all headshots", &s->killImpulse.applyOnAllHeadshots,
			"When ON: head snap fires on EVERY headshot to a non-player humanoid "
			"(including feral ghouls; not synths), including shots that don't kill "
			"and shots on already-dead actors.\n"
			"When OFF: head snap only fires on instakill headshots (same categories).\n"
			"Useful for tactical feedback when a shot connects but the helmet absorbs it.");
		SliderFloatSave("Snap angle (degrees)", &s->killImpulse.magnitude, 5.0f, 90.0f,
			"How far the head rotates backward on snap. 25 gives a dramatic but "
			"believable snap. Higher values look more cinematic / extreme.",
			"%.0f");
		SliderFloatSave("Upward bias", &s->killImpulse.upwardBias, 0.0f, 1.0f,
			"Tilts the rotation axis upward so the head snaps back AND up, instead "
			"of purely backward.\n"
			"  0.0 = pure backward snap\n"
			"  0.3 = slight upward tilt (default, looks natural)\n"
			"  1.0 = maximum upward tilt",
			"%.2f");
		SliderFloatSave("Decay duration (seconds)", &s->killImpulse.decayDuration, 0.2f, 5.0f,
			"How long the head stays rotated before smoothly interpolating back to "
			"neutral. Higher = the snap pose lingers longer before releasing.\n"
			"  0.5s = quick snap, animation takes over fast\n"
			"  1.5s = natural (default)\n"
			"  3.0s+ = head stays rotated for most of the death animation",
			"%.1f");

		DrawSaveStatus();
	}

	void __stdcall RenderLegendary()
	{
		auto* s = Settings::GetSingleton();

		PageHeader("Legendary mutation handling",
			"Vanilla legendary enemies trigger a 'mutation' (instant heal-to-full + buff) "
			"when their HP drops below 50%. If the mod's massive headshot damage lands "
			"DURING the mutation animation, the engine ends up confused and the legendary "
			"can either die early (skipping the mutation entirely, no legendary loot drop) "
			"or live forever. This page suppresses headshots while we detect a mutation "
			"is in progress so vanilla mechanics work cleanly.");

		SectionHeader("Settings");
		CheckboxSave("Respect legendary mutation",  &s->legendary.respectLegendaryMutation,
			"If ON (default), the mod will detect the mutation MGEF/health snapshot and "
			"refuse to apply kill damage during the cooldown. Turn OFF if you want headshots "
			"to ignore the mutation entirely (you may lose legendary loot drops).");
		SliderFloatSave("Cooldown (seconds)",       &s->legendary.legendaryCooldownSeconds, 0.0f, 30.0f,
			"How long after a detected mutation we keep suppressing headshots. The mutation "
			"animation is ~3-5 seconds; the default leaves a buffer so the engine has time to "
			"finish processing the legendary heal and you don't 'instakill' on the very next frame.",
			"%.1f");
		SliderFloatSave("Mutation health gain threshold (ratio)",
			&s->legendary.mutationHealthRatioThreshold, 0.0f, 1.0f,
			"How much the actor's health must jump (as a fraction of max HP) within one "
			"frame to count as a 'mutation heal'. Default 0.50 catches the standard +50% "
			"mutation. Lower this if mods reduce the heal amount; raise it if other "
			"effects cause false positives.",
			"%.2f");

		DrawSaveStatus();
	}

	// Helper: draw all controls for one visual effect layer.
	static void DrawEffectLayer(VisualEffectLayer* a_layer,
		char* a_imodBuf, std::size_t a_imodBufSize,
		const char* a_imodTooltip,
		float a_durMin, float a_durMax,
		const char* a_durTooltip,
		const char* a_strTooltip)
	{
		CheckboxSave("Enabled", &a_layer->enabled,
			"Toggle this visual layer on or off independently.");

		if (ImGuiMCP::InputText("IMod EditorID", a_imodBuf, static_cast<int>(a_imodBufSize))) {
			a_layer->imodEditorID = a_imodBuf;
			MarkDirty();
		}
		Tooltip(a_imodTooltip);

		SliderFloatSave("Duration (seconds)", &a_layer->duration, a_durMin, a_durMax,
			a_durTooltip, "%.1f");
		SliderFloatSave("Strength", &a_layer->strength, 0.0f, 2.0f,
			a_strTooltip, "%.2f");
	}

	void __stdcall RenderPlayerFeedback()
	{
		auto* s = Settings::GetSingleton();

		PageHeader("Player headshot feedback",
			"When the player gets shot in the head, up to three visual effects and a "
			"tinnitus sound fire simultaneously to give visceral feedback. Each layer "
			"uses a vanilla ImageSpaceModifier (browse Fallout4.esm in xEdit for the "
			"full list) and can be toggled, retimed, and rebalanced independently.\n\n"
			"A cooldown timer prevents rapid fire from spamming effects. IMod EditorID "
			"changes require a game restart to take effect.");

		SectionHeader("Master toggle");
		CheckboxSave("Enable headshot feedback", &s->playerFeedback.enableFeedback,
			"Master switch. When OFF, no feedback effects fire on the player from "
			"headshots. Individual layer toggles below are still respected when ON.");
		SliderFloatSave("Feedback cooldown (seconds)", &s->playerFeedback.feedbackCooldown, 0.5f, 30.0f,
			"Minimum time between feedback triggers. Prevents shotgun pellets "
			"and automatic weapons from spamming effects every frame.",
			"%.1f");

		ImGuiMCP::Spacing();
		SectionHeader("Tinnitus sound",
			"Plays a custom WAV file on player headshot. "
			"Place the file in Data/F4SE/Plugins/HeadshotsKillF4SE/.");
		CheckboxSave("Enable tinnitus sound", &s->playerFeedback.enableTinnitusSound,
			"Play the tinnitus WAV. Turn OFF to have only visual effects.");
		SliderFloatSave("Tinnitus volume", &s->playerFeedback.tinnitusSoundVolume, 0.0f, 1.0f,
			"Volume scale for the WAV. 1.0 = original file volume. "
			"Plays through Windows audio (not the in-game mixer).",
			"%.2f");
		if (ImGuiMCP::InputText("WAV filename", State::tinnitusSoundFileInput,
			sizeof(State::tinnitusSoundFileInput))) {
			s->playerFeedback.tinnitusSoundFile = State::tinnitusSoundFileInput;
			MarkDirty();
		}
		Tooltip("Filename in Data/F4SE/Plugins/HeadshotsKillF4SE/. "
			"Default: headshot_tinnitus.wav. Changes need save + restart.");

		ImGuiMCP::Spacing();
		SectionHeader("Audio muffle (low-pass filter)",
			"Applies a low-pass filter to game audio on headshot, simulating the "
			"muffled hearing effect of a close gunshot. Audio gradually fades back "
			"to normal over the configured duration.");
		CheckboxSave("Enable audio muffle", &s->playerFeedback.enableAudioMuffle,
			"Muffle game SFX and voice audio when the player gets headshotted. "
			"Creates a realistic 'ears ringing' sensation alongside the tinnitus WAV.");
		SliderFloatSave("Muffle intensity", &s->playerFeedback.muffleIntensity, 0.0f, 1.0f,
			"How much the audio is muffled at peak. 0.0 = complete silence, "
			"0.15 = heavy muffle (default), 0.5 = moderate, 1.0 = no effect.",
			"%.2f");
		SliderFloatSave("Muffle fade duration (seconds)", &s->playerFeedback.muffleFadeDuration, 0.5f, 15.0f,
			"How long it takes for audio to fade from muffled back to normal. "
			"Longer values give a more dramatic 'recovering hearing' effect.",
			"%.1f");

		// ── Layer 1: Concussion / double vision ─────────────────────────
		ImGuiMCP::Spacing();
		if (ImGuiMCP::CollapsingHeader("Layer 1: Concussion (double vision)",
			ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGuiMCP::PushID("concussion");
			ImGuiMCP::TextWrapped(
				"Brief double-vision wobble that simulates the jarring impact of a "
				"bullet hitting your helmet/skull. Fires and fades quickly.");
			DrawEffectLayer(&s->playerFeedback.concussion,
				State::concussionIModInput, sizeof(State::concussionIModInput),
				"Default: ImageSpaceConcussion. Other options: ExplosionGrenadeImod, "
				"GunBashImod, GetHit.",
				0.1f, 10.0f,
				"How long the concussion wobble persists. Default 1.5s -- "
				"short enough to not annoy, long enough to feel.",
				"Blend intensity. 1.0 = full vanilla effect, 0.5 = half, "
				"2.0 = exaggerated. Keep at 1.0 unless the default is too intense.");
			ImGuiMCP::PopID();
		}

		// ── Layer 2: Impact flash / whiteout ────────────────────────────
		ImGuiMCP::Spacing();
		if (ImGuiMCP::CollapsingHeader("Layer 2: Impact flash (whiteout)",
			ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGuiMCP::PushID("impactflash");
			ImGuiMCP::TextWrapped(
				"A very brief bright flash that simulates the moment of impact -- "
				"like a flashbang effect compressed into a fraction of a second. "
				"WhiteoutImodToNormal fades from white back to normal automatically.");
			DrawEffectLayer(&s->playerFeedback.impactFlash,
				State::impactFlashIModInput, sizeof(State::impactFlashIModInput),
				"Default: WhiteoutImodToNormal. Alternatives: WhiteoutImod (no auto-fade), "
				"ExplosionFlash (orange tint), FadetoWhiteImod.",
				0.1f, 5.0f,
				"Keep very short (0.2-0.5s). Longer durations blind the player.",
				"How intense the flash is. Default 0.5 -- punchy but not blinding. "
				"1.0 = full white screen, 0.3 = gentle pulse.");
			ImGuiMCP::PopID();
		}

		// ── Layer 3: Greyscale / recovery ───────────────────────────────
		ImGuiMCP::Spacing();
		if (ImGuiMCP::CollapsingHeader("Layer 3: Greyscale (recovery fade)",
			ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGuiMCP::PushID("greyscale");
			ImGuiMCP::TextWrapped(
				"A lingering desaturation / greyscale that represents the player "
				"recovering their senses after the impact. Lasts the longest of "
				"the three layers and fades out gracefully.");
			DrawEffectLayer(&s->playerFeedback.greyscale,
				State::greyscaleIModInput, sizeof(State::greyscaleIModInput),
				"Default: zd_ScopeTargetingRecon (greyscale overlay). Alternatives: "
				"ChemFadeOutImod (drug-style desaturation), RadImodSatFadeOut.",
				0.5f, 30.0f,
				"How long the greyscale lingers. Default 2.5s -- gives a 'recovering' "
				"feel without overstaying its welcome.",
				"Blend intensity. 1.0 = full desaturation as defined by the IMod. "
				"0.5 = muted effect, 2.0 = exaggerated.");
			ImGuiMCP::PopID();
		}

		DrawSaveStatus();
	}

	// =====================================================================
	// Debug: Actor Inspector
	// =====================================================================

	static const char* kCategoryLabels[] = {
		"None (0)",
		"Humanoid (1)",
		"SuperMutant (2)",
		"SmallCreature (3)",
		"ArmoredCreature (4)",
		"LargeCreature (5)",
		"Robot (6)",
		"Synth (7)",
	};
	static constexpr int kCategoryCount = 8;

	void __stdcall RenderDebug()
	{
		PageHeader("HeadshotsKill F4SE -- Debug",
			"Select an actor in the console (click on them), then open this page "
			"to see how the mod classifies them. You can also add their race to a "
			"category override from here.");

		auto* s = Settings::GetSingleton();

		SectionHeader("Console-selected actor",
			"Open the game console (~), click on an actor, then view this page.");

		// Resolve the console-selected ref to an Actor
		RE::Actor* actor = nullptr;
		RE::ObjectRefHandle pickHandle = RE::Console::GetCurrentPickREFR();
		if (pickHandle) {
			RE::NiPointer<RE::TESObjectREFR> refPtr;
			if (RE::BSPointerHandleManagerInterface<RE::TESObjectREFR>::GetSmartPointer(pickHandle, refPtr)) {
				if (refPtr && refPtr->GetFormType() == RE::ENUM_FORM_ID::kACHR) {
					actor = static_cast<RE::Actor*>(refPtr.get());
				}
			}
		}

		if (!actor) {
			ImGuiMCP::TextWrapped("No actor selected. Open the console and click on an actor.");
			ImGuiMCP::Spacing();
		} else {
			const std::uint32_t formID = actor->GetFormID();
			const char* displayName = nullptr;
			const char* npcEdid = nullptr;
			if (auto* npc = actor->GetNPC()) {
				displayName = npc->GetFullName();
				npcEdid = npc->GetFormEditorID();
			}

			// Race info
			const char* raceEdid = nullptr;
			const char* raceName = nullptr;
			std::uint32_t raceFormID = 0;
			if (actor->race) {
				raceEdid = actor->race->GetFormEditorID();
				raceName = actor->race->GetFullName();
				raceFormID = actor->race->GetFormID();
			}

			// Classify
			auto info = ActorClassifier::GetSingleton()->Classify(actor);
			const char* catName = ActorCategoryName(info.category);

			ImGuiMCP::Text("Form ID:   0x%08X", formID);
			ImGuiMCP::Text("Name:      %s", (displayName && *displayName) ? displayName : "<none>");
			ImGuiMCP::Text("NPC EDID:  %s", (npcEdid && *npcEdid) ? npcEdid : "<none>");
			ImGuiMCP::Text("Race EDID: %s", (raceEdid && *raceEdid) ? raceEdid : "<none>");
			ImGuiMCP::Text("Race Name: %s", (raceName && *raceName) ? raceName : "<none>");
			ImGuiMCP::Text("Race ID:   0x%08X", raceFormID);
			ImGuiMCP::Spacing();
			ImGuiMCP::Text("Category:  %s", catName);
			ImGuiMCP::Text("Feral ghoul (race pattern): %s", info.isFeralGhoul ? "Yes" : "No");
			ImGuiMCP::Text("Level:     %u", info.level);
			ImGuiMCP::Text("Legendary: %s", info.isLegendary ? "Yes" : "No");
			ImGuiMCP::Text("Power Armor: %s", info.isInPowerArmor ? "Yes" : "No");
			ImGuiMCP::Text("Player:    %s", info.isPlayer ? "Yes" : "No");
			ImGuiMCP::Text("Follower:  %s", info.isFollower ? "Yes" : "No");
			ImGuiMCP::Text("Deathclaw: %s", info.isDeathclaw ? "Yes" : "No");
			ImGuiMCP::Text("Queen:     %s", info.isMirelurkQueen ? "Yes" : "No");

			// Keywords
			ImGuiMCP::Spacing();
			SectionHeader("Keywords (ActorType*)");
			const auto& kw = ActorClassifier::GetSingleton()->Keywords();
			struct KWEntry { const char* label; RE::BGSKeyword* kw; };
			KWEntry kwList[] = {
				{ "ActorTypeNPC",           kw.actorTypeNPC },
				{ "ActorTypeGhoul",         kw.actorTypeGhoul },
				{ "ActorTypeSuperMutant",   kw.actorTypeSuperMutant },
				{ "ActorTypeRobot",         kw.actorTypeRobot },
				{ "ActorTypeSynth",         kw.actorTypeSynth },
				{ "ActorTypeAnimal",        kw.actorTypeAnimal },
				{ "ActorTypeDeathclaw",     kw.actorTypeDeathclaw },
				{ "ActorTypeMirelurk",      kw.actorTypeMirelurk },
				{ "ActorTypeMirelurkQueen", kw.actorTypeMirelurkQueen },
				{ "ActorTypeRadScorpion",   kw.actorTypeRadscorpion },
				{ "ActorTypeYaoGuai",       kw.actorTypeYaoGuai },
				{ "ActorTypeBehemoth",      kw.actorTypeBehemoth },
				{ "ActorTypeFogCrawler",    kw.actorTypeFogCrawler },
				{ "encTypeLegendary",       kw.encTypeLegendary },
			};
			for (const auto& e : kwList) {
				if (!e.kw) continue;
				bool has = actor->HasKeyword(e.kw, nullptr);
				if (has) {
					ImGuiMCP::Text("  [X] %s", e.label);
				}
			}
			bool anyKW = false;
			for (const auto& e : kwList) {
				if (e.kw && actor->HasKeyword(e.kw, nullptr)) { anyKW = true; break; }
			}
			if (!anyKW) {
				ImGuiMCP::Text("  (none of the known ActorType keywords)");
			}

			// Check if this race already has an override
			ImGuiMCP::Spacing();
			SectionHeader("Add race category override",
				"If this actor's race is not classified correctly, you can add "
				"a race override here. The Race EditorID pattern will be matched "
				"as a case-insensitive substring.");

			bool hasExistingOverride = false;
			std::string existingPattern;
			int existingCat = 0;
			if (raceEdid && *raceEdid) {
				for (const auto& [pat, cat] : s->raceCategoryOverrides) {
					if (!pat.empty() && ContainsCaseInsensitive(raceEdid, pat)) {
						hasExistingOverride = true;
						existingPattern = pat;
						existingCat = cat;
						break;
					}
				}
			}

			if (hasExistingOverride) {
				int catIdx = existingCat;
				const char* catLabel = (catIdx >= 0 && catIdx < kCategoryCount) ? kCategoryLabels[catIdx] : "???";
				ImGuiMCP::Text("Existing override: '%s' -> %s", existingPattern.c_str(), catLabel);
				if (ImGuiMCP::Button("Remove override")) {
					s->raceCategoryOverrides.erase(existingPattern);
					s->Save();
					ActorClassifier::GetSingleton()->Init();
				}
			}

			static int selectedCategory = 5; // default to LargeCreature
			static char racePatternBuf[128] = "";
			static std::uint32_t lastInspectedFormID = 0;

			// Auto-fill the pattern buffer when a new actor is selected
			if (formID != lastInspectedFormID) {
				lastInspectedFormID = formID;
				if (raceEdid && *raceEdid) {
					std::strncpy(racePatternBuf, raceEdid, sizeof(racePatternBuf) - 1);
					racePatternBuf[sizeof(racePatternBuf) - 1] = '\0';
				} else {
					racePatternBuf[0] = '\0';
				}
			}

			ImGuiMCP::InputText("Race pattern", racePatternBuf, sizeof(racePatternBuf));
			Tooltip("Substring to match against the Race EditorID. Pre-filled with "
				"the selected actor's race. You can shorten it (e.g. 'YaoGuai' "
				"instead of the full 'CWYaoGuaiRace') to catch more variants.");

			if (ImGuiMCP::Combo("Target category", &selectedCategory, kCategoryLabels, kCategoryCount)) {
			}
			Tooltip("Which actor category this race should be classified as.");

			if (ImGuiMCP::Button("Add override")) {
				std::string pat(racePatternBuf);
				if (!pat.empty() && selectedCategory > 0 && selectedCategory < kCategoryCount) {
					s->raceCategoryOverrides[pat] = selectedCategory;
					s->Save();
					ActorClassifier::GetSingleton()->Init();
					racePatternBuf[0] = '\0';
				}
			}
			Tooltip("Saves the override to your INI and re-initializes the classifier.");
		}

		// Show all existing race overrides
		ImGuiMCP::Spacing();
		SectionHeader("All race category overrides",
			"All currently defined race -> category overrides. These are checked "
			"first in the classification pipeline, before keywords or name heuristics.");

		if (s->raceCategoryOverrides.empty()) {
			ImGuiMCP::Text("(none)");
		} else {
			int removeIdx = -1;
			int idx = 0;
			std::string removeKey;
			for (const auto& [pat, cat] : s->raceCategoryOverrides) {
				const char* catLabel = (cat >= 0 && cat < kCategoryCount) ? kCategoryLabels[cat] : "???";
				ImGuiMCP::Text("  '%s' -> %s", pat.c_str(), catLabel);
				ImGuiMCP::SameLine();
				char btnId[64];
				std::snprintf(btnId, sizeof(btnId), "Remove##rco%d", idx);
				if (ImGuiMCP::Button(btnId)) {
					removeKey = pat;
					removeIdx = idx;
				}
				++idx;
			}
			if (removeIdx >= 0 && !removeKey.empty()) {
				s->raceCategoryOverrides.erase(removeKey);
				s->Save();
				ActorClassifier::GetSingleton()->Init();
			}
		}
	}

	void __stdcall RenderAbout()
	{
		PageHeader("HeadshotsKill F4SE",
			"F4SE port of HeadshotsKill (originally an SKSE plugin). Applies large damage "
			"on headshots to score instakills, with rules based on caliber, target "
			"category, and headgear -- so a Deathclaw isn't getting one-shot by a 9mm "
			"and a humanoid in a Combat Helmet has to lose the helmet first.\n\n"
			"All settings live in HeadshotsKillF4SE.ini next to the DLL; the Ammo Browser "
			"and Race Blacklist write back to the INI as you edit them.");

		SectionHeader("File locations");
		ImGuiMCP::Text("INI:   Data/F4SE/Plugins/HeadshotsKillF4SE.ini");
		ImGuiMCP::Text("JSON:  Data/F4SE/Plugins/HeadshotsKillF4SE/ammo_calibers.json");
		ImGuiMCP::Text("Log:   My Games/Fallout4/F4SE/HeadshotsKillF4SE.log");

		ImGuiMCP::Spacing();
		SectionHeader("Tips");
		ImGuiMCP::TextWrapped(
			"- Hover any control to see what it does.\n"
			"- Enable 'Debug logging' on the General page to see per-hit and per-ammo decisions.\n"
			"- The Ammo Browser explains why each ammo got its category in the log when debug is on.\n"
			"- After installing a new ammo mod, click 'Re-scan all ammo' on the Ammo Browser page.");
	}
}

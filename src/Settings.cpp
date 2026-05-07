#include "Settings.h"

namespace HSK
{
	// =============================================================
	// String helpers
	// =============================================================
	std::vector<std::string> SplitCommaList(const std::string& a_str)
	{
		std::vector<std::string> out;
		std::string              cur;
		cur.reserve(32);
		for (char c : a_str) {
			if (c == ',' || c == ';') {
				while (!cur.empty() && std::isspace(static_cast<unsigned char>(cur.back()))) cur.pop_back();
				while (!cur.empty() && std::isspace(static_cast<unsigned char>(cur.front()))) cur.erase(cur.begin());
				if (!cur.empty()) out.push_back(std::move(cur));
				cur.clear();
			} else {
				cur.push_back(c);
			}
		}
		while (!cur.empty() && std::isspace(static_cast<unsigned char>(cur.back()))) cur.pop_back();
		while (!cur.empty() && std::isspace(static_cast<unsigned char>(cur.front()))) cur.erase(cur.begin());
		if (!cur.empty()) out.push_back(std::move(cur));
		return out;
	}

	std::string JoinCommaList(const std::vector<std::string>& a_list)
	{
		std::string out;
		for (std::size_t i = 0; i < a_list.size(); ++i) {
			if (i > 0) out.push_back(',');
			out.append(a_list[i]);
		}
		return out;
	}

	std::string CaliberToString(Caliber a_c)
	{
		switch (a_c) {
		case Caliber::Excluded:   return "excluded";
		case Caliber::Pistol:     return "pistol";
		case Caliber::Shotgun:    return "shotgun";
		case Caliber::Rifle:      return "rifle";
		case Caliber::LargeRifle: return "large_rifle";
		default:                  return "pistol";
		}
	}

	Caliber CaliberFromString(std::string_view a_s)
	{
		if (a_s == "excluded")    return Caliber::Excluded;
		if (a_s == "pistol")      return Caliber::Pistol;
		if (a_s == "shotgun")     return Caliber::Shotgun;
		if (a_s == "rifle")       return Caliber::Rifle;
		if (a_s == "large_rifle") return Caliber::LargeRifle;
		if (a_s == "largerifle")  return Caliber::LargeRifle;
		return Caliber::Pistol;
	}

	const char* CaliberDisplay(Caliber a_c)
	{
		switch (a_c) {
		case Caliber::Excluded:   return "Excluded";
		case Caliber::Pistol:     return "Pistol";
		case Caliber::Shotgun:    return "Shotgun";
		case Caliber::Rifle:      return "Rifle";
		case Caliber::LargeRifle: return "Large Rifle";
		default:                  return "Pistol";
		}
	}

	// =============================================================
	// Settings::Load
	// =============================================================
	void Settings::Load()
	{
		std::unique_lock lk(_mutex);

		CSimpleIniA ini;
		ini.SetUnicode();
		const auto rc = ini.LoadFile(kIniPath);

		// auto-create file with defaults if missing
		if (rc < 0) {
			lk.unlock();
			Save();
			return;
		}

		auto getB = [&](const char* sec, const char* key, bool def) {
			return ini.GetBoolValue(sec, key, def);
		};
		auto getF = [&](const char* sec, const char* key, float def) {
			return static_cast<float>(ini.GetDoubleValue(sec, key, def));
		};
		auto getI = [&](const char* sec, const char* key, int def) {
			return static_cast<int>(ini.GetLongValue(sec, key, def));
		};
		auto getS = [&](const char* sec, const char* key, const char* def) {
			return std::string(ini.GetValue(sec, key, def));
		};

		// [General]
		enabled      = getB("General", "bEnableMod", enabled);
		debugLogging = getB("General", "bEnableDebugLogging", debugLogging);
		killDamage   = getF("General", "fKillDamage", killDamage);

		// [Victims]
		applyToPlayerAndFollowers = getB("Victims", "bApplyToPlayerAndFollowers", applyToPlayerAndFollowers);
		twoShotWindowSeconds      = getF("Victims", "fTwoShotWindowSeconds", twoShotWindowSeconds);
		twoShotNearDeathRatio     = getF("Victims", "fTwoShotNearDeathRatio", twoShotNearDeathRatio);
		levelGapThreshold         = getI("Victims", "iLevelGapThreshold", levelGapThreshold);
		essentialMode             = getI("Victims", "iEssentialMode", essentialMode);

		// [Chances]
		chances.humanoid        = getF("Chances", "fHumanoid",        chances.humanoid);
		chances.feralGhoul      = getF("Chances", "fFeralGhoul",      chances.feralGhoul);
		chances.smallCreature   = getF("Chances", "fSmallCreature",   chances.smallCreature);
		chances.armoredCreature = getF("Chances", "fArmoredCreature", chances.armoredCreature);
		chances.largeCreature   = getF("Chances", "fLargeCreature",   chances.largeCreature);
		chances.superMutant     = getF("Chances", "fSuperMutant",     chances.superMutant);
		chances.deathclaw       = getF("Chances", "fDeathclaw",       chances.deathclaw);
		chances.mirelurkQueen   = getF("Chances", "fMirelurkQueen",   chances.mirelurkQueen);
		chances.synth           = getF("Chances", "fSynth",           chances.synth);

		// [CaliberModifiers]
		caliberMods.pistolVsArmored     = getF("CaliberModifiers", "fPistolVsArmored",     caliberMods.pistolVsArmored);
		caliberMods.shotgunVsArmored    = getF("CaliberModifiers", "fShotgunVsArmored",    caliberMods.shotgunVsArmored);
		caliberMods.rifleVsArmored      = getF("CaliberModifiers", "fRifleVsArmored",      caliberMods.rifleVsArmored);
		caliberMods.largeRifleVsArmored = getF("CaliberModifiers", "fLargeRifleVsArmored", caliberMods.largeRifleVsArmored);
		caliberMods.pistolVsLarge       = getF("CaliberModifiers", "fPistolVsLarge",       caliberMods.pistolVsLarge);
		caliberMods.shotgunVsLarge      = getF("CaliberModifiers", "fShotgunVsLarge",      caliberMods.shotgunVsLarge);
		caliberMods.rifleVsLarge        = getF("CaliberModifiers", "fRifleVsLarge",        caliberMods.rifleVsLarge);
		caliberMods.largeRifleVsLarge   = getF("CaliberModifiers", "fLargeRifleVsLarge",   caliberMods.largeRifleVsLarge);
		caliberMods.pistolVsSynth       = getF("CaliberModifiers", "fPistolVsSynth",       caliberMods.pistolVsSynth);
		caliberMods.shotgunVsSynth      = getF("CaliberModifiers", "fShotgunVsSynth",      caliberMods.shotgunVsSynth);
		caliberMods.rifleVsSynth        = getF("CaliberModifiers", "fRifleVsSynth",        caliberMods.rifleVsSynth);
		caliberMods.largeRifleVsSynth   = getF("CaliberModifiers", "fLargeRifleVsSynth",   caliberMods.largeRifleVsSynth);
		caliberMods.pistolVsPA          = getF("CaliberModifiers", "fPistolVsPA",          caliberMods.pistolVsPA);
		caliberMods.shotgunVsPA         = getF("CaliberModifiers", "fShotgunVsPA",         caliberMods.shotgunVsPA);
		caliberMods.rifleVsPA           = getF("CaliberModifiers", "fRifleVsPA",           caliberMods.rifleVsPA);
		caliberMods.largeRifleVsPA      = getF("CaliberModifiers", "fLargeRifleVsPA",      caliberMods.largeRifleVsPA);
		caliberMods.pistolVsFeralGhoul     = getF("CaliberModifiers", "fPistolVsFeralGhoul",     caliberMods.pistolVsFeralGhoul);
		caliberMods.shotgunVsFeralGhoul    = getF("CaliberModifiers", "fShotgunVsFeralGhoul",    caliberMods.shotgunVsFeralGhoul);
		caliberMods.rifleVsFeralGhoul      = getF("CaliberModifiers", "fRifleVsFeralGhoul",      caliberMods.rifleVsFeralGhoul);
		caliberMods.largeRifleVsFeralGhoul = getF("CaliberModifiers", "fLargeRifleVsFeralGhoul", caliberMods.largeRifleVsFeralGhoul);

		// [Helmet]
		helmet.enableHelmetKnockoff           = getB("Helmet", "bEnableHelmetKnockoff",          helmet.enableHelmetKnockoff);
		helmet.knockoffChancePistol           = getF("Helmet", "fKnockoffChancePistol",          helmet.knockoffChancePistol);
		helmet.knockoffChanceRifle            = getF("Helmet", "fKnockoffChanceRifle",           helmet.knockoffChanceRifle);
		helmet.knockoffChanceShotgun          = getF("Helmet", "fKnockoffChanceShotgun",         helmet.knockoffChanceShotgun);
		helmet.paKnockoffChanceRifle          = getF("Helmet", "fPAKnockoffChanceRifle",         helmet.paKnockoffChanceRifle);
		helmet.paKnockoffChanceLargeRifle     = getF("Helmet", "fPAKnockoffChanceLargeRifle",    helmet.paKnockoffChanceLargeRifle);
		helmet.dropLinearImpulse              = getF("Helmet", "fDropLinearImpulse",             helmet.dropLinearImpulse);
		helmet.dropAngularImpulse             = getF("Helmet", "fDropAngularImpulse",            helmet.dropAngularImpulse);
		helmet.dropSpawnHeight                = getF("Helmet", "fDropSpawnHeight",               helmet.dropSpawnHeight);
		helmet.dropFlyAgainstShot             = getF("Helmet", "fDropFlyAgainstShot",            helmet.dropFlyAgainstShot);
		helmet.dropFlyUpLift                  = getF("Helmet", "fDropFlyUpLift",                 helmet.dropFlyUpLift);
		helmet.shotgunPelletGroupWindowSec    = getF("Helmet", "fShotgunPelletGroupWindowSec",   helmet.shotgunPelletGroupWindowSec);

		// [Melee]
		melee.enableMeleeKnockoff      = getB("Melee", "bEnableMeleeKnockoff",      melee.enableMeleeKnockoff);
		melee.meleeKnockoffChanceMedium = getF("Melee", "fKnockoffChanceMedium",    melee.meleeKnockoffChanceMedium);
		melee.meleeKnockoffChanceLarge  = getF("Melee", "fKnockoffChanceLarge",     melee.meleeKnockoffChanceLarge);
		melee.gunBashKnockoff          = getB("Melee", "bGunBashKnockoff",          melee.gunBashKnockoff);
		melee.gunBashKnockoffChance    = getF("Melee", "fGunBashKnockoffChance",    melee.gunBashKnockoffChance);

		// [ArmorScaling]
		armorScaling.ballisticHalfAR     = getF("ArmorScaling", "fBallisticHalfAR",     armorScaling.ballisticHalfAR);
		armorScaling.energyHalfAR        = getF("ArmorScaling", "fEnergyHalfAR",        armorScaling.energyHalfAR);
		armorScaling.crossResistFactor   = getF("ArmorScaling", "fCrossResistFactor",   armorScaling.crossResistFactor);

		// [Advanced]
		advanced.ammoDamageReferenceDamage = getF("Advanced", "fAmmoDamageReferenceDamage", advanced.ammoDamageReferenceDamage);
		advanced.ammoDamageInfluence       = getF("Advanced", "fAmmoDamageInfluence",       advanced.ammoDamageInfluence);
		advanced.enableDistanceFalloff     = getB("Advanced", "bEnableDistanceFalloff",     advanced.enableDistanceFalloff);
		advanced.distanceFullChanceUnits   = getF("Advanced", "fDistanceFullChanceUnits",   advanced.distanceFullChanceUnits);
		advanced.distanceZeroChanceUnits   = getF("Advanced", "fDistanceZeroChanceUnits",   advanced.distanceZeroChanceUnits);
		advanced.sneakBonusMul             = getF("Advanced", "fSneakBonusMul",             advanced.sneakBonusMul);
		advanced.vatsRequiresCritical      = getB("Advanced", "bVATSRequiresCritical",     advanced.vatsRequiresCritical);
		advanced.criticalBonusChance       = getF("Advanced", "fCriticalBonusChance",      advanced.criticalBonusChance);

		// [PlayerFeedback]
		playerFeedback.enableFeedback          = getB("PlayerFeedback", "bEnableFeedback",          playerFeedback.enableFeedback);
		playerFeedback.enableTinnitusSound     = getB("PlayerFeedback", "bEnableTinnitusSound",     playerFeedback.enableTinnitusSound);
		playerFeedback.tinnitusSoundVolume     = getF("PlayerFeedback", "fTinnitusSoundVolume",     playerFeedback.tinnitusSoundVolume);
		playerFeedback.tinnitusSoundFile       = getS("PlayerFeedback", "sTinnitusSoundFile",       playerFeedback.tinnitusSoundFile.c_str());
		playerFeedback.enableAudioMuffle       = getB("PlayerFeedback", "bEnableAudioMuffle",       playerFeedback.enableAudioMuffle);
		playerFeedback.muffleIntensity         = getF("PlayerFeedback", "fMuffleIntensity",         playerFeedback.muffleIntensity);
		playerFeedback.muffleFadeDuration      = getF("PlayerFeedback", "fMuffleFadeDuration",      playerFeedback.muffleFadeDuration);
		playerFeedback.feedbackCooldown        = getF("PlayerFeedback", "fFeedbackCooldown",        playerFeedback.feedbackCooldown);

		// [FeedbackConcussion]
		playerFeedback.concussion.enabled       = getB("FeedbackConcussion", "bEnabled",       playerFeedback.concussion.enabled);
		playerFeedback.concussion.imodEditorID  = getS("FeedbackConcussion", "sIModEditorID",  playerFeedback.concussion.imodEditorID.c_str());
		playerFeedback.concussion.duration      = getF("FeedbackConcussion", "fDuration",      playerFeedback.concussion.duration);
		playerFeedback.concussion.strength      = getF("FeedbackConcussion", "fStrength",      playerFeedback.concussion.strength);

		// [FeedbackImpactFlash]
		playerFeedback.impactFlash.enabled      = getB("FeedbackImpactFlash", "bEnabled",      playerFeedback.impactFlash.enabled);
		playerFeedback.impactFlash.imodEditorID = getS("FeedbackImpactFlash", "sIModEditorID", playerFeedback.impactFlash.imodEditorID.c_str());
		playerFeedback.impactFlash.duration     = getF("FeedbackImpactFlash", "fDuration",     playerFeedback.impactFlash.duration);
		playerFeedback.impactFlash.strength     = getF("FeedbackImpactFlash", "fStrength",     playerFeedback.impactFlash.strength);

		// [FeedbackGreyscale]
		playerFeedback.greyscale.enabled        = getB("FeedbackGreyscale", "bEnabled",        playerFeedback.greyscale.enabled);
		playerFeedback.greyscale.imodEditorID   = getS("FeedbackGreyscale", "sIModEditorID",   playerFeedback.greyscale.imodEditorID.c_str());
		playerFeedback.greyscale.duration       = getF("FeedbackGreyscale", "fDuration",       playerFeedback.greyscale.duration);
		playerFeedback.greyscale.strength       = getF("FeedbackGreyscale", "fStrength",       playerFeedback.greyscale.strength);

		// [KillImpulse]
		killImpulse.enabled            = getB("KillImpulse", "bEnabled",            killImpulse.enabled);
		killImpulse.applyOnAllHeadshots = getB("KillImpulse", "bApplyOnAllHeadshots", killImpulse.applyOnAllHeadshots);
		killImpulse.magnitude          = getF("KillImpulse", "fMagnitude",          killImpulse.magnitude);
		killImpulse.upwardBias         = getF("KillImpulse", "fUpwardBias",         killImpulse.upwardBias);
		killImpulse.decayDuration      = getF("KillImpulse", "fDecayDuration",      killImpulse.decayDuration);

		// [Helmet] player extensions
		playerHelmetKnockoffEnabled = getB("Helmet", "bPlayerHelmetKnockoff",    playerHelmetKnockoffEnabled);
		playerHelmetKnockoffMult    = getF("Helmet", "fPlayerKnockoffChanceMult", playerHelmetKnockoffMult);
		playerHelmetProtection      = getB("Helmet", "bPlayerHelmetProtection",   playerHelmetProtection);
		playerInstakillMinAR        = getF("Helmet", "fPlayerInstakillMinAR",     playerInstakillMinAR);
		helmetNotifyPlayer = getB("Helmet", "bNotifyPlayerKnockoff", helmetNotifyPlayer);
		helmetAutoReequip  = getB("Helmet", "bAutoReequipPlayer",   helmetAutoReequip);
		helmetShaderEnabled   = getB("Helmet", "bShaderEnabled",      helmetShaderEnabled);
		helmetShaderEditorID  = getS("Helmet", "sShaderEditorID",     helmetShaderEditorID.c_str());
		helmetShaderDuration  = getF("Helmet", "fShaderDuration",     helmetShaderDuration);
		helmetLightEnabled      = getB("Helmet", "bHelmetLight",            helmetLightEnabled);
		helmetLightEditorID     = getS("Helmet", "sHelmetLightEditorID",   helmetLightEditorID.c_str());
		helmetTrackerEnabled    = getB("Helmet", "bHelmetTracker",         helmetTrackerEnabled);
		helmetTrackerIntervalSec = getF("Helmet", "fHelmetTrackerInterval", helmetTrackerIntervalSec);
		followerHelmetRestore = getB("Helmet", "bFollowerHelmetRestore", followerHelmetRestore);

		// [Legendary]
		legendary.respectLegendaryMutation     = getB("Legendary", "bRespectLegendaryMutation",     legendary.respectLegendaryMutation);
		legendary.legendaryCooldownSeconds     = getF("Legendary", "fLegendaryCooldownSeconds",     legendary.legendaryCooldownSeconds);
		legendary.mutationHealthRatioThreshold = getF("Legendary", "fMutationHealthRatioThreshold", legendary.mutationHealthRatioThreshold);

		// [Lists]
		raceBlocklist     = SplitCommaList(getS("Lists", "sRaceBlocklist", ""));
		keywordImmuneList = SplitCommaList(getS("Lists", "sKeywordImmune", "ActorTypeRobot"));
		feralGhoulRacePatterns = SplitCommaList(getS("Lists", "sFeralGhoulRacePatterns", "FeralGhoul"));
		if (feralGhoulRacePatterns.empty()) {
			feralGhoulRacePatterns.push_back("FeralGhoul");
		}

		// [RaceCategoryOverrides]
		raceCategoryOverrides.clear();
		{
			std::list<CSimpleIniA::Entry> raceKeys;
			ini.GetAllKeys("RaceCategoryOverrides", raceKeys);
			for (const auto& key : raceKeys) {
				const char* val = ini.GetValue("RaceCategoryOverrides", key.pItem, nullptr);
				if (val) {
					raceCategoryOverrides[key.pItem] = std::atoi(val);
				}
			}
		}

		// [AmmoOverrides]
		ammoOverrides.clear();
		std::list<CSimpleIniA::Entry> keys;
		ini.GetAllKeys("AmmoOverrides", keys);
		for (const auto& key : keys) {
			const char* val = ini.GetValue("AmmoOverrides", key.pItem, nullptr);
			if (val) {
				ammoOverrides[key.pItem] = std::atoi(val);
			}
		}

		logger::info("[HSK] Settings loaded from {}", kIniPath);
		logger::info("[HSK]   enabled={} debug={} killDamage={}", enabled, debugLogging, killDamage);
		logger::info("[HSK]   applyToPlayerAndFollowers={} levelGapThreshold={} raceBlocklist={} keywordImmune={} ammoOverrides={}",
			applyToPlayerAndFollowers, levelGapThreshold, raceBlocklist.size(), keywordImmuneList.size(), ammoOverrides.size());
	}

	// =============================================================
	// Settings::Save
	// =============================================================
	void Settings::Save()
	{
		std::unique_lock lk(_mutex);

		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);  // preserve unknown keys

		auto setB = [&](const char* sec, const char* key, bool v, const char* comment = nullptr) {
			ini.SetBoolValue(sec, key, v, comment);
		};
		auto setF = [&](const char* sec, const char* key, float v, const char* comment = nullptr) {
			ini.SetDoubleValue(sec, key, static_cast<double>(v), comment);
		};
		auto setI = [&](const char* sec, const char* key, int v, const char* comment = nullptr) {
			ini.SetLongValue(sec, key, v, comment);
		};
		auto setS = [&](const char* sec, const char* key, const char* v, const char* comment = nullptr) {
			ini.SetValue(sec, key, v, comment);
		};

		// [General]
		setB("General", "bEnableMod", enabled,
			"; Master toggle. When false, the plugin is dormant.");
		setB("General", "bEnableDebugLogging", debugLogging);
		setF("General", "fKillDamage", killDamage,
			"; Health damage applied to score the kill (default 99999).");

		// [Victims]
		setB("Victims", "bApplyToPlayerAndFollowers", applyToPlayerAndFollowers,
			"; If false, the player and player teammates are immune to instakill headshots.");
		setF("Victims", "fTwoShotWindowSeconds", twoShotWindowSeconds,
			"; Player/follower two-shot rule: 2nd headshot within this many seconds kills.");
		setF("Victims", "fTwoShotNearDeathRatio", twoShotNearDeathRatio,
			"; First shot on player/follower leaves HP at this fraction of max (0.05 = 5%).");
		setI("Victims", "iLevelGapThreshold", levelGapThreshold,
			"; Skip enemies more than this many levels above the player.");
		setI("Victims", "iEssentialMode", essentialMode,
			"; 0=Skip essentials, 1=Bleedout instead of kill, 2=Ignore essential flag.");

		// [Chances]
		setF("Chances", "fHumanoid",        chances.humanoid);
		setF("Chances", "fFeralGhoul",      chances.feralGhoul);
		setF("Chances", "fSmallCreature",   chances.smallCreature);
		setF("Chances", "fArmoredCreature", chances.armoredCreature);
		setF("Chances", "fLargeCreature",   chances.largeCreature);
		setF("Chances", "fSuperMutant",     chances.superMutant);
		setF("Chances", "fDeathclaw",       chances.deathclaw);
		setF("Chances", "fMirelurkQueen",   chances.mirelurkQueen);
		setF("Chances", "fSynth",           chances.synth);

		// [CaliberModifiers]
		setF("CaliberModifiers", "fPistolVsArmored",     caliberMods.pistolVsArmored);
		setF("CaliberModifiers", "fShotgunVsArmored",    caliberMods.shotgunVsArmored);
		setF("CaliberModifiers", "fRifleVsArmored",      caliberMods.rifleVsArmored);
		setF("CaliberModifiers", "fLargeRifleVsArmored", caliberMods.largeRifleVsArmored);
		setF("CaliberModifiers", "fPistolVsLarge",       caliberMods.pistolVsLarge);
		setF("CaliberModifiers", "fShotgunVsLarge",      caliberMods.shotgunVsLarge);
		setF("CaliberModifiers", "fRifleVsLarge",        caliberMods.rifleVsLarge);
		setF("CaliberModifiers", "fLargeRifleVsLarge",   caliberMods.largeRifleVsLarge);
		setF("CaliberModifiers", "fPistolVsSynth",       caliberMods.pistolVsSynth);
		setF("CaliberModifiers", "fShotgunVsSynth",      caliberMods.shotgunVsSynth);
		setF("CaliberModifiers", "fRifleVsSynth",        caliberMods.rifleVsSynth);
		setF("CaliberModifiers", "fLargeRifleVsSynth",   caliberMods.largeRifleVsSynth);
		setF("CaliberModifiers", "fPistolVsPA",          caliberMods.pistolVsPA);
		setF("CaliberModifiers", "fShotgunVsPA",         caliberMods.shotgunVsPA);
		setF("CaliberModifiers", "fRifleVsPA",           caliberMods.rifleVsPA);
		setF("CaliberModifiers", "fLargeRifleVsPA",      caliberMods.largeRifleVsPA);
		setF("CaliberModifiers", "fPistolVsFeralGhoul",     caliberMods.pistolVsFeralGhoul);
		setF("CaliberModifiers", "fShotgunVsFeralGhoul",    caliberMods.shotgunVsFeralGhoul);
		setF("CaliberModifiers", "fRifleVsFeralGhoul",      caliberMods.rifleVsFeralGhoul);
		setF("CaliberModifiers", "fLargeRifleVsFeralGhoul", caliberMods.largeRifleVsFeralGhoul);

		// [Helmet]
		setB("Helmet", "bEnableHelmetKnockoff",        helmet.enableHelmetKnockoff);
		setF("Helmet", "fKnockoffChancePistol",        helmet.knockoffChancePistol);
		setF("Helmet", "fKnockoffChanceRifle",         helmet.knockoffChanceRifle);
		setF("Helmet", "fKnockoffChanceShotgun",       helmet.knockoffChanceShotgun);
		setF("Helmet", "fPAKnockoffChanceRifle",       helmet.paKnockoffChanceRifle);
		setF("Helmet", "fPAKnockoffChanceLargeRifle",  helmet.paKnockoffChanceLargeRifle);
		setF("Helmet", "fDropLinearImpulse",           helmet.dropLinearImpulse);
		setF("Helmet", "fDropAngularImpulse",          helmet.dropAngularImpulse);
		setF("Helmet", "fDropSpawnHeight",             helmet.dropSpawnHeight);
		setF("Helmet", "fDropFlyAgainstShot",          helmet.dropFlyAgainstShot);
		setF("Helmet", "fDropFlyUpLift",               helmet.dropFlyUpLift);
		setF("Helmet", "fShotgunPelletGroupWindowSec", helmet.shotgunPelletGroupWindowSec);

		// [Melee]
		setB("Melee", "bEnableMeleeKnockoff", melee.enableMeleeKnockoff,
			"; Master toggle for melee helmet knockoff.\n"
			"; If true, melee hits to the head can knock off helmets.");
		setF("Melee", "fKnockoffChanceMedium", melee.meleeKnockoffChanceMedium,
			"; Chance per medium melee weapon (one-handed sword/axe/mace) head hit to knock off a helmet.");
		setF("Melee", "fKnockoffChanceLarge", melee.meleeKnockoffChanceLarge,
			"; Chance per large melee weapon (two-handed/sledge/bat) head hit to knock off a helmet.");
		setB("Melee", "bGunBashKnockoff", melee.gunBashKnockoff,
			"; If true, bashing someone in the head with a non-pistol firearm can knock off their helmet.");
		setF("Melee", "fGunBashKnockoffChance", melee.gunBashKnockoffChance,
			"; Chance per gun-bash head hit (non-pistol firearm) to knock off a helmet.");

		// [ArmorScaling]
		setF("ArmorScaling", "fBallisticHalfAR",   armorScaling.ballisticHalfAR,
			"; AR at which headshot chance drops to 50%. Vanilla non-PA helmets range 2-35.");
		setF("ArmorScaling", "fEnergyHalfAR",      armorScaling.energyHalfAR,
			"; Energy AR at which headshot chance drops to 50%.");
		setF("ArmorScaling", "fCrossResistFactor", armorScaling.crossResistFactor,
			"; Fraction of ballistic AR that contributes when an energy weapon hits.");

		// [Advanced]
		setF("Advanced", "fAmmoDamageReferenceDamage", advanced.ammoDamageReferenceDamage,
			"; Baseline ammo damage; rounds above this get a chance boost, below get a penalty.");
		setF("Advanced", "fAmmoDamageInfluence",       advanced.ammoDamageInfluence,
			"; 0 = ammo damage has no effect, 1 = full effect on chance.");
		setB("Advanced", "bEnableDistanceFalloff",     advanced.enableDistanceFalloff,
			"; Headshot chance drops off at long range.");
		setF("Advanced", "fDistanceFullChanceUnits",   advanced.distanceFullChanceUnits,
			"; Distance (game units) below which full chance applies. ~3000 = ~50m.");
		setF("Advanced", "fDistanceZeroChanceUnits",   advanced.distanceZeroChanceUnits,
			"; Distance beyond which headshot chance is 0. ~15000 = ~210m.");
		setF("Advanced", "fSneakBonusMul",             advanced.sneakBonusMul,
			"; Chance multiplier when attacker is sneaking. 1.25 = 25% bonus.");
		setB("Advanced", "bVATSRequiresCritical",     advanced.vatsRequiresCritical,
			"; If true, headshot instakills in VATS only fire on critical hits.");
		setF("Advanced", "fCriticalBonusChance",      advanced.criticalBonusChance,
			"; Flat %% bonus to instakill chance for critical hits outside VATS (default 15).");

		// [PlayerFeedback]
		setB("PlayerFeedback", "bEnableFeedback",          playerFeedback.enableFeedback,
			"; Master toggle for player-headshot feedback (tinnitus + screen effects).");
		setB("PlayerFeedback", "bEnableTinnitusSound",     playerFeedback.enableTinnitusSound,
			"; Play a tinnitus WAV when the player gets headshotted.");
		setF("PlayerFeedback", "fTinnitusSoundVolume",     playerFeedback.tinnitusSoundVolume,
			"; Volume scale for the tinnitus sound (0.0 = silent, 1.0 = full).");
		setS("PlayerFeedback", "sTinnitusSoundFile",       playerFeedback.tinnitusSoundFile.c_str(),
			"; WAV filename in Data/F4SE/Plugins/HeadshotsKillF4SE/.");
		setB("PlayerFeedback", "bEnableAudioMuffle",       playerFeedback.enableAudioMuffle,
			"; Apply a low-pass filter (muffle) to game audio on headshot, then fade back.");
		setF("PlayerFeedback", "fMuffleIntensity",         playerFeedback.muffleIntensity,
			"; Frequency multiplier at peak muffle. 0.0 = silence, 0.15 = heavy muffle, 1.0 = no effect.");
		setF("PlayerFeedback", "fMuffleFadeDuration",      playerFeedback.muffleFadeDuration,
			"; Seconds to fade from muffled back to normal audio.");
		setF("PlayerFeedback", "fFeedbackCooldown",        playerFeedback.feedbackCooldown,
			"; Minimum seconds between feedback triggers (prevents spam from rapid hits).");

		// [FeedbackConcussion]
		setB("FeedbackConcussion", "bEnabled",       playerFeedback.concussion.enabled,
			"; Double-vision / concussion wobble on player headshot.");
		setS("FeedbackConcussion", "sIModEditorID",  playerFeedback.concussion.imodEditorID.c_str(),
			"; Vanilla IMod for concussion effect.");
		setF("FeedbackConcussion", "fDuration",      playerFeedback.concussion.duration,
			"; Seconds the concussion effect stays active.");
		setF("FeedbackConcussion", "fStrength",      playerFeedback.concussion.strength,
			"; Blend strength of the effect (0.0 = invisible, 1.0 = full).");

		// [FeedbackImpactFlash]
		setB("FeedbackImpactFlash", "bEnabled",      playerFeedback.impactFlash.enabled,
			"; Brief bright flash simulating the impact moment.");
		setS("FeedbackImpactFlash", "sIModEditorID", playerFeedback.impactFlash.imodEditorID.c_str(),
			"; Vanilla IMod for impact flash.");
		setF("FeedbackImpactFlash", "fDuration",     playerFeedback.impactFlash.duration,
			"; Seconds the flash lasts (keep very short).");
		setF("FeedbackImpactFlash", "fStrength",     playerFeedback.impactFlash.strength,
			"; Blend strength of the flash.");

		// [FeedbackGreyscale]
		setB("FeedbackGreyscale", "bEnabled",        playerFeedback.greyscale.enabled,
			"; Desaturated / greyscale screen for post-headshot recovery period.");
		setS("FeedbackGreyscale", "sIModEditorID",   playerFeedback.greyscale.imodEditorID.c_str(),
			"; Vanilla IMod for greyscale/desaturation effect.");
		setF("FeedbackGreyscale", "fDuration",       playerFeedback.greyscale.duration,
			"; Seconds the greyscale lingers.");
		setF("FeedbackGreyscale", "fStrength",       playerFeedback.greyscale.strength,
			"; Blend strength of the greyscale effect.");

		// [KillImpulse]
		setB("KillImpulse", "bEnabled",     killImpulse.enabled,
			"; Rotate the head/neck bone on headshot (layers on death animations).");
		setB("KillImpulse", "bApplyOnAllHeadshots", killImpulse.applyOnAllHeadshots,
			"; If true, head snap fires on every headshot to non-player humanoids.\n"
			"; If false (default), only fires on instakill headshots.");
		setF("KillImpulse", "fMagnitude",   killImpulse.magnitude,
			"; Head snap angle in degrees. 25 = dramatic but believable.");
		setF("KillImpulse", "fUpwardBias",  killImpulse.upwardBias,
			"; Upward tilt on the snap axis. 0 = purely backward, 0.3 = slight upward tilt.");
		setF("KillImpulse", "fDecayDuration", killImpulse.decayDuration,
			"; How long (seconds) the head snap persists before decaying back to neutral.\n"
			"; Higher = head stays rotated longer. 1.5s default.");

		// [Helmet] player extensions
		setB("Helmet", "bPlayerHelmetKnockoff", playerHelmetKnockoffEnabled,
			"; Master toggle: can the player's own helmet be knocked off?\n"
			"; false = player is immune to helmet knockoff entirely.");
		setF("Helmet", "fPlayerKnockoffChanceMult", playerHelmetKnockoffMult,
			"; Multiplier applied to the base knockoff chance when the player is hit.\n"
			"; 1.0 = same as NPCs, 0.5 = half chance, 2.0 = double, 0 = never.");
		setB("Helmet", "bPlayerHelmetProtection", playerHelmetProtection,
			"; If true, a headshot can only instakill the player while they are bare-headed.\n"
			"; A helmeted player is immune to instakill; enemies must knock off the helmet\n"
			"; first before a follow-up headshot can kill.");
		setF("Helmet", "fPlayerInstakillMinAR", playerInstakillMinAR,
			"; Only active when bPlayerHelmetProtection=false.\n"
			"; Player can only be instakilled if their head armor's ballistic AR is BELOW this value.\n"
			"; Set to 0 to disable (player is always vulnerable regardless of AR).\n"
			"; Default 8.0: light head armor (AR >= 8) blocks instakill; bare-headed or near-bare is vulnerable.");
		setB("Helmet", "bNotifyPlayerKnockoff", helmetNotifyPlayer,
			"; Show a HUD notification when the player's helmet gets knocked off.");
		setB("Helmet", "bAutoReequipPlayer",   helmetAutoReequip,
			"; Auto-equip the player's helmet when they pick it up after knockoff.");
		setB("Helmet", "bShaderEnabled",       helmetShaderEnabled,
			"; Apply a glow/highlight shader to the dropped helmet so the player can find it.");
		setS("Helmet", "sShaderEditorID",      helmetShaderEditorID.c_str(),
			"; EditorID of the TESEffectShader to apply (vanilla: WorkshopHighlightShader).");
		setF("Helmet", "fShaderDuration",      helmetShaderDuration,
			"; Seconds to keep the shader active. -1 = indefinite (until pickup).");
		setB("Helmet", "bHelmetLight", helmetLightEnabled,
			"; Place an unshadowed point light at the helmet's drop location.\n"
			"; Requires a valid TESObjectLIGH EditorID below (vanilla or modded).");
		setS("Helmet", "sHelmetLightEditorID", helmetLightEditorID.c_str(),
			"; EditorID of a TESObjectLIGH form to place at the helmet. Must exist in\n"
			"; your load order. Vanilla examples: DefaultLight01NSFill, SpotLight01NS.");
		setB("Helmet", "bHelmetTracker", helmetTrackerEnabled,
			"; Show a periodic HUD message with compass direction + distance to your\n"
			"; dropped helmet so you can find it in dense environments.");
		setF("Helmet", "fHelmetTrackerInterval", helmetTrackerIntervalSec,
			"; How often (seconds) the tracker HUD message refreshes. Lower = more frequent.");
		setB("Helmet", "bFollowerHelmetRestore", followerHelmetRestore,
			"; Auto-restore a follower's knocked-off helmet back into their inventory\n"
			"; and re-equip it when combat ends (or on save load).");

		// [Legendary]
		setB("Legendary", "bRespectLegendaryMutation",     legendary.respectLegendaryMutation,
			"; If true, do not insta-kill a legendary enemy that just mutated.");
		setF("Legendary", "fLegendaryCooldownSeconds",     legendary.legendaryCooldownSeconds);
		setF("Legendary", "fMutationHealthRatioThreshold", legendary.mutationHealthRatioThreshold);

		// [Lists]
		setS("Lists", "sRaceBlocklist",
			JoinCommaList(raceBlocklist).c_str(),
			"; Comma-separated race EditorIDs to exclude.");
		setS("Lists", "sKeywordImmune",
			JoinCommaList(keywordImmuneList).c_str(),
			"; Comma-separated BGSKeyword EditorIDs that exempt the actor.");
		setS("Lists", "sFeralGhoulRacePatterns",
			JoinCommaList(feralGhoulRacePatterns).c_str(),
			"; Comma-separated substrings of TESRace EditorID. Matched Humanoids use "
			"fFeralGhoul base chance + f*VsFeralGhoul multipliers; helmet AR ignored for instakill chance.");

		// [RaceCategoryOverrides]
		ini.Delete("RaceCategoryOverrides", nullptr);
		for (const auto& [key, val] : raceCategoryOverrides) {
			ini.SetLongValue("RaceCategoryOverrides", key.c_str(), val,
				"; Race EditorID pattern -> ActorCategory int (1=Humanoid,2=SuperMutant,3=SmallCreature,4=ArmoredCreature,5=LargeCreature,6=Robot,7=Synth)");
		}

		// [AmmoOverrides]
		ini.Delete("AmmoOverrides", nullptr);
		for (const auto& [key, val] : ammoOverrides) {
			ini.SetLongValue("AmmoOverrides", key.c_str(), val);
		}

		std::filesystem::create_directories(std::filesystem::path(kIniPath).parent_path());
		ini.SaveFile(kIniPath);
		logger::info("[HSK] Settings saved to {}", kIniPath);
	}

	// =============================================================
	// Settings::SaveAmmoOverridesOnly
	//
	// Reads the current INI from disk, replaces ONLY the
	// [AmmoOverrides] section with the in-memory map, writes back.
	// Any other section (General/Chances/Helmet/etc.) is left exactly
	// as it currently appears on disk.
	//
	// This protects the user from accidentally committing pending
	// non-ammo changes when they tweak an ammo classification.
	// =============================================================
	void Settings::SaveAmmoOverridesOnly()
	{
		std::shared_lock lk(_mutex);

		CSimpleIniA ini;
		ini.SetUnicode();
		// Loading the existing file means any pending non-ammo edits
		// in the live `Settings` object are NOT carried into the file.
		(void)ini.LoadFile(kIniPath);

		ini.Delete("AmmoOverrides", nullptr);
		for (const auto& [key, val] : ammoOverrides) {
			ini.SetLongValue("AmmoOverrides", key.c_str(), val);
		}

		std::filesystem::create_directories(std::filesystem::path(kIniPath).parent_path());
		ini.SaveFile(kIniPath);
		logger::info("[HSK] Ammo overrides saved (other sections untouched)");
	}
}

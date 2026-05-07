#include "ActorClassifier.h"
#include "Settings.h"

namespace HSK
{
	static std::string ToLower(std::string_view a_s)
	{
		std::string out(a_s);
		std::transform(out.begin(), out.end(), out.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
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

	void ActorClassifier::Init()
	{
		std::unique_lock lk(_mutex);

		auto lookup = [](const char* edid) -> RE::BGSKeyword* {
			return RE::TESForm::GetFormByEditorID<RE::BGSKeyword>(edid);
		};

		_keywords.actorTypeRobot          = lookup("ActorTypeRobot");
		_keywords.actorTypeSuperMutant    = lookup("ActorTypeSuperMutant");
		_keywords.actorTypeDeathclaw      = lookup("ActorTypeDeathclaw");
		_keywords.actorTypeMirelurk       = lookup("ActorTypeMirelurk");
		_keywords.actorTypeMirelurkQueen  = lookup("ActorTypeMirelurkQueen");
		_keywords.actorTypeRadscorpion    = lookup("ActorTypeRadScorpion");
		if (!_keywords.actorTypeRadscorpion) {
			_keywords.actorTypeRadscorpion = lookup("ActorTypeRadscorpion");
		}
		_keywords.actorTypeAnimal     = lookup("ActorTypeAnimal");
		_keywords.actorTypeNPC        = lookup("ActorTypeNPC");
		_keywords.actorTypeGhoul      = lookup("ActorTypeGhoul");
		_keywords.actorTypeSynth      = lookup("ActorTypeSynth");
		_keywords.actorTypeBear       = lookup("ActorTypeYaoGuai");  // F4 yao guai is bear-style
		_keywords.actorTypeBehemoth   = lookup("ActorTypeBehemoth");
		_keywords.actorTypeFogCrawler = lookup("ActorTypeFogCrawler");
		_keywords.actorTypeYaoGuai    = lookup("ActorTypeYaoGuai");

		_keywords.encTypeLegendary    = lookup("encTypeLegendary");
		if (!_keywords.encTypeLegendary) {
			_keywords.encTypeLegendary = lookup("EncTypeLegendary");
		}

		// Build user-immune keyword set
		_userImmuneKeywordIDs.clear();
		for (const auto& edid : Settings::GetSingleton()->keywordImmuneList) {
			if (auto* kw = RE::TESForm::GetFormByEditorID<RE::BGSKeyword>(edid.c_str())) {
				_userImmuneKeywordIDs.insert(kw->GetFormID());
			}
		}

		logger::info("[HSK] ActorClassifier initialized: legendaryKW={} immuneKWs={}",
			_keywords.encTypeLegendary != nullptr, _userImmuneKeywordIDs.size());
	}

	bool ActorClassifier::IsRaceBlocklisted(RE::Actor* a_actor) const
	{
		if (!a_actor || !a_actor->race) return false;
		const char* raceEdid = a_actor->race->GetFormEditorID();
		if (!raceEdid) return false;

		const auto& blocklist = Settings::GetSingleton()->raceBlocklist;
		for (const auto& entry : blocklist) {
			if (entry.empty()) continue;
			if (ContainsCaseInsensitive(raceEdid, entry)) {
				return true;
			}
		}
		return false;
	}

	bool ActorClassifier::HasImmuneKeyword(RE::Actor* a_actor) const
	{
		if (!a_actor) return false;
		std::shared_lock lk(_mutex);
		for (auto kwID : _userImmuneKeywordIDs) {
			auto* kw = RE::TESForm::GetFormByID<RE::BGSKeyword>(kwID);
			if (kw && a_actor->HasKeyword(kw, nullptr)) {
				return true;
			}
		}
		return false;
	}

	bool ActorClassifier::IsPlayerFollower(RE::Actor* a_actor) const
	{
		if (!a_actor) return false;

		// Player isn't their own follower.
		if (a_actor->IsPlayerRef()) return false;

		// Check kFollower extra data type (set by the engine on hired companions and
		// is the standard "is in player's party" flag).
		if (auto* extra = a_actor->extraList.get();
			extra && extra->HasType(RE::EXTRA_DATA_TYPE::kFollower)) {
			return true;
		}

		// Note: AIProcess::commandingActor is internal in this CommonLibF4 build;
		// kFollower above already covers the player's hired companions which is
		// the primary use case.
		return false;
	}

	// Helper: check if any string in a pool is a substring of a_haystack (case-insensitive).
	static bool MatchesAnyPattern(const std::string& a_haystack,
		std::initializer_list<const char*> a_patterns)
	{
		for (const char* p : a_patterns) {
			if (ContainsCaseInsensitive(a_haystack, p))
				return true;
		}
		return false;
	}

	// Build a combined searchable string from all the actor's identity
	// fields: race EDID, race name, NPC full name, NPC EDID.
	static std::string BuildSearchString(RE::Actor* a_actor)
	{
		std::string s;
		if (a_actor->race) {
			const char* raceEdid = a_actor->race->GetFormEditorID();
			if (raceEdid && *raceEdid) { s += raceEdid; s += ' '; }
			const char* raceName = a_actor->race->GetFullName();
			if (raceName && *raceName) { s += raceName; s += ' '; }
		}
		if (auto* npc = a_actor->GetNPC()) {
			const char* actorName = npc->GetFullName();
			if (actorName && *actorName) { s += actorName; s += ' '; }
		}
		if (auto* npc = a_actor->GetNPC()) {
			const char* npcEdid = npc->GetFormEditorID();
			if (npcEdid && *npcEdid) { s += npcEdid; s += ' '; }
		}
		return s;
	}

	// If category is Humanoid and the race EditorID matches any feralGhoulRacePatterns
	// substring, mark isFeralGhoul (separate instakill tuning; helmet ignored in chance).
	static void ApplyFeralGhoulRaceTag(RE::Actor* a_actor, ActorInfo& io)
	{
		io.isFeralGhoul = false;
		if (io.category != ActorCategory::Humanoid || !a_actor || !a_actor->race) {
			return;
		}
		const char* raceEdid = a_actor->race->GetFormEditorID();
		if (!raceEdid || !*raceEdid) return;
		for (const auto& pat : Settings::GetSingleton()->feralGhoulRacePatterns) {
			if (pat.empty()) continue;
			if (ContainsCaseInsensitive(raceEdid, pat)) {
				io.isFeralGhoul = true;
				return;
			}
		}
	}

	ActorInfo ActorClassifier::Classify(RE::Actor* a_actor) const
	{
		ActorInfo info;
		if (!a_actor) return {};

		info.isPlayer = a_actor->IsPlayerRef();
		info.isFollower = IsPlayerFollower(a_actor);
		info.scale = 1.0f;

		// Power Armor check (extraList->HasType(kPowerArmor))
		if (auto* extra = a_actor->extraList.get()) {
			info.isInPowerArmor = extra->HasType(RE::EXTRA_DATA_TYPE::kPowerArmor);
		}

		// Level
		if (auto* npc = a_actor->GetNPC()) {
			info.level = npc->actorData.level;
		}

		// Legendary check
		std::shared_lock lk(_mutex);
		if (_keywords.encTypeLegendary &&
			a_actor->HasKeyword(_keywords.encTypeLegendary, nullptr)) {
			info.isLegendary = true;
		}

		// =========================================================
		// 0. User race-category overrides (highest priority).
		//    These are substring matches against Race EditorID.
		// =========================================================
		if (a_actor->race) {
			const char* raceEdid = a_actor->race->GetFormEditorID();
			if (raceEdid && *raceEdid) {
				const auto& overrides = Settings::GetSingleton()->raceCategoryOverrides;
				for (const auto& [pattern, catInt] : overrides) {
					if (pattern.empty()) continue;
					if (ContainsCaseInsensitive(raceEdid, pattern)) {
						auto cat = static_cast<ActorCategory>(catInt);
						info.category = cat;
						if (cat == ActorCategory::LargeCreature) {
							std::string lr = ToLower(raceEdid);
							if (lr.find("deathclaw") != std::string::npos) info.isDeathclaw = true;
							if (lr.find("queen") != std::string::npos) info.isMirelurkQueen = true;
						}
						ApplyFeralGhoulRaceTag(a_actor, info);
						return info;
					}
				}
			}
		}

		// =========================================================
		// 1. Keyword-based classification (vanilla + modded keywords)
		// =========================================================

		// 1a. Robots are always excluded
		if (_keywords.actorTypeRobot && a_actor->HasKeyword(_keywords.actorTypeRobot, nullptr)) {
			info.category = ActorCategory::Robot;
			return info;
		}

		// 1b. Large creatures
		if (_keywords.actorTypeMirelurkQueen && a_actor->HasKeyword(_keywords.actorTypeMirelurkQueen, nullptr)) {
			info.category = ActorCategory::LargeCreature;
			info.isMirelurkQueen = true;
			ApplyFeralGhoulRaceTag(a_actor, info);
			return info;
		}
		if (_keywords.actorTypeBehemoth && a_actor->HasKeyword(_keywords.actorTypeBehemoth, nullptr)) {
			info.category = ActorCategory::LargeCreature;
			ApplyFeralGhoulRaceTag(a_actor, info);
			return info;
		}
		if (_keywords.actorTypeDeathclaw && a_actor->HasKeyword(_keywords.actorTypeDeathclaw, nullptr)) {
			info.category = ActorCategory::LargeCreature;
			info.isDeathclaw = true;
			ApplyFeralGhoulRaceTag(a_actor, info);
			return info;
		}
		if (_keywords.actorTypeFogCrawler && a_actor->HasKeyword(_keywords.actorTypeFogCrawler, nullptr)) {
			info.category = ActorCategory::LargeCreature;
			ApplyFeralGhoulRaceTag(a_actor, info);
			return info;
		}
		if (_keywords.actorTypeYaoGuai && a_actor->HasKeyword(_keywords.actorTypeYaoGuai, nullptr)) {
			info.category = ActorCategory::LargeCreature;
			ApplyFeralGhoulRaceTag(a_actor, info);
			return info;
		}

		// 1c. Super mutants
		if (_keywords.actorTypeSuperMutant && a_actor->HasKeyword(_keywords.actorTypeSuperMutant, nullptr)) {
			info.category = ActorCategory::SuperMutant;
			ApplyFeralGhoulRaceTag(a_actor, info);
			return info;
		}

		// 1d. Armored creatures
		if (_keywords.actorTypeMirelurk && a_actor->HasKeyword(_keywords.actorTypeMirelurk, nullptr)) {
			info.category = ActorCategory::ArmoredCreature;
			ApplyFeralGhoulRaceTag(a_actor, info);
			return info;
		}
		if (_keywords.actorTypeRadscorpion && a_actor->HasKeyword(_keywords.actorTypeRadscorpion, nullptr)) {
			info.category = ActorCategory::ArmoredCreature;
			ApplyFeralGhoulRaceTag(a_actor, info);
			return info;
		}

		// 1e. Standard humanoids
		if (_keywords.actorTypeNPC && a_actor->HasKeyword(_keywords.actorTypeNPC, nullptr)) {
			info.category = ActorCategory::Humanoid;
			ApplyFeralGhoulRaceTag(a_actor, info);
			return info;
		}
		if (_keywords.actorTypeGhoul && a_actor->HasKeyword(_keywords.actorTypeGhoul, nullptr)) {
			info.category = ActorCategory::Humanoid;
			ApplyFeralGhoulRaceTag(a_actor, info);
			return info;
		}
		if (_keywords.actorTypeSynth && a_actor->HasKeyword(_keywords.actorTypeSynth, nullptr)) {
			info.category = ActorCategory::Synth;
			ApplyFeralGhoulRaceTag(a_actor, info);
			return info;
		}

		// 1f. Common animals -> small creatures by default
		if (_keywords.actorTypeAnimal && a_actor->HasKeyword(_keywords.actorTypeAnimal, nullptr)) {
			// If the animal's name/race contains a known large-creature
			// pattern, override SmallCreature -> LargeCreature. This catches
			// modded yao guai, bears, etc. that only have ActorTypeAnimal.
			const std::string search = BuildSearchString(a_actor);
			if (MatchesAnyPattern(search, { "yao guai", "yaoguai", "bear",
				"deathclaw", "fog crawler", "fogcrawler", "behemoth" })) {
				info.category = ActorCategory::LargeCreature;
				if (ContainsCaseInsensitive(search, "deathclaw")) info.isDeathclaw = true;
				ApplyFeralGhoulRaceTag(a_actor, info);
				return info;
			}
			info.category = ActorCategory::SmallCreature;
			ApplyFeralGhoulRaceTag(a_actor, info);
			return info;
		}

		// =========================================================
		// 2. Name + Race EditorID heuristics for actors without
		//    *any* vanilla ActorType keyword (many modded creatures).
		//    We search Race EDID, Race name, NPC full name, NPC EDID
		//    as a combined lowercase string.
		// =========================================================
		const std::string search = BuildSearchString(a_actor);
		if (!search.empty()) {
			// Large creatures
			if (MatchesAnyPattern(search, { "deathclaw" })) {
				info.category = ActorCategory::LargeCreature;
				info.isDeathclaw = true;
				ApplyFeralGhoulRaceTag(a_actor, info);
				return info;
			}
			if (MatchesAnyPattern(search, { "queen" })) {
				info.category = ActorCategory::LargeCreature;
				info.isMirelurkQueen = true;
				ApplyFeralGhoulRaceTag(a_actor, info);
				return info;
			}
			if (MatchesAnyPattern(search, { "behemoth", "fog crawler", "fogcrawler",
				"yao guai", "yaoguai", "bear", "gulper", "hermit crab",
				"fog creeper", "anglerfish", "angler" })) {
				info.category = ActorCategory::LargeCreature;
				ApplyFeralGhoulRaceTag(a_actor, info);
				return info;
			}
			// Super mutants
			if (MatchesAnyPattern(search, { "super mutant", "supermutant", "nightkin" })) {
				info.category = ActorCategory::SuperMutant;
				ApplyFeralGhoulRaceTag(a_actor, info);
				return info;
			}
			// Armored creatures
			if (MatchesAnyPattern(search, { "mirelurk", "radscorpion", "rad scorpion",
				"crab", "crawler" })) {
				info.category = ActorCategory::ArmoredCreature;
				ApplyFeralGhoulRaceTag(a_actor, info);
				return info;
			}
			// Synths
			if (MatchesAnyPattern(search, { "synthgen1", "synthgen2", "synth gen 1",
				"synth gen 2", "synth courser" })) {
				info.category = ActorCategory::Synth;
				ApplyFeralGhoulRaceTag(a_actor, info);
				return info;
			}
			// Robots
			if (MatchesAnyPattern(search, { "robot", "turret", "sentry", "assaultron",
				"protectron", "mr. handy", "mr. gutsy", "eyebot" })) {
				info.category = ActorCategory::Robot;
				ApplyFeralGhoulRaceTag(a_actor, info);
				return info;
			}
			// Humanoids
			if (MatchesAnyPattern(search, { "human", "ghoul", "raider", "gunner",
				"settler", "minutem" })) {
				info.category = ActorCategory::Humanoid;
				ApplyFeralGhoulRaceTag(a_actor, info);
				return info;
			}
		}

		// 3. Default for unknown modded creatures: SmallCreature
		info.category = ActorCategory::SmallCreature;
		ApplyFeralGhoulRaceTag(a_actor, info);
		return info;
	}
}

#pragma once

#include "PCH.h"

namespace HSK
{
	enum class ActorCategory : std::uint8_t
	{
		None             = 0,
		Humanoid         = 1,  // Standard humanoid (raiders, gunners, NPCs, ghouls)
		SuperMutant      = 2,  // Super mutants, behemoths, nightkin -> large rules
		SmallCreature    = 3,  // Radroach, bloatfly, dog, mole rat, etc. -> 100% any caliber
		ArmoredCreature  = 4,  // Mirelurk, radscorpion -> face hit only, rifle+
		LargeCreature    = 5,  // Deathclaw, yao guai, fog crawler, mirelurk queen -> rifle+
		Robot            = 6,  // Excluded by default
		Synth            = 7,  // All synths (Gen-1/2/3) -- humanoid-like, can wear helmets (Gen-3)
	};

	[[nodiscard]] inline const char* ActorCategoryName(ActorCategory a_c)
	{
		switch (a_c) {
		case ActorCategory::Humanoid:        return "Humanoid";
		case ActorCategory::SuperMutant:     return "SuperMutant";
		case ActorCategory::SmallCreature:   return "SmallCreature";
		case ActorCategory::ArmoredCreature: return "ArmoredCreature";
		case ActorCategory::LargeCreature:   return "LargeCreature";
		case ActorCategory::Robot:           return "Robot";
		case ActorCategory::Synth:           return "Synth";
		default:                             return "None";
		}
	}

	struct ActorInfo
	{
		ActorCategory  category{ ActorCategory::Humanoid };
		bool           isPlayer{ false };
		bool           isFollower{ false };
		bool           isLegendary{ false };
		bool           isInPowerArmor{ false };
		bool           isDeathclaw{ false };
		bool           isMirelurkQueen{ false };
		std::uint16_t  level{ 1 };
		float          scale{ 1.0f };
	};

	class ActorClassifier
	{
	public:
		static ActorClassifier* GetSingleton()
		{
			static ActorClassifier singleton;
			return &singleton;
		}

		// Cache keyword form pointers we look up by EditorID. Call after kGameDataReady.
		void Init();

		// Classify a target actor. Not cached -- actors can change equipment/state.
		[[nodiscard]] ActorInfo Classify(RE::Actor* a_actor) const;

		// True if the actor's Race::EditorID is in the user's race blocklist.
		[[nodiscard]] bool IsRaceBlocklisted(RE::Actor* a_actor) const;

		// True if the actor has a keyword that the user marked as "immune" (default: ActorTypeRobot).
		[[nodiscard]] bool HasImmuneKeyword(RE::Actor* a_actor) const;

		// True if `a_actor` is a follower of the player (companion or commanded actor).
		[[nodiscard]] bool IsPlayerFollower(RE::Actor* a_actor) const;

		// Cached keyword pointers (nullable -- mod may not have the keyword loaded).
		struct KeywordCache
		{
			RE::BGSKeyword* actorTypeRobot{ nullptr };
			RE::BGSKeyword* actorTypeSuperMutant{ nullptr };
			RE::BGSKeyword* actorTypeDeathclaw{ nullptr };
			RE::BGSKeyword* actorTypeMirelurk{ nullptr };
			RE::BGSKeyword* actorTypeMirelurkQueen{ nullptr };
			RE::BGSKeyword* actorTypeRadscorpion{ nullptr };
			RE::BGSKeyword* actorTypeAnimal{ nullptr };
			RE::BGSKeyword* actorTypeNPC{ nullptr };
			RE::BGSKeyword* actorTypeGhoul{ nullptr };
			RE::BGSKeyword* actorTypeSynth{ nullptr };
			RE::BGSKeyword* actorTypeBear{ nullptr };
			RE::BGSKeyword* actorTypeBehemoth{ nullptr };
			RE::BGSKeyword* actorTypeFogCrawler{ nullptr };
			RE::BGSKeyword* actorTypeYaoGuai{ nullptr };
			// Legendary detection
			RE::BGSKeyword* encTypeLegendary{ nullptr };
		};

		[[nodiscard]] const KeywordCache& Keywords() const noexcept { return _keywords; }

	private:
		ActorClassifier() = default;
		~ActorClassifier() = default;
		ActorClassifier(const ActorClassifier&) = delete;
		ActorClassifier& operator=(const ActorClassifier&) = delete;

		KeywordCache                                  _keywords;
		std::unordered_set<std::uint32_t>             _userImmuneKeywordIDs;
		mutable std::shared_mutex                     _mutex;
	};
}

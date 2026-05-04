#include "AmmoClassifier.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace HSK
{
	using json = nlohmann::json;

	// =================================================================
	// Helpers
	// =================================================================
	static std::string ToLower(std::string_view a_s)
	{
		std::string out(a_s);
		std::transform(out.begin(), out.end(), out.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

	static bool ContainsAny(const std::string& a_lower, const std::vector<std::string>& a_subs)
	{
		for (const auto& s : a_subs) {
			if (s.empty()) continue;
			if (a_lower.find(s) != std::string::npos) return true;
		}
		return false;
	}

	static std::string GetSourcePluginName(const RE::TESForm* a_form)
	{
		if (!a_form) return {};
		auto* file = const_cast<RE::TESForm*>(a_form)->GetFile(0);
		if (!file) return {};
		return std::string(file->GetFilename());
	}

	std::string AmmoClassifier::MakeOverrideKey(std::string_view a_plugin, std::string_view a_edid)
	{
		std::string key;
		key.reserve(a_plugin.size() + 1 + a_edid.size());
		key.append(ToLower(a_plugin));
		key.push_back(':');
		key.append(ToLower(a_edid));
		return key;
	}

	std::string AmmoClassifier::MakeOverrideKey(const RE::TESAmmo* a_ammo)
	{
		if (!a_ammo) return {};
		const auto plugin = GetSourcePluginName(a_ammo);
		const char* edid  = a_ammo->GetFormEditorID();
		if (!edid) edid = "";
		return MakeOverrideKey(plugin, edid);
	}

	// =================================================================
	// JSON loading
	// =================================================================
	void AmmoClassifier::LoadJson()
	{
		_jsonByPluginEdid.clear();
		_jsonByEdid.clear();
		_heuristics = {};

		std::ifstream f(Settings::kJsonPath);
		if (!f.is_open()) {
			logger::warn("[HSK] ammo_calibers.json not found at {}; using heuristics only", Settings::kJsonPath);
			return;
		}

		json doc;
		try {
			f >> doc;
		} catch (const std::exception& e) {
			logger::error("[HSK] Failed to parse ammo_calibers.json: {}", e.what());
			return;
		}

		// Parse entries
		if (doc.contains("ammo") && doc["ammo"].is_array()) {
			for (const auto& item : doc["ammo"]) {
				if (!item.is_object() || !item.contains("match")) continue;
				const std::string match = item["match"].get<std::string>();
				const std::string cal   = item.value("caliber", "pistol");
				const std::string dt    = item.value("damageType", "ballistic");

				JsonEntry e;
				e.caliber    = CaliberFromString(cal);
				e.damageType = (dt == "energy") ? DamageType::Energy : DamageType::Ballistic;

				const auto colonPos = match.find(':');
				if (colonPos != std::string::npos) {
					e.sourcePlugin = match.substr(0, colonPos);
					e.editorID     = match.substr(colonPos + 1);
					_jsonByPluginEdid[ToLower(match)] = e;
					// Also index by EditorID alone as a fallback (don't overwrite existing entries).
					_jsonByEdid.emplace(ToLower(e.editorID), e);
				} else {
					e.editorID = match;
					_jsonByEdid[ToLower(match)] = e;
				}
			}
		}

		// Parse heuristics
		if (doc.contains("heuristics") && doc["heuristics"].is_object()) {
			const auto& h = doc["heuristics"];

			auto fillVec = [&](const char* key, std::vector<std::string>& out) {
				if (h.contains(key) && h[key].is_array()) {
					out.reserve(h[key].size());
					for (const auto& v : h[key]) {
						if (v.is_string()) out.push_back(ToLower(v.get<std::string>()));
					}
				}
			};

			fillVec("exclude_substrings",     _heuristics.excludeSubs);
			fillVec("shotgun_substrings",     _heuristics.shotgunSubs);
			fillVec("pistol_substrings",      _heuristics.pistolSubs);
			fillVec("rifle_substrings",       _heuristics.rifleSubs);
			fillVec("large_rifle_substrings", _heuristics.largeRifleSubs);
			fillVec("energy_substrings",      _heuristics.energySubs);
		}

		logger::info("[HSK] AmmoClassifier loaded {} explicit ammo entries, {} EditorID fallback entries",
			_jsonByPluginEdid.size(), _jsonByEdid.size());
	}

	bool AmmoClassifier::ApplyJsonMatch(const std::string& a_pluginColonEdid, const std::string& a_edid, AmmoEntry& a_out, std::string* a_outReason) const
	{
		if (auto it = _jsonByPluginEdid.find(a_pluginColonEdid); it != _jsonByPluginEdid.end()) {
			a_out.caliber    = it->second.caliber;
			a_out.damageType = it->second.damageType;
			if (a_outReason) *a_outReason = "json exact match: '" + a_pluginColonEdid + "'";
			return true;
		}
		if (auto it = _jsonByEdid.find(a_edid); it != _jsonByEdid.end()) {
			a_out.caliber    = it->second.caliber;
			a_out.damageType = it->second.damageType;
			if (a_outReason) *a_outReason = "json EditorID-only match: '" + a_edid + "'";
			return true;
		}
		return false;
	}

	// =================================================================
	// Heuristic classification.
	//   Returns the human-readable reason for the chosen caliber so we can
	//   log it. If no heuristic matches, returns empty and a_out is left
	//   at its current (default) values.
	// =================================================================
	std::string AmmoClassifier::ApplyHeuristics(const RE::TESAmmo* a_ammo, const std::string& a_edidLower, AmmoEntry& a_out) const
	{
		// 1. EditorID exclude substrings (junk, missile, grenade, flamer, etc.)
		if (ContainsAny(a_edidLower, _heuristics.excludeSubs)) {
			a_out.caliber    = Caliber::Excluded;
			a_out.damageType = DamageType::Ballistic;
			return "exclude_substring matched in EditorID";
		}

		// 2. Projectile flag check.
		//    BGSProjectile is always formType=kPROJ; the type & flags are
		//    encoded in BGSProjectileData::flags (lower 16 bits = behavior
		//    flags, upper 16 bits = projectile type bits).
		//      bit  0 (0x00000001) = Hitscan         (regular bullets)
		//      bit  1 (0x00000002) = Explosion        (carries explosion data)
		//      bit 16 (0x00010000) = Type Missile    (DEFAULT for bullets too!)
		//      bit 17 (0x00020000) = Type Lobber     (thrown grenades)
		//      bit 18 (0x00040000) = Type Beam       (energy weapons)
		//      bit 19 (0x00080000) = Type Flame      (flamer)
		//      bit 20 (0x00100000) = Type Cone       (cryolator)
		//      bit 21 (0x00200000) = Type Barrier    (barriers)
		//      bit 22 (0x00400000) = Type Arrow      (arrows / spikes)
		//
		//    IMPORTANT: vanilla 10mm/.308/.45/.50/etc. bullets ARE Missile-type
		//    internally even though they're hitscan, so we must NOT exclude
		//    based on the Missile bit alone. The reliable signal is:
		//      - Has explosionType set AND not Hitscan -> real explosive
		//      - Type is Lobber / Flame / Cone / Barrier -> always exclude
		if (a_ammo && a_ammo->data.projectile) {
			const auto* proj = a_ammo->data.projectile;
			const std::uint32_t pflags = proj->data.flags;
			constexpr std::uint32_t kHitscan  = 1u << 0;
			constexpr std::uint32_t kLobber   = 1u << 17;
			constexpr std::uint32_t kBeam     = 1u << 18;
			constexpr std::uint32_t kFlame    = 1u << 19;
			constexpr std::uint32_t kCone     = 1u << 20;
			constexpr std::uint32_t kBarrier  = 1u << 21;
			const bool isHitscan    = (pflags & kHitscan) != 0;
			const bool hasExplosion = proj->data.explosionType != nullptr;

			if (pflags & kLobber)  { a_out.caliber = Caliber::Excluded; return "projectile-type Lobber (grenade)"; }
			if (pflags & kFlame)   { a_out.caliber = Caliber::Excluded; return "projectile-type Flame"; }
			if (pflags & kCone)    { a_out.caliber = Caliber::Excluded; return "projectile-type Cone (cryolator)"; }
			if (pflags & kBarrier) { a_out.caliber = Caliber::Excluded; return "projectile-type Barrier"; }

			// Real explosives (Missile Launcher, Fat Man, mininukes): non-hitscan
			// projectiles with an attached explosion.
			if (!isHitscan && hasExplosion) {
				a_out.caliber = Caliber::Excluded;
				return "non-hitscan projectile with explosionType (real missile/launcher)";
			}

			// Beam = energy. We mark damageType but continue with caliber heuristics.
			if (pflags & kBeam) {
				a_out.damageType = DamageType::Energy;
			}
		}

		// 3. Energy substrings -> mark damage type (continue with caliber check)
		if (ContainsAny(a_edidLower, _heuristics.energySubs)) {
			a_out.damageType = DamageType::Energy;
		}

		// 4. Caliber substrings (priority: large_rifle > shotgun > rifle > pistol)
		if (ContainsAny(a_edidLower, _heuristics.largeRifleSubs)) {
			a_out.caliber = Caliber::LargeRifle;
			return "large_rifle_substring matched";
		}
		if (ContainsAny(a_edidLower, _heuristics.shotgunSubs)) {
			a_out.caliber = Caliber::Shotgun;
			return "shotgun_substring matched";
		}
		if (ContainsAny(a_edidLower, _heuristics.rifleSubs)) {
			a_out.caliber = Caliber::Rifle;
			return "rifle_substring matched";
		}
		if (ContainsAny(a_edidLower, _heuristics.pistolSubs)) {
			a_out.caliber = Caliber::Pistol;
			return "pistol_substring matched";
		}

		// 5. Fallback by ammo damage value if available
		if (a_ammo) {
			const float dmg = a_ammo->data.damage;
			if (dmg >= 80.0f) {
				a_out.caliber = Caliber::LargeRifle;
				return std::format("damage>={} -> LargeRifle (dmg={})", 80, dmg);
			}
			if (dmg >= 30.0f) {
				a_out.caliber = Caliber::Rifle;
				return std::format("damage>={} -> Rifle (dmg={})", 30, dmg);
			}
		}

		return {};
	}

	// =================================================================
	// Per-ammo classification.
	//   The optional a_outReason is filled with a short explanation of which
	//   classification path was taken (override / json / heuristic / default).
	// =================================================================
	AmmoEntry AmmoClassifier::ClassifyOne(const RE::TESAmmo* a_ammo, std::string* a_outReason) const
	{
		AmmoEntry entry;
		if (!a_ammo) return entry;

		entry.formID       = a_ammo->GetFormID();
		entry.editorID     = a_ammo->GetFormEditorID() ? a_ammo->GetFormEditorID() : "";
		entry.sourcePlugin = GetSourcePluginName(a_ammo);
		entry.caliber      = Caliber::Pistol;
		entry.damageType   = DamageType::Ballistic;
		entry.autoClassified = true;
		entry.ammoDamage   = a_ammo->data.damage;

		// 1. User override
		const auto key = MakeOverrideKey(entry.sourcePlugin, entry.editorID);
		const auto* settings = Settings::GetSingleton();
		if (auto it = settings->ammoOverrides.find(key); it != settings->ammoOverrides.end()) {
			entry.caliber = static_cast<Caliber>(std::clamp(it->second, 0, 4));
			entry.autoClassified = false;
			entry.classificationReason = "user override (set via menu / INI)";
		}

		const std::string edidLower = ToLower(entry.editorID);

		// 2. JSON match (override-respecting)
		if (entry.autoClassified) {
			std::string jreason;
			if (ApplyJsonMatch(key, edidLower, entry, &jreason)) {
				entry.classificationReason = jreason;
				if (a_outReason) *a_outReason = jreason;
				return entry;
			}
		} else {
			// Override has set caliber. We still consult the JSON for damageType,
			// then re-apply the override caliber so override always wins.
			ApplyJsonMatch(key, edidLower, entry, nullptr);
			entry.caliber = static_cast<Caliber>(std::clamp(settings->ammoOverrides.at(key), 0, 4));
		}

		// 3. Heuristics
		if (entry.autoClassified) {
			std::string hreason = ApplyHeuristics(a_ammo, edidLower, entry);
			entry.classificationReason = hreason.empty()
				? std::string("default Pistol/Ballistic (no JSON entry, no heuristic match)")
				: ("heuristic: " + hreason);
		} else {
			// Override caliber wins; just compute damageType via the heuristic on
			// a throwaway copy so we don't pollute the entry.
			AmmoEntry tmp = entry;
			ApplyHeuristics(a_ammo, edidLower, tmp);
			entry.damageType = tmp.damageType;
		}
		if (a_outReason) *a_outReason = entry.classificationReason;

		return entry;
	}

	// =================================================================
	// Init / Recategorize
	// =================================================================
	void AmmoClassifier::Init()
	{
		std::unique_lock lk(_mutex);
		LoadJson();
		ScanAllAmmoImpl();
		_initialized.store(true);
	}

	void AmmoClassifier::Recategorize()
	{
		std::unique_lock lk(_mutex);
		ScanAllAmmoImpl();
		_initialized.store(true);
	}

	void AmmoClassifier::ScanAllAmmo()
	{
		std::unique_lock lk(_mutex);
		ScanAllAmmoImpl();
	}

	// Internal -- caller must hold lock.
	void AmmoClassifier::ScanAllAmmoImpl()
	{
		_cache.clear();

		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh) {
			logger::warn("[HSK] TESDataHandler not available; AmmoClassifier scan deferred");
			return;
		}

		const auto& ammoArray = dh->GetFormArray<RE::TESAmmo>();
		_cache.reserve(ammoArray.size());

		std::array<std::size_t, 5> catCounts{ 0, 0, 0, 0, 0 };

		const bool verbose = Settings::GetSingleton()->debugLogging;
		if (verbose) {
			logger::info("[HSK] === Ammo classification breakdown (debug=true) ===");
		}

		for (auto* ammo : ammoArray) {
			if (!ammo) continue;
			std::string reason;
			AmmoEntry entry = ClassifyOne(ammo, &reason);
			_cache.emplace(entry.formID, entry);
			const auto idx = static_cast<std::size_t>(entry.caliber);
			if (idx < catCounts.size()) catCounts[idx]++;

			if (verbose) {
				const auto* proj = ammo->data.projectile;
				const std::uint32_t pflags = proj ? proj->data.flags : 0u;
				const bool hasExpl = (proj && proj->data.explosionType != nullptr);
				const float dmg = ammo->data.damage;
				logger::info("[HSK]   {:<40s} {:<48s} cal={:<10s} dt={:<10s}  ammoDmg={:.1f}  proj=0x{:08X}{}  reason={}",
					entry.editorID,
					entry.sourcePlugin,
					CaliberToString(entry.caliber),
					(entry.damageType == DamageType::Energy ? "energy" : "ballistic"),
					dmg,
					pflags,
					(hasExpl ? " [+expl]" : ""),
					reason);
			}
		}

		logger::info("[HSK] Ammo scan complete: total={} excluded={} pistol={} shotgun={} rifle={} large_rifle={}",
			_cache.size(),
			catCounts[static_cast<std::size_t>(Caliber::Excluded)],
			catCounts[static_cast<std::size_t>(Caliber::Pistol)],
			catCounts[static_cast<std::size_t>(Caliber::Shotgun)],
			catCounts[static_cast<std::size_t>(Caliber::Rifle)],
			catCounts[static_cast<std::size_t>(Caliber::LargeRifle)]);
	}

	// =================================================================
	// Public lookups
	// =================================================================
	AmmoEntry AmmoClassifier::Classify(const RE::TESAmmo* a_ammo) const
	{
		if (!a_ammo) return {};
		std::shared_lock lk(_mutex);
		const auto id = a_ammo->GetFormID();
		if (auto it = _cache.find(id); it != _cache.end()) {
			return it->second;
		}
		// Lazy classification for ammo loaded after our scan (e.g. hotload).
		lk.unlock();
		std::unique_lock wlk(_mutex);
		AmmoEntry entry = ClassifyOne(a_ammo);
		_cache[id] = entry;
		return entry;
	}

	Caliber AmmoClassifier::GetCaliber(const RE::TESAmmo* a_ammo) const
	{
		return Classify(a_ammo).caliber;
	}

	DamageType AmmoClassifier::GetDamageType(const RE::TESAmmo* a_ammo) const
	{
		return Classify(a_ammo).damageType;
	}

	bool AmmoClassifier::IsExcluded(const RE::TESAmmo* a_ammo) const
	{
		return Classify(a_ammo).caliber == Caliber::Excluded;
	}

	std::vector<AmmoEntry> AmmoClassifier::GetAllEntries() const
	{
		std::shared_lock lk(_mutex);
		std::vector<AmmoEntry> out;
		out.reserve(_cache.size());
		for (const auto& [id, entry] : _cache) {
			out.push_back(entry);
		}
		std::sort(out.begin(), out.end(), [](const AmmoEntry& a, const AmmoEntry& b) {
			if (a.sourcePlugin != b.sourcePlugin) return a.sourcePlugin < b.sourcePlugin;
			return a.editorID < b.editorID;
		});
		return out;
	}

	// =================================================================
	// User overrides
	// =================================================================
	void AmmoClassifier::SetOverride(const std::string& a_pluginColonEdid, Caliber a_c, DamageType a_dt)
	{
		auto* s = Settings::GetSingleton();
		s->ammoOverrides[a_pluginColonEdid] = static_cast<int>(a_c);
		(void)a_dt;  // damageType is auto-derived
		// Persist only the ammo overrides section so we don't accidentally
		// flush pending in-memory changes the user hasn't clicked Save on.
		s->SaveAmmoOverridesOnly();
		Recategorize();
	}

	void AmmoClassifier::ClearOverride(const std::string& a_pluginColonEdid)
	{
		auto* s = Settings::GetSingleton();
		s->ammoOverrides.erase(a_pluginColonEdid);
		s->SaveAmmoOverridesOnly();
		Recategorize();
	}
}

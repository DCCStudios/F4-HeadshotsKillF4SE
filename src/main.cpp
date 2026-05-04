#include "Settings.h"
#include "AmmoClassifier.h"
#include "ActorClassifier.h"
#include "HitEventHandler.h"
#include "HitHook.h"
#include "HelmetHandler.h"
#include "HeadshotSpell.h"
#include "LegendaryTracker.h"
#include "PlayerFeedback.h"
#include "GameDefinitions.h"
#include "Menu.h"

#include <thread>

namespace Plugin
{
	static constexpr auto NAME    = "HeadshotsKillF4SE"sv;
	static constexpr auto VERSION = REL::Version{ 1, 0, 0 };
}

namespace
{
	std::atomic<bool> g_pluginEnabled{ true };
	std::atomic<bool> g_gameDataInitialized{ false };

	void InitializeLogging()
	{
		auto path = F4SE::log::log_directory();
		if (!path) {
			F4SE::stl::report_and_fail("Failed to find F4SE log directory"sv);
		}
		*path /= std::format("{}.log"sv, Plugin::NAME);

		std::shared_ptr<spdlog::logger> log;
#ifndef NDEBUG
		log = std::make_shared<spdlog::logger>(
			"global"s,
			std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));
		log->set_level(spdlog::level::trace);
#else
		log = std::make_shared<spdlog::logger>(
			"global"s,
			std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));
		log->set_level(spdlog::level::info);
#endif
		// IMPORTANT: do NOT flush_on(info) -- it forces a synchronous disk
		// write per log line, which during the verbose ammo scan
		// (250+ lines) would block the F4SE message-dispatch thread for
		// hundreds of milliseconds and prevent OTHER F4SE plugins'
		// kGameDataReady listeners from running in time for the framework
		// to render their menus at the main menu. Flush only on warnings.
		log->flush_on(spdlog::level::warn);
		spdlog::set_default_logger(std::move(log));
	}

	// Heavy work that needs game forms to be loaded. Runs once on
	// kGameDataReady. The expensive AmmoClassifier::Init() ammo scan is
	// dispatched onto a detached background thread so we return control to
	// F4SE's message dispatcher immediately.
	void OnGameDataReady()
	{
		if (g_gameDataInitialized.exchange(true)) {
			return;  // already done
		}

		logger::info("[HSK] kGameDataReady -- initializing game-form-dependent subsystems");

		// These are fast (form lookups by EditorID).
		HSK::ActorClassifier::GetSingleton()->Init();
		HSK::LegendaryTracker::GetSingleton()->Init();
		HSK::HeadshotSpell::GetSingleton()->Init();
		HSK::PlayerFeedback::GetSingleton()->Init();

		// Primary hit detection: trampoline hook on DoHitMe.
		// This is far more reliable than TESHitEvent (which misfires with
		// projectile-conversion mods and has layout differences across
		// engine versions). Install this FIRST so it catches hits as
		// soon as the game starts dispatching them.
		HSK::HitHook::Install();

		// Legacy fallback: TESHitEvent sink. Still installed in case the
		// DoHitMe REL::ID fails to resolve on some engine build.
		HSK::HitEventHandler::GetSingleton()->Install();
		HSK::ContainerChangeHandler::GetSingleton()->Install();
		HSK::HelmetHandler::GetSingleton()->ResolveEngineFunctions();

		// Heavy ammo scan -- kick onto a background thread so we don't block
		// the rest of F4SE's listener chain (which includes other plugins'
		// menu registrations, etc.). The classifier guards with its own
		// shared_mutex; reading TESForm arrays after data is loaded is safe
		// for our read-only iteration.
		std::thread([] {
			try {
				HSK::AmmoClassifier::GetSingleton()->Init();
			} catch (const std::exception& e) {
				logger::error("[HSK] Background AmmoClassifier::Init threw: {}", e.what());
			} catch (...) {
				logger::error("[HSK] Background AmmoClassifier::Init threw unknown exception");
			}
		}).detach();
	}

	void OnPostLoadGame()
	{
		if (!g_pluginEnabled) return;
		logger::info("[HSK] kPostLoadGame -- refreshing classifiers (settings preserved in-memory)");

		// Intentionally do NOT call Settings::Load() here. The in-memory
		// Settings is the source of truth for the session; loading a save
		// must not silently throw away pending unsaved menu edits. The user
		// can use the "Reload from disk" button in the menu to manually
		// re-read the INI if they hand-edited it outside the game.
		HSK::ActorClassifier::GetSingleton()->Init();

		// Ammo classification is NOT re-run here. The form array doesn't
		// change between loads (same mod list), and the lazy fallback in
		// Classify() handles any ammo not yet in the cache. Re-scans are
		// only triggered by Init() at kGameDataReady and by explicit user
		// actions in the UI (Re-scan button, override changes).

		// Safety net for players who saved mid-combat with a follower
		// helmet knocked off.  Combat state from the previous session
		// is not carried over, so the combat-end watcher may never trigger.
		// Running the restore here ensures followers get their helmets back.
		HSK::HelmetHandler::GetSingleton()->RestoreFollowerHelmets();
	}

	void OnNewGame()
	{
		if (!g_pluginEnabled) return;
		logger::info("[HSK] kNewGame -- refreshing classifiers (settings preserved in-memory)");

		HSK::ActorClassifier::GetSingleton()->Init();
	}

	void MessageCallback(F4SE::MessagingInterface::Message* msg)
	{
		if (!msg) return;
		switch (msg->type) {
		case F4SE::MessagingInterface::kGameDataReady: OnGameDataReady(); break;
		case F4SE::MessagingInterface::kPostLoadGame:  OnPostLoadGame();  break;
		case F4SE::MessagingInterface::kNewGame:       OnNewGame();       break;
		default: break;
		}
	}
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* F4SE, F4SE::PluginInfo* info)
{
	info->infoVersion = F4SE::PluginInfo::kVersion;
	info->name        = Plugin::NAME.data();
	info->version     = 1;

	if (F4SE->IsEditor()) {
		return false;
	}

	const auto ver = F4SE->RuntimeVersion();
	if (ver < F4SE::RUNTIME_1_10_162) {
		return false;
	}
	return true;
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* F4SE)
{
	InitializeLogging();
	logger::info("{} v{}.{}.{} loading", Plugin::NAME,
		Plugin::VERSION[0], Plugin::VERSION[1], Plugin::VERSION[2]);

	HSK::g_runtimeVersion = F4SE->RuntimeVersion();
	logger::info("[HSK] runtime version: {}", HSK::g_runtimeVersion.string());

	F4SE::Init(F4SE);

	// Reserve trampoline space for our DoHitMe hook. 64 bytes is plenty
	// for a single write_call<5> (14 bytes used). This MUST happen early
	// (in F4SEPlugin_Load) because F4SE may only accept allocation
	// requests before the game data is loaded.
	F4SE::AllocTrampoline(64);

	// ---- LOAD-TIME (do everything that doesn't require game forms) -----
	//
	// Per the F4SE Menu Framework example plugin (and to avoid being the
	// reason other Menu Framework mods don't show at the main menu): we
	// register the menu HERE, in F4SEPlugin_Load, BEFORE the main menu
	// is rendered. Registering inside kGameDataReady means the framework's
	// section tree is empty until the message fires, which produces an
	// empty UI tree at the main menu.
	//
	// Settings::Load() is just an INI/JSON parse and needs no game forms,
	// so it's safe (and necessary, so render functions read correct
	// values) to call here too.

	try {
		HSK::Settings::GetSingleton()->Load();
	} catch (const std::exception& e) {
		logger::error("[HSK] Settings::Load threw: {}", e.what());
	} catch (...) {
		logger::error("[HSK] Settings::Load threw unknown exception");
	}

	try {
		HSK::Menu::Register();
	} catch (const std::exception& e) {
		logger::error("[HSK] Menu::Register threw: {}", e.what());
	} catch (...) {
		logger::error("[HSK] Menu::Register threw unknown exception");
	}

	// ---- DATA-READY (everything that requires loaded game forms) -------
	auto* messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(MessageCallback)) {
		logger::critical("[HSK] Failed to register messaging listener");
		return false;
	}

	logger::info("[HSK] Plugin loaded; menu registered. Waiting for kGameDataReady to scan ammo / install hit hooks.");
	return true;
}

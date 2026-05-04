#include "HitEventHandler.h"
#include "HeadshotLogic.h"
#include "HelmetHandler.h"
#include "Settings.h"

namespace HSK
{
	bool HitEventHandler::Install()
	{
		if (_installed) return true;
		if (!RegisterHitEventSink(this)) {
			logger::error("[HSK] HitEventHandler::Install -- failed to register TESHitEvent sink");
			return false;
		}
		_installed = true;
		logger::info("[HSK] HitEventHandler installed (TESHitEvent sink registered)");
		return true;
	}

	void HitEventHandler::Uninstall()
	{
		if (!_installed) return;
		UnregisterHitEventSink(this);
		_installed = false;
	}

	RE::BSEventNotifyControl HitEventHandler::ProcessEvent(
		const RE::TESHitEvent&        a_event,
		RE::BSTEventSource<RE::TESHitEvent>*)
	{
		// Fast path: master toggle
		const auto* settings = Settings::GetSingleton();
		if (!settings->enabled) {
			return RE::BSEventNotifyControl::kContinue;
		}

		// Defer all classification + work to HeadshotLogic. We catch any exceptions
		// so a buggy classification doesn't propagate into the engine's event pump.
		try {
			HeadshotLogic::EvaluateHit(a_event);
		} catch (const std::exception& e) {
			logger::error("[HSK] HeadshotLogic threw: {}", e.what());
		} catch (...) {
			logger::error("[HSK] HeadshotLogic threw unknown exception");
		}

		return RE::BSEventNotifyControl::kContinue;
	}

	// =================================================================
	// ContainerChangeHandler -- helmet auto-reequip
	// =================================================================
	bool ContainerChangeHandler::Install()
	{
		if (_installed) return true;
		if (!RegisterContainerChangedEventSink(this)) {
			logger::warn("[HSK] ContainerChangeHandler -- failed to register TESContainerChangedEvent sink");
			return false;
		}
		_installed = true;
		logger::info("[HSK] ContainerChangeHandler installed");
		return true;
	}

	void ContainerChangeHandler::Uninstall()
	{
		if (!_installed) return;
		UnregisterContainerChangedEventSink(this);
		_installed = false;
	}

	RE::BSEventNotifyControl ContainerChangeHandler::ProcessEvent(
		const RE::TESContainerChangedEvent_Compat& a_event,
		RE::BSTEventSource<RE::TESContainerChangedEvent_Compat>*)
	{
		// Only care about items entering the player's inventory (0x14)
		if (a_event.newContainerFormID != 0x14) {
			return RE::BSEventNotifyControl::kContinue;
		}

		try {
			HelmetHandler::GetSingleton()->OnPlayerItemAdded(a_event.baseObjFormID);
		} catch (...) {}

		return RE::BSEventNotifyControl::kContinue;
	}
}

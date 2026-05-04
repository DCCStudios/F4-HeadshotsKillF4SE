#pragma once

#include "PCH.h"
#include "GameDefinitions.h"

namespace HSK
{
	class HitEventHandler :
		public RE::BSTEventSink<RE::TESHitEvent>
	{
	public:
		static HitEventHandler* GetSingleton()
		{
			static HitEventHandler singleton;
			return &singleton;
		}

		// Register sink with the engine. Idempotent.
		bool Install();

		// Unregister sink (for shutdown).
		void Uninstall();

		// BSTEventSink<TESHitEvent>
		RE::BSEventNotifyControl ProcessEvent(
			const RE::TESHitEvent&        a_event,
			RE::BSTEventSource<RE::TESHitEvent>* a_source) override;

	private:
		HitEventHandler() = default;
		~HitEventHandler() = default;
		HitEventHandler(const HitEventHandler&) = delete;
		HitEventHandler& operator=(const HitEventHandler&) = delete;

		bool _installed{ false };
	};

	// Listens for items entering the player's inventory (for helmet auto-reequip).
	class ContainerChangeHandler :
		public RE::BSTEventSink<RE::TESContainerChangedEvent_Compat>
	{
	public:
		static ContainerChangeHandler* GetSingleton()
		{
			static ContainerChangeHandler singleton;
			return &singleton;
		}

		bool Install();
		void Uninstall();

		RE::BSEventNotifyControl ProcessEvent(
			const RE::TESContainerChangedEvent_Compat& a_event,
			RE::BSTEventSource<RE::TESContainerChangedEvent_Compat>* a_source) override;

	private:
		ContainerChangeHandler() = default;
		bool _installed{ false };
	};
}

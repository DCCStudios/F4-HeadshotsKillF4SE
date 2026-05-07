#pragma once

#include "F4SEMenuFramework.h"

namespace HSK::Menu
{
	// Register all menu items with F4SE Menu Framework. Idempotent.
	void Register();

	// Per-page render callbacks.
	void __stdcall RenderGeneral();
	void __stdcall RenderChances();
	void __stdcall RenderCaliberMods();
	void __stdcall RenderHelmet();
	void __stdcall RenderAmmoBrowser();
	void __stdcall RenderRaceBlacklist();
	void __stdcall RenderLegendary();
	void __stdcall RenderPlayerFeedback();
	void __stdcall RenderDebug();
	void __stdcall RenderAbout();

	namespace State
	{
		inline bool   initialized{ false };

		// "Pending changes" tracking.
		//   Any non-ammo-override edit (sliders, toggles, text fields) sets
		//   `dirty = true` but does NOT touch the disk. The user must click
		//   Save to persist, or Discard to revert from disk.
		//   Ammo overrides bypass this and auto-save on change (via
		//   AmmoClassifier::SetOverride / ClearOverride).
		inline bool        dirty{ false };
		inline std::string saveStatusMsg;
		inline float       saveStatusTimer{ 0.0f };

		// Ammo browser
		inline char  ammoFilter[128]{ "" };
		// Filter mode: -1 = show all categories, 0..4 = only show that caliber.
		inline int   ammoCategoryFilter{ -1 };
		// Display mode: 0 = grouped by source plugin (default), 1 = flat alphabetical
		inline int   ammoViewMode{ 0 };
		// Show only entries the user has overridden.
		inline bool  ammoOnlyShowOverrides{ false };
		inline bool  ammoBrowserNeedsRefresh{ true };

		// Race blacklist input
		inline char  raceBlacklistInput[256]{ "" };
		// Feral ghoul race pattern list (comma-separated substrings of TESRace EditorID)
		inline char  feralGhoulRacePatternsInput[256]{ "" };
		// Keyword immune input
		inline char  keywordImmuneInput[256]{ "" };

		// Player feedback inputs
		inline char  tinnitusSoundFileInput[128]{ "" };
		inline char  concussionIModInput[128]{ "" };
		inline char  impactFlashIModInput[128]{ "" };
		inline char  greyscaleIModInput[128]{ "" };
	}

	// Helpers
	void DrawSaveStatus();
	// Mark settings as dirty; replaces the old auto-save behavior.
	void MarkDirty();
	// Persist the live in-memory settings to disk and clear the dirty flag.
	void CommitPending();
	// Reload the live settings from disk, throwing away anything in memory
	// that hasn't been saved. Also refreshes UI input buffers.
	void DiscardPending();
	// Render the persistent "Unsaved changes / Save / Discard" toolbar at
	// the top of every settings page. Returns true if anything was clicked.
	void RenderSaveBar();
}

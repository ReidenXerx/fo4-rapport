#pragma once

#include "Orders.h"

namespace RP
{
	// The face during a scene.
	//
	// Why this exists at all: across the whole AAF install there are ten mfgSet
	// references, and the pack that was playing when the owner first watched a
	// Rapport scene has exactly one of them -- Sleep_EyesClosed. The faces are
	// blank because nothing asks them to be anything else. That is a content gap
	// every animation pack shares, and a framework is the right place to fill it.
	//
	// Unlike the overlays this layer depends on nobody. A morph id is an index
	// into the engine's own facial morph table, so there is no texture to ship
	// and no mod whose assets we are borrowing.
	//
	// The one thing it must never do is leave a face behind. An expression is
	// applied with lock="true", which holds it against anything else that would
	// move it, so a scene that ends badly -- or a save taken in the middle of one
	// -- would otherwise leave a settler wearing it for the rest of the
	// playthrough. Everyone currently wearing one is therefore written into the
	// save, and cleared on the next load.
	class Expressions
	{
	public:
		[[nodiscard]] static Expressions& GetSingleton() noexcept;

		void Load();
		[[nodiscard]] bool Enabled() const noexcept { return _enabled; }

		// A scene of ours began. Duration is what we asked AAF for, and the steps
		// below are placed as fractions of it.
		void OnSceneStarted(std::uint32_t a_first, std::uint32_t a_second, float a_durationSeconds);

		// The tags of one animation, as AAF reported them. They decide which
		// progression the scene gets -- kissing is not the same face as sex.
		void NoteTags(std::string_view a_tags);

		void OnSceneEnded();

		// Called on every poll. Queues the next expression when its moment has
		// come, and the clearing one when the afterglow is over.
		void Pump();

		// ---- the save --------------------------------------------------------
		// Form ids currently wearing a Rapport expression. Small, and only
		// non-empty during and just after a scene -- which is exactly when a save
		// is most likely to strand one.
		[[nodiscard]] std::vector<std::uint32_t> Wearing() const;
		void RestoreWearing(std::vector<std::uint32_t> a_wearing);

		// Queues the clearing set for everyone on the list and empties it. Used on
		// load and by the panic switch.
		void ClearEveryone(std::string_view a_why);

		void Reset();

		[[nodiscard]] static std::filesystem::path ConfigPath();

	private:
		struct Step
		{
			float       at{ 0.0f };   // fraction of the scene's length
			std::string set;
		};

		void Queue(std::string_view a_setID);

		mutable std::mutex _lock;

		bool        _enabled{ true };
		float       _dazedSeconds{ 20.0f };
		std::string _clearSet{ "Rapport_Clear" };
		std::string _afterSet{ "Rapport_Dazed" };
		std::string _kissSet{ "Rapport_Kiss" };
		std::vector<Step>        _steps;
		std::vector<std::string> _kissOnlyTags;

		// ---- the scene in progress ------------------------------------------
		std::uint32_t _first{ 0 };
		std::uint32_t _second{ 0 };
		float         _duration{ 0.0f };
		std::chrono::steady_clock::time_point _startedAt{};
		std::chrono::steady_clock::time_point _endedAt{};
		bool          _running{ false };
		bool          _dazing{ false };
		bool          _clearPending{ false };   // a save was made mid-scene
		std::size_t   _nextStep{ 0 };
		std::string   _tags;
		bool          _sawSexTag{ false };

		std::vector<std::uint32_t> _wearing;
	};
}

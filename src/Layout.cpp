#include "Layout.h"

namespace RP::Layout
{
#ifndef RP_RUNTIME_DATABASE
	void Check() {}
	bool Ok() noexcept { return true; }
#else
	namespace
	{
		enum class State
		{
			kUnchecked,
			kGood,
			kBad
		};
		std::atomic<State> g_state{ State::kUnchecked };

		struct Run
		{
			std::vector<std::string> problems;
		};

		// A form's type byte, or -1 when the read faults: a pointer read at the wrong offset
		// is exactly what this is here to catch, so it must not crash the game doing so.
		int SafeFormType(const RE::TESForm* a_form) noexcept
		{
			__try {
				return static_cast<int>(a_form->GetFormType());
			} __except (1) {
				return -1;
			}
		}

		// The checks, run inside Guarded: no object with a destructor may live in the __try frame.
		bool Guarded(void (*a_fn)(void*), void* a_arg) noexcept
		{
			__try {
				a_fn(a_arg);
				return true;
			} __except (1) {
				return false;
			}
		}

		void Measure(void* a_run)
		{
			auto&      run = *static_cast<Run*>(a_run);
			const auto is = [&](const RE::TESForm* a_form, RE::ENUM_FORM_ID a_type, std::string_view a_what) {
				if (!a_form) {
					run.problems.push_back(std::format("{} is empty", a_what));
					return false;
				}
				const auto type = SafeFormType(a_form);
				if (type != std::to_underlying(a_type)) {
					run.problems.push_back(std::format("{} is not the form it should be (type {})", a_what, type));
					return false;
				}
				return true;
			};

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!is(player, RE::ENUM_FORM_ID::kACHR, "the player")) {
				return;
			}
			is(player->GetNPC(), RE::ENUM_FORM_ID::kNPC_, "the player's base record");
			// ActorScan.cpp and PapyrusLink.cpp read these two on every candidate.
			is(player->race, RE::ENUM_FORM_ID::kRACE, "Actor::race");
			if (const auto life = static_cast<std::uint32_t>(player->lifeState); life > 8) {
				run.problems.push_back(std::format("Actor::lifeState reads {} for the player", life));
			}
			// The alias entries ActorScan reads (instancedPackages, quest), on the player, who is in
			// several vanilla quests' aliases at any point past the opening.
			if (!player->extraList) {
				run.problems.push_back("Actor::extraList is empty for the player");
			} else if (const auto aliases = player->extraList->GetByType<RE::ExtraAliasInstanceArray>()) {
				std::size_t checked = 0;
				for (const auto& instance : aliases->aliasArray) {
					if (checked++ == 8) {
						break;
					}
					if (instance.quest && !is(instance.quest, RE::ENUM_FORM_ID::kQUST, "ExtraAliasInstanceArray quest")) {
						break;
					}
				}
			}
			// ActorScan's source: the process lists' handles, each an actor.
			if (const auto lists = RE::ProcessLists::GetSingleton()) {
				for (const auto* list : { &lists->highActorHandles, &lists->middleHighActorHandles }) {
					std::size_t checked = 0;
					for (const auto& handle : *list) {
						if (checked++ == 8) {
							break;
						}
						if (auto ptr = handle.get(); ptr && !is(ptr.get(), RE::ENUM_FORM_ID::kACHR, "ProcessLists handle")) {
							break;
						}
					}
				}
			} else {
				run.problems.push_back("ProcessLists is missing");
			}
			// The stall watchdog's pause test (PapyrusLink.cpp).
			if (const auto main = RE::Main::GetSingleton()) {
				if (const auto frozen = *reinterpret_cast<const std::uint8_t*>(&main->freezeTime); frozen > 1) {
					run.problems.push_back(std::format("Main::freezeTime reads {}", frozen));
				}
			}
			if (const auto ui = RE::UI::GetSingleton()) {
				if (ui->menuMode > 1000 || ui->freezeFramePause > 1000) {
					run.problems.push_back(std::format("UI counters read menuMode {} freezeFramePause {}", ui->menuMode,
						ui->freezeFramePause));
				}
			}
		}
	}

	void Check()
	{
		if (g_state.load() != State::kUnchecked) {
			return;
		}
		Run run;
		if (!Guarded(&Measure, &run)) {
			run.problems.push_back("a read faulted");
		}
		if (run.problems.empty()) {
			g_state.store(State::kGood);
			logger::info("layout: every member Rapport reads directly checks out on this game version");
			return;
		}
		g_state.store(State::kBad);
		std::string all;
		for (const auto& p : run.problems) {
			all += (all.empty() ? "" : "; ") + p;
		}
		logger::critical("layout: this game version lays out a class differently than Rapport expects ({}) - "
						 "Rapport stays OFF for this session rather than read the wrong memory",
			all);
	}

	bool Ok() noexcept
	{
		return g_state.load() == State::kGood;
	}
#endif
}

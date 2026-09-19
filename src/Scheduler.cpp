#include "Scheduler.h"

#include "Candidates.h"
#include "Aftermath.h"
#include "Config.h"
#include "Expressions.h"
#include "Ledger.h"
#include "Scenarios.h"
#include "PapyrusLink.h"
#include "Pairing.h"

namespace RP
{
	Scheduler& Scheduler::GetSingleton() noexcept
	{
		static Scheduler singleton;
		return singleton;
	}

	void Scheduler::Start()
	{
		if (_started.exchange(true)) {
			return;
		}
		_thread = std::jthread{ [this](std::stop_token a_stop) { ThreadMain(a_stop); } };
		logger::info("scheduler started (tick {}s)", Config::GetSingleton().tickSeconds);
	}

	void Scheduler::OnLoad()
	{
		const auto& config = Config::GetSingleton();
		{
			std::scoped_lock lock{ _clockLock };
			_nextTickAt = std::chrono::steady_clock::now() + std::chrono::seconds{ config.warmupSeconds };
		}
		_inSession.store(true);
		_panicked = false;
		PapyrusLink::GetSingleton().RequireHandshake();
		logger::info("session ready — warming up for {}s before the first tick", config.warmupSeconds);
	}

	void Scheduler::OnUnload() noexcept
	{
		_inSession.store(false);
	}

	void Scheduler::Stop() noexcept
	{
		_thread.request_stop();
	}

	void Scheduler::ThreadMain(std::stop_token a_stop)
	{
		const auto tick = std::chrono::seconds{ Config::GetSingleton().tickSeconds };

		while (!a_stop.stop_requested()) {
			// Wake often enough to notice a stop request without holding a long sleep.
			std::this_thread::sleep_for(std::chrono::milliseconds{ 250 });
			if (a_stop.stop_requested()) {
				return;
			}
			if (!_inSession.load()) {
				continue;
			}

			{
				std::scoped_lock lock{ _clockLock };
				if (std::chrono::steady_clock::now() < _nextTickAt) {
					continue;
				}
			}

			if (_passInFlight.exchange(true)) {
				// The previous pass has not finished yet. Skipping is correct — a
				// tick we cannot afford is a tick we do not take — but say so once
				// per pass, not on every 250 ms wake-up. The first run after a load
				// waited 18 seconds for the main thread and wrote 74 identical
				// warnings.
				if (!_skipReported.exchange(true)) {
					logger::warn("tick due while the previous pass is still in flight — waiting for it");
				}
				continue;
			}
			_skipReported.store(false);

			// The next deadline is set when this pass FINISHES, not here. A task
			// posted to the main thread runs whenever the game gets round to it,
			// and scheduling from the post time turned a 19-second delay into two
			// passes one second apart.

			PostSlice();
		}
	}

	void Scheduler::PostSlice()
	{
		const auto task = F4SE::GetTaskInterface();
		if (!task) {
			_passInFlight.store(false);
			return;
		}

		task->AddTask([this]() {
			const auto& config = Config::GetSingleton();

			if (_scan.Slices() == 0 && _scan.Size() == 0) {
				_scan.Begin(config.scanRadius);
			}

			const auto started = std::chrono::steady_clock::now();
			const bool done = _scan.Step(config.frameBudgetMs);
			const auto elapsed =
				std::chrono::duration<double, std::milli>{ std::chrono::steady_clock::now() - started }.count();

			_passMs += elapsed;
			_worstSliceMs = (std::max)(_worstSliceMs, elapsed);
			_worstPassSliceEver = (std::max)(_worstPassSliceEver, elapsed);
			_totalSliceMs += elapsed;
			++_sliceCount;

			if (done) {
				FinishPass();
			} else {
				PostSlice();
			}
		});
	}

	void Scheduler::FinishPass()
	{
		const auto& counters = _scan.Counters();
		++_ticks;

		logger::info(
			"tick {}: {} actors -> {} candidates in {} slice(s), {:.3f} ms total, worst slice {:.3f} ms "
			"(stale {}, unloaded {}, child {}, dead {}, combat {}, out of range {}, race {}, dialogue {}, quest {})",
			_ticks, _scan.Size(), counters.candidates, _scan.Slices(), _passMs, _worstSliceMs,
			counters.stale, counters.notLoaded, counters.child, counters.dead, counters.inCombat,
			counters.outOfRange, counters.raceNotAllowed, counters.inDialogue, counters.questDriven);

		if (Config::GetSingleton().verbose && !_scan.RejectedRaces().empty()) {
			std::vector<std::pair<std::uint32_t, std::uint32_t>> byCount{
				_scan.RejectedRaces().begin(), _scan.RejectedRaces().end()
			};
			std::ranges::sort(byCount, [](const auto& a, const auto& b) { return a.second > b.second; });

			std::string census;
			for (std::size_t i = 0; i < byCount.size() && i < 8; ++i) {
				if (!census.empty()) {
					census += ", ";
				}
				census += std::format("{:08X} x{}", byCount[i].first, byCount[i].second);
			}
			logger::info("   races rejected ({} distinct): {}", byCount.size(), census);
		}

		// Dry run: rank the pairs and say what would happen. Nothing below touches an
		// actor, reserves anyone, or starts anything — that is a later milestone, and
		// the whole point of this one is to read the decisions before they are real.
		{
			auto& link = PapyrusLink::GetSingleton();

			std::vector<RE::Actor*> candidates;
			candidates.reserve(_scan.Candidates().size());
			const auto& ledger = Ledger::GetSingleton();
			const auto  cooldown = Config::GetSingleton().cooldownHours;

			std::uint32_t benched = 0;
			std::uint32_t resting = 0;
			for (const auto& handle : _scan.Candidates()) {
				const auto actor = handle.get();
				if (!actor) {
					continue;
				}
				// AAF will refuse anyone still carrying its busy flag, and the
				// scoring does not know that. Ranking them anyway means the best
				// pair is one that cannot be started, and the tick is wasted.
				if (link.IsActorBusy(actor->GetFormID())) {
					++benched;
					continue;
				}
				// Stand-in policy, reading the framework's facts. The ledger says
				// when; this line is the only thing that decides "too soon".
				if (cooldown > 0.0f && ledger.HoursSinceScene(actor->GetFormID()) < cooldown) {
					++resting;
					continue;
				}
				candidates.push_back(actor.get());
			}
			if (benched > 0) {
				logger::info("   {} candidate(s) benched: AAF still has them flagged busy", benched);
			}
			if (resting > 0) {
				logger::info("   {} candidate(s) within the {:.0f}-hour cooldown", resting, cooldown);
			}

			const auto ranked = RankPairs(
				candidates, _scan.ObserverPositions(), Config::GetSingleton().Weights(), 3);

			const auto& weights = Config::GetSingleton().Weights();

			link.CheckWatchdog();
			link.CheckBridgeAlive();

			// Expiry is checked on the tick rather than on a timer of its own: it
			// is two comparisons per standing overlay, and the tick is already the
			// place that knows what time it is.
			// The way out, taken once per load. It runs here rather than at startup
			// because the ledger is empty until the save has been read, and taking
			// overlays off requires knowing which ones are on.
			if (Config::GetSingleton().panicClear && !_panicked) {
				_panicked = true;
				logger::warn(
					"PanicClear is set in Rapport.ini: taking everything back off. "
					"Set it to 0 again once this save is clean, or it will run on every load.");
				Aftermath::GetSingleton().RemoveEverything("PanicClear is set");
				Expressions::GetSingleton().ClearEveryone("PanicClear is set");
				Ledger::GetSingleton().Clear();
			}

			Aftermath::GetSingleton().Tick(_scan.LoadedIDs());

			// Published every pass, whatever the stand-in does with it. An addon polls
			// on its own clock and must see what this pass measured, including that
			// there is nothing worth acting on.
			Candidates::GetSingleton().Publish(ranked);

			if (ranked.empty()) {
				logger::info("   no viable pair ({} candidates, {} watching)",
					candidates.size(), _scan.ObserverPositions().size());
			} else {
				logger::info("   best pair {} the {:.2f} bar ({} candidates)",
					ranked.front().score >= weights.minimumScore ? "CLEARS" : "misses",
					weights.minimumScore, candidates.size());
				for (const auto& pair : ranked) {
					logger::info(
						"   {} {} + {} — score {:.2f} (apart {:.0f}, faction {}, {}, {}, "
						"observers {}{})",
						"would pair",
						pair.first->GetDisplayFullName(), pair.second->GetDisplayFullName(),
						pair.score, pair.signals.distance,
						pair.signals.sharedFaction ? "shared" : "different",
						pair.signals.interior ? "indoors" : "outdoors",
						pair.signals.night ? "night" : "day",
						pair.signals.observers,
						pair.signals.playerNear ? ", player watching" : "");
				}
			}

			// The only place anything is acted on. A dry run reports and stops here;
			// this is a stand-in for the Chemistry addon, and it now yields the moment
			// a real addon says it is taking over.
			//
			// The check is a call and not a config flag on purpose: whether Rapport
			// should decide depends on which mods are INSTALLED, and an ini that has
			// to be edited to match is an ini that will be wrong. Two things starting
			// scenes is the failure being prevented here.
			const auto& settings = Config::GetSingleton();
			if (Candidates::GetSingleton().StoodDown()) {
				if (_ticks % 10 == 0) {
					logger::info("   an addon owns the decision; the stand-in is standing down");
				}
			} else if (!settings.dryRun && !ranked.empty() &&
					   ranked.front().score >= weights.minimumScore) {
				if (link.Busy()) {
					logger::info("   holding: a scene is already running");
				} else if (!link.Ready()) {
					logger::info("   holding: the bridge is not ready");
				} else {
					// The scenario named here is the STAND-IN'S choice, not the framework's.
					// Rapport executes a story an addon asks for; this branch is only
					// pretending to be Chemistry until Chemistry exists, and it says so
					// a few lines above.
					const auto& scenario = settings.standInScenario;
					const auto  seconds =
						scenario.empty()
							? settings.sceneSeconds
							: (std::max)(settings.sceneSeconds,
							             Scenarios::GetSingleton().SecondsFor(scenario));

					link.RequestScene(ranked.front().first, ranked.front().second, seconds, scenario);
				}
			}
		}

		if (_ticks % 10 == 0) {
			PapyrusLink::GetSingleton().LogHealth();
			logger::info(
				"budget so far: {} slices, {:.3f} ms average, {:.3f} ms worst",
				_sliceCount, _sliceCount ? _totalSliceMs / static_cast<double>(_sliceCount) : 0.0,
				_worstPassSliceEver);
		}

		_passMs = 0.0;
		_worstSliceMs = 0.0;
		_scan = {};

		{
			std::scoped_lock lock{ _clockLock };
			_nextTickAt = std::chrono::steady_clock::now() +
			              std::chrono::seconds{ Config::GetSingleton().tickSeconds };
		}
		_passInFlight.store(false);
	}
}

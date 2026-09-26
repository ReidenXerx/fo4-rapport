#include "DebugTriggers.h"

#include "ActorScan.h"
#include "Config.h"
#include "Ledger.h"
#include "Orientation.h"
#include "Pairing.h"
#include "PapyrusLink.h"
#include "Scenarios.h"

namespace RP::DebugTriggers
{
	namespace
	{
		constexpr std::uint32_t kPlayer = 0x14;
		constexpr float         kPi = 3.14159265358979f;

		[[nodiscard]] std::string Name(RE::Actor* a_actor)   // not const: GetDisplayFullName is not
		{
			if (!a_actor) {
				return "nobody";
			}
			const char* name = RP::Compat::DisplayName(a_actor);
			return name && *name ? std::string{ name } : std::format("{:08X}", a_actor->GetFormID());
		}

		// Degrees from a_from's heading to a_to: 0 straight ahead, negative to the
		// left. Yaw 0 is +Y and runs clockwise -- the same arithmetic F4MCP's `aim`
		// verb measures facing with in game.
		[[nodiscard]] float HeadingDegrees(const RE::TESObjectREFR* a_from, const RE::TESObjectREFR* a_to)
		{
			const auto f = a_from->GetPosition();
			const auto t = a_to->GetPosition();
			float      delta = std::atan2(t.x - f.x, t.y - f.y) - a_from->data.angle.z;
			while (delta > kPi) {
				delta -= 2.0f * kPi;
			}
			while (delta <= -kPi) {
				delta += 2.0f * kPi;
			}
			return delta * 180.0f / kPi;
		}

		[[nodiscard]] float Distance(const RE::TESObjectREFR* a_a, const RE::TESObjectREFR* a_b)
		{
			const auto p = a_a->GetPosition();
			const auto q = a_b->GetPosition();
			const auto dx = p.x - q.x;
			const auto dy = p.y - q.y;
			const auto dz = p.z - q.z;
			return std::sqrt(dx * dx + dy * dy + dz * dz);
		}

		// In the part of the world the player is in -- ActorScan's InPlayersWorld, whose
		// comment has the scar: an interior just left stays loaded, 3D and all, and
		// distances taken between two interiors that do not share coordinates put
		// somebody in another building "right in front of you".
		[[nodiscard]] bool InPlayersWorld(RE::Actor& a_actor, RE::PlayerCharacter& a_player)
		{
			auto* cell = a_actor.GetParentCell();
			auto* here = a_player.GetParentCell();
			if (!cell || !here) {
				return false;
			}
			if (cell == here) {
				return true;
			}
			return !cell->IsInterior() && !here->IsInterior() && cell->worldSpace && cell->worldSpace == here->worldSpace;
		}

		// The rules no button skips: "" when this actor may take part at all.
		[[nodiscard]] std::string HardRule(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return "is nobody";
			}
			if (a_actor->GetFormID() == kPlayer) {
				return a_actor->IsInCombat() ? std::string{ "(you) are fighting" } : std::string{};
			}
			if (a_actor->IsChild()) {
				return "is a child - never";
			}
			if (a_actor->IsDead(true)) {
				return "is dead";
			}
			if (!a_actor->Get3D()) {
				return "is not loaded";
			}
			if (a_actor->IsInCombat()) {
				return "is fighting";
			}
			if (!Config::GetSingleton().IsRaceAllowed(a_actor->race)) {
				return "is of a race Rapport does not dress (races.json)";
			}
			if (PapyrusLink::GetSingleton().IsActorBusy(a_actor->GetFormID())) {
				return "still carries AAF's busy flag";
			}
			return {};
		}

		// Soft gate: the stand-in's cooldown. "" when this actor is not resting.
		[[nodiscard]] std::string Resting(RE::Actor* a_actor)
		{
			const auto cooldown = Config::GetSingleton().cooldownHours;
			if (!a_actor || a_actor->GetFormID() == kPlayer || cooldown <= 0.0f) {
				return {};
			}
			const auto since = Ledger::GetSingleton().HoursSinceScene(a_actor->GetFormID());
			if (since < cooldown) {
				return std::format("{} had a scene {:.1f} game hours ago - the cooldown is {:.0f}", Name(a_actor),
					since, cooldown);
			}
			return {};
		}

		// One scan, exactly the stand-in's: who is eligible, and who can see.
		[[nodiscard]] ActorScan Scan()
		{
			ActorScan scan;
			scan.Begin(Config::GetSingleton().scanRadius);
			while (!scan.Step(1000.0f)) {
			}
			return scan;
		}

		[[nodiscard]] std::string Describe(const ScoredPair& a_pair)
		{
			const auto& s = a_pair.signals;
			return std::format("apart {:.0f}, faction {}, {}, {}, observers {}{}", s.distance,
				s.sharedFaction ? "shared" : "different", s.interior ? "indoors" : "outdoors", s.night ? "night" : "day",
				s.observers, s.playerNear ? ", player watching" : "");
		}

		[[nodiscard]] std::string Request(RE::Actor* a_first, RE::Actor* a_second, bool a_force, std::string a_extra)
		{
			const auto& settings = Config::GetSingleton();
			const auto& scenario = settings.standInScenario;
			const auto  seconds =
				scenario.empty() ? settings.sceneSeconds
								 : (std::max)(settings.sceneSeconds, Scenarios::GetSingleton().SecondsFor(scenario));

			// The funnel every scene passes, the player's lane included; forced also
			// passes the lane's hold, as the dev channel's request does. Its door
			// refuses in words, and the words go on the HUD: "the reason is in
			// Rapport.log" sent the owner to a file for a one-line answer.
			std::string why;
			const bool  taken =
				PapyrusLink::GetSingleton().RequestScene(a_first, a_second, seconds, scenario, a_force, &why);
			const auto line = taken
			                      ? std::format("Rapport debug ({}): scene asked for - {} + {}{}", a_force ? "forced" : "real",
									  Name(a_first), Name(a_second), a_extra)
			                      : std::format("Rapport debug ({}): Rapport refused {} + {}: {}", a_force ? "forced" : "real",
									  Name(a_first), Name(a_second), why.empty() ? "the reason is in Rapport.log" : why);
			logger::info("debug trigger: {}", line);
			return line;
		}

		// True for a woman, false for a man; nullopt with no NPC record to ask.
		[[nodiscard]] std::optional<bool> Female(RE::Actor* a_actor)
		{
			auto* npc = a_actor ? a_actor->GetNPC() : nullptr;   // not const: GetSex() is not
			if (!npc) {
				return std::nullopt;
			}
			return RP::Compat::Female(npc);
		}

		[[nodiscard]] std::string Refuse(bool a_force, std::string_view a_why)
		{
			auto line = std::format("Rapport debug ({}): {}", a_force ? "forced" : "real", a_why);
			logger::info("debug trigger: {}", line);
			return line;
		}

		[[nodiscard]] std::string NotNow(bool a_force)
		{
			auto& link = PapyrusLink::GetSingleton();
			if (!link.Ready()) {
				return Refuse(a_force, "the bridge is not ready yet - give it a few seconds after a load");
			}
			if (link.Busy()) {
				return Refuse(a_force, "a scene is already running - one at a time");
			}
			return {};
		}
	}

	RE::Actor* ActorInFront(float a_maxDistance, float a_maxAngle)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* lists = RE::ProcessLists::GetSingleton();
		if (!player || !lists) {
			return nullptr;
		}
		RE::Actor* best = nullptr;
		float      bestAngle = a_maxAngle;
		for (const auto* list : { &lists->highActorHandles, &lists->middleHighActorHandles }) {
			for (const auto& handle : *list) {
				const auto actor = handle.get();
				// Never a child: the one "in front of you" is who every trigger in all three
				// mods acts on, and adults only is a hard rule (DESIGN) -- not a filter left
				// to each caller to remember.
				if (!actor || actor.get() == player || !actor->Get3D() || actor->IsDead(true) || actor->IsChild() ||
					!InPlayersWorld(*actor, *player)) {
					continue;
				}
				if (Distance(player, actor.get()) > a_maxDistance) {
					continue;
				}
				const auto angle = std::fabs(HeadingDegrees(player, actor.get()));
				if (angle <= bestAngle) {
					bestAngle = angle;
					best = actor.get();
				}
			}
		}
		return best;
	}

	std::string SceneWith(RE::Actor* a_first, RE::Actor* a_second, bool a_force)
	{
		if (!a_first || !a_second || a_first == a_second) {
			return Refuse(a_force, "nobody in front of you to pair with");
		}
		for (auto* actor : { a_first, a_second }) {
			if (const auto why = HardRule(actor); !why.empty()) {
				return Refuse(a_force, std::format("{} {}", Name(actor), why));
			}
		}
		if (auto busy = NotNow(a_force); !busy.empty()) {
			return busy;
		}

		std::string extra;
		if (a_force) {
			extra = " - skipped: the bar, cooldowns, privacy, time of day";
		} else {
			for (auto* actor : { a_first, a_second }) {
				if (const auto why = Resting(actor); !why.empty()) {
					return Refuse(a_force, why);
				}
			}
			// The bar, for two NPCs. The player is never scored: a scene with the
			// player is the player's choice, and the stand-in never picks them.
			const bool withPlayer = a_first->GetFormID() == kPlayer || a_second->GetFormID() == kPlayer;
			if (!Orientation::GetSingleton().Mutual(a_first, a_second)) {
				return Refuse(a_force, std::format("not a pair: {} (R-27)", Orientation::GetSingleton().WhyNot(a_first, a_second)));
			}
			if (!withPlayer) {
				const auto  scan = Scan();
				const auto& weights = Config::GetSingleton().Weights();
				const auto  ranked = RankPairs({ a_first, a_second }, scan.ObserverPositions(), weights, 1);
				if (ranked.empty()) {
					return Refuse(a_force, std::format("{} and {} are not a pair Rapport ranks - too far apart, or hostile",
											   Name(a_first), Name(a_second)));
				}
				if (ranked.front().score < weights.minimumScore) {
					return Refuse(a_force, std::format("score {:.2f} misses the {:.2f} bar ({})", ranked.front().score,
											   weights.minimumScore, Describe(ranked.front())));
				}
				extra = std::format(" - score {:.2f} clears the {:.2f} bar", ranked.front().score, weights.minimumScore);
			}
		}
		return Request(a_first, a_second, a_force, std::move(extra));
	}

	std::string SceneFor(RE::Actor* a_target, bool a_force)
	{
		if (!a_target || a_target->GetFormID() == kPlayer) {
			return Refuse(a_force, "nobody in front of you");
		}
		if (const auto why = HardRule(a_target); !why.empty()) {
			return Refuse(a_force, std::format("{} {}", Name(a_target), why));
		}
		if (auto busy = NotNow(a_force); !busy.empty()) {
			return busy;
		}
		if (!a_force) {
			// What stops the stand-in before it looks at anyone (Scheduler.cpp).
			if (PapyrusLink::GetSingleton().AutonomyPaused()) {
				return Refuse(a_force, "autonomy is paused (mailbox) - nothing starts on its own");
			}
			if (Config::GetSingleton().dryRun) {
				return Refuse(a_force, "DryRun is on in Rapport.ini - the stand-in only watches");
			}
			if (const auto why = Resting(a_target); !why.empty()) {
				return Refuse(a_force, why);
			}
		}

		// The stand-in's own candidates -- the same scan, the same filters -- so a
		// REAL answer is the answer autonomy would give.
		const auto scan = Scan();
		auto&      link = PapyrusLink::GetSingleton();
		std::vector<RE::Actor*> candidates;
		bool                    targetIn = false;
		for (const auto& handle : scan.Candidates()) {
			const auto actor = handle.get();
			if (!actor || link.IsActorBusy(actor->GetFormID())) {
				continue;
			}
			if (!a_force && !Resting(actor.get()).empty()) {
				continue;
			}
			targetIn = targetIn || actor.get() == a_target;
			candidates.push_back(actor.get());
		}
		if (!targetIn) {
			if (!a_force) {
				return Refuse(a_force, std::format("{} is not a candidate now - talking, in an ambient conversation, held "
												   "by a quest, or out of range (the tick log says which)",
										   Name(a_target)));
			}
			candidates.push_back(a_target);   // forced: the hard rules passed above
		}

		const auto& weights = Config::GetSingleton().Weights();
		const auto  every = candidates.size() * candidates.size();
		const auto  ranked = RankPairs(candidates, scan.ObserverPositions(), weights, every, !a_force);
		const auto  best = std::ranges::find_if(ranked, [&](const ScoredPair& p) {
			return p.first == a_target || p.second == a_target;
		});
		if (best == ranked.end()) {
			return Refuse(a_force, std::format("nobody Rapport would pair with {} within {:.0f} units", Name(a_target),
									   weights.maxPairDistance));
		}
		std::string extra;
		if (a_force) {
			extra = std::format(" - score {:.2f}; skipped: the bar, cooldowns, privacy, time of day", best->score);
		} else {
			if (best->score < weights.minimumScore) {
				return Refuse(a_force, std::format("{}'s best partner is {} at {:.2f}, under the {:.2f} bar ({})",
										   Name(a_target), Name(best->first == a_target ? best->second : best->first),
										   best->score, weights.minimumScore, Describe(*best)));
			}
			extra = std::format(" - score {:.2f} clears the {:.2f} bar", best->score, weights.minimumScore);
		}
		return Request(best->first, best->second, a_force, std::move(extra));
	}

	std::string ScenePair(RE::Actor* a_facing, std::string_view a_pair, bool a_force)
	{
		// How many women the pair has. AAF names compositions females first (F_M,
		// never M_F), so "MF" is accepted as the same thing.
		//
		// Compared WITHOUT case. The argument is a BSFixedString, and the engine interns
		// those case-insensitively, handing back whichever spelling it stored first
		// (aaf-under-the-hood §12): the MCM's "FM" arrived here as "fm", and the owner's
		// F+M button answered that "fm" is not a pair.
		std::string pair{ a_pair };
		std::ranges::transform(pair, pair.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
		int women = -1;
		if (pair == "FF") {
			women = 2;
		} else if (pair == "FM" || pair == "MF") {
			women = 1;
		} else if (pair == "MM") {
			women = 0;
		}
		if (women < 0) {
			return Refuse(a_force, std::format("\"{}\" is not a pair - FF, FM or MM", a_pair));
		}
		const std::string_view label = women == 2 ? "F+F" : women == 1 ? "F+M" : "M+M";

		if (auto busy = NotNow(a_force); !busy.empty()) {
			return busy;
		}
		if (!a_force) {
			// What stops the stand-in before it looks at anyone (Scheduler.cpp).
			if (PapyrusLink::GetSingleton().AutonomyPaused()) {
				return Refuse(a_force, "autonomy is paused (mailbox) - nothing starts on its own");
			}
			if (Config::GetSingleton().dryRun) {
				return Refuse(a_force, "DryRun is on in Rapport.ini - the stand-in only watches");
			}
		}

		// The one faced is one of the two when they fit this kind of pair. The player
		// is never one: "a scene with the one I face" is the button for that.
		RE::Actor*  anchor = nullptr;
		std::string passedOver;
		if (a_facing && a_facing->GetFormID() != kPlayer) {
			const auto female = Female(a_facing);
			const bool fits = female && ((*female && women >= 1) || (!*female && women <= 1));
			if (!fits) {
				passedOver = std::format(" ({} in front of you is not part of an {} pair - passed over)", Name(a_facing),
					label);
			} else {
				if (const auto why = HardRule(a_facing); !why.empty()) {
					return Refuse(a_force, std::format("{} {}", Name(a_facing), why));
				}
				if (!a_force) {
					if (const auto why = Resting(a_facing); !why.empty()) {
						return Refuse(a_force, why);
					}
				}
				anchor = a_facing;
			}
		}

		// The stand-in's own candidates, as SceneFor takes them, less anyone whose
		// sex nothing can tell and anyone a hard rule keeps out.
		const auto scan = Scan();
		std::vector<RE::Actor*> candidates;
		bool                    anchorIn = false;
		for (const auto& handle : scan.Candidates()) {
			const auto actor = handle.get();
			if (!actor || !Female(actor.get()) || !HardRule(actor.get()).empty()) {
				continue;
			}
			if (!a_force && !Resting(actor.get()).empty()) {
				continue;
			}
			anchorIn = anchorIn || actor.get() == anchor;
			candidates.push_back(actor.get());
		}
		if (anchor && !anchorIn) {
			if (!a_force) {
				return Refuse(a_force, std::format("{} is not a candidate now - talking, in an ambient conversation, held "
												   "by a quest, or out of range (the tick log says which)",
										   Name(anchor)));
			}
			candidates.push_back(anchor);   // forced: the hard rules passed above
		}

		const auto& weights = Config::GetSingleton().Weights();
		const auto  every = candidates.size() * candidates.size();
		const auto  ranked = RankPairs(candidates, scan.ObserverPositions(), weights, every, !a_force);
		const auto  best = std::ranges::find_if(ranked, [&](const ScoredPair& p) {
			const auto a = Female(p.first);
			const auto b = Female(p.second);
			if (!a || !b || static_cast<int>(*a) + static_cast<int>(*b) != women) {
				return false;
			}
			return !anchor || p.first == anchor || p.second == anchor;
		});
		if (best == ranked.end()) {
			return Refuse(a_force,
				anchor ? std::format("nobody Rapport would pair with {} as {} within {:.0f} units", Name(anchor), label,
							 weights.maxPairDistance)
					   : std::format("no {} pair near you that Rapport ranks - {} candidate(s) in range{}", label,
							 candidates.size(), passedOver));
		}

		std::string extra;
		if (a_force) {
			extra = std::format(" ({}) - score {:.2f}; skipped: the bar, cooldowns, privacy, time of day", label,
				best->score);
		} else {
			if (best->score < weights.minimumScore) {
				return Refuse(a_force, std::format("the best {} pair is {} + {} at {:.2f}, under the {:.2f} bar ({}){}",
										   label, Name(best->first), Name(best->second), best->score,
										   weights.minimumScore, Describe(*best), passedOver));
			}
			extra = std::format(" ({}) - score {:.2f} clears the {:.2f} bar", label, best->score, weights.minimumScore);
		}
		return Request(best->first, best->second, a_force, extra + passedOver);
	}
}

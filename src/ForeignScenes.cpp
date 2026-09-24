#include "ForeignScenes.h"

#include "Aftermath.h"
#include "Config.h"
#include "Expressions.h"
#include "PapyrusLink.h"
#include "TreeIndex.h"

namespace RP
{
	namespace
	{
		// A scene with neither a tree nor a length (AAF's duration -1: it loops until
		// somebody stops it) still has to build, so its progress is time over a nominal
		// length. ASSUMED, for the owner to tune: about as long as the scenes Rapport
		// itself asks for.
		constexpr float kLoopNominalSeconds = 150.0f;

		// How long a finished scene is remembered, so its late or second OnSceneEnd
		// cannot put a second layer of cum on the same people (A-22).
		constexpr float kFinishedSeconds = 120.0f;

		// A scene that has not begun to animate this long after its start never will.
		// AAF's OnSceneInit comes once every actor has locked -- AFTER the walk, which
		// runs from OnWalkInit about ten seconds earlier -- and the first animation
		// follows it within a fraction of a second: 0.33 s and 0.10 s measured
		// (docs/runs/2026-09-18-first-complete-chain.log). ASSUMED, and very long.
		constexpr float kNeverAnimatedSeconds = 180.0f;

		// The meta Rapport asks for its own scenes with (Bridge.psc kOurMeta).
		constexpr std::string_view kOurMeta = "Rapport,autonomy"sv;

		// A TIMED scene with no tree ends on its own timer (aaf-sot, finding H), so one
		// this far past its length without an end event missed it.
		[[nodiscard]] float OverdueAfter(float a_duration)
		{
			return (std::max)(a_duration * 3.0f, a_duration + 300.0f);
		}

		[[nodiscard]] float SecondsBetween(std::chrono::steady_clock::time_point a_from,
			std::chrono::steady_clock::time_point a_to)
		{
			return std::chrono::duration<float>{ a_to - a_from }.count();
		}

		[[nodiscard]] std::string Lower(std::string_view a_text)
		{
			std::string out{ a_text };
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		// Case-insensitive, like every string AAF hands back (docs/aaf-under-the-hood.md,
		// section 12): it returns the casing it interned first.
		[[nodiscard]] bool IsOurMeta(std::string_view a_meta)
		{
			return Lower(a_meta) == Lower(kOurMeta);
		}

		// The tree a scene plays, found ONCE from the position AAF started it on: every
		// later event names the step playing ("02", "Orgasm (loop)"), which is no tree's
		// id. Case-insensitive, for the same reason.
		[[nodiscard]] std::uint32_t StagesOf(std::string_view a_position)
		{
			if (a_position.empty()) {
				return 0;
			}
			const auto& index = TreeIndex::GetSingleton();
			if (const auto* entry = index.Find(a_position)) {
				return entry->stages;
			}
			const auto wanted = Lower(a_position);
			for (const auto& entry : index.Entries()) {
				if (Lower(entry.positionID) == wanted) {
					return entry.stages;
				}
			}
			return 0;
		}

		[[nodiscard]] std::string Members(const std::vector<ForeignScenes::Member>& a_members)
		{
			std::string out;
			for (const auto& member : a_members) {
				if (!out.empty()) {
					out += ", ";
				}
				out += member.player ? std::string{ "the player" } : std::format("{:08X}", member.formID);
			}
			return out;
		}

		[[nodiscard]] std::string Join(const std::string& a_left, std::string_view a_right)
		{
			if (a_left.empty()) {
				return std::string{ a_right };
			}
			if (a_right.empty()) {
				return a_left;
			}
			return a_left + "," + std::string{ a_right };
		}

		[[nodiscard]] bool Holds(const std::vector<ForeignScenes::Member>& a_members, std::uint32_t a_formID)
		{
			return std::ranges::any_of(a_members, [&](const ForeignScenes::Member& m) { return m.formID == a_formID; });
		}

		// Holds them, and has put its face on them.
		[[nodiscard]] bool Wears(const std::vector<ForeignScenes::Member>& a_members, std::uint32_t a_formID)
		{
			return std::ranges::any_of(
				a_members, [&](const ForeignScenes::Member& m) { return m.formID == a_formID && m.wearing; });
		}

		// Everybody in a_who is in a_in -- and a_who is somebody: an empty list proves
		// nothing about whose event it is.
		[[nodiscard]] bool AllIn(const std::vector<ForeignScenes::Member>& a_who,
			const std::vector<ForeignScenes::Member>& a_in)
		{
			return !a_who.empty() &&
			       std::ranges::all_of(a_who, [&](const ForeignScenes::Member& m) { return Holds(a_in, m.formID); });
		}

		[[nodiscard]] std::vector<std::uint32_t> IdsOf(const std::vector<ForeignScenes::Member>& a_members)
		{
			std::vector<std::uint32_t> ids;
			for (const auto& member : a_members) {
				ids.push_back(member.formID);
			}
			return ids;
		}

		// DESIGN.md's hard rules, on actors Rapport did not choose. A child among the
		// scene's OWN actors -- AAF's actor list, never a bystander -- means the scene is
		// left entirely alone. A race Rapport does not dress (races.json, A-4) gets no
		// face and no cum, but stays in the scene: who an act landed on is decided with
		// everybody there (a man and a female creature is a PAIR, and the pair rule gives
		// her the cum -- so nobody -- where dropping her first would make him "alone" and
		// give it to him).
		enum class Eligible
		{
			kYes,
			kChild,
			kNobody
		};

		[[nodiscard]] Eligible Screen(std::int32_t a_location, const std::vector<ForeignScenes::Member>& a_members,
			bool a_log)
		{
			if (std::ranges::any_of(a_members, [](const ForeignScenes::Member& m) { return m.child; })) {
				logger::error(
					"foreign scene {:08X}: a CHILD is among its actors ({}) - Rapport leaves the whole scene alone "
					"(DESIGN hard rule: adults only)",
					static_cast<std::uint32_t>(a_location), Members(a_members));
				return Eligible::kChild;
			}
			const auto undressed = static_cast<std::size_t>(
				std::ranges::count_if(a_members, [](const ForeignScenes::Member& m) { return !m.raceAllowed; }));
			if (undressed == a_members.size()) {
				return Eligible::kNobody;
			}
			if (undressed > 0 && a_log) {
				logger::info(
					"foreign scene {:08X}: {} actor(s) of a race Rapport does not dress get no face and no cum "
					"(races.json)",
					static_cast<std::uint32_t>(a_location), undressed);
			}
			return Eligible::kYes;
		}

		[[nodiscard]] bool FacesOn()
		{
			return Config::GetSingleton().foreignFaces && Expressions::GetSingleton().Enabled();
		}
	}

	ForeignScenes& ForeignScenes::GetSingleton() noexcept
	{
		static ForeignScenes singleton;
		return singleton;
	}

	void ForeignScenes::Started(std::int32_t a_location, std::vector<Member> a_members, std::string a_position,
		std::string a_tags, std::string a_meta, bool a_npcControlled, float a_duration)
	{
		if (a_location == 0 || a_members.empty()) {
			logger::warn("foreign scene: a start with no location or no actors - ignored (location {:08X}, {} actor(s))",
				static_cast<std::uint32_t>(a_location), a_members.size());
			return;
		}

		// Read BEFORE our lock: no other subsystem's lock is ever taken inside it.
		const auto verdict = Screen(a_location, a_members, true);
		const auto [first, second] = PapyrusLink::GetSingleton().InFlightPair();
		// Ours when it SAYS so and holds the pair in flight. By the actors alone, another
		// mod's scene that got one of ours first would be taken for ours and left bare
		// for its whole length.
		const bool ours = IsOurMeta(a_meta) && std::ranges::any_of(a_members, [&](const Member& m) {
			return m.formID != 0 && (m.formID == first || m.formID == second);
		});
		const bool faces = FacesOn();
		const auto schedule = Expressions::GetSingleton().IntensitySchedule();
		const bool namesAct = !a_tags.empty() && !Expressions::FaceForAct(a_tags, a_position, 2).empty();
		const auto where = std::format("the scene at {:08X}", static_cast<std::uint32_t>(a_location));

		std::vector<Retired>                        retired;
		std::vector<Order>                          out;
		std::vector<std::pair<std::uint32_t, int>>  heat;
		std::vector<std::uint32_t>                  held;
		{
			NamedLock lock{ _lock, "foreign scenes" };
			if (!lock) {
				return;
			}
			// Every way out of the locked part still reaches the carrying-out below.
			[&] {
				const auto now = Clock::now();

				// A new scene here: whatever was given up on at this place is over. Its end
				// may still come, so it is remembered as finished, by its cast.
				if (const auto quiet = _quiet.find(a_location); quiet != _quiet.end()) {
					Remember(a_location, quiet->second.actors, now);
					_quiet.erase(quiet);
				}

				if (verdict != Eligible::kYes || ours) {
					// Remembered, so its animations and its end are left alone too.
					_ignored.insert(a_location);
					// A record already here is finished now: this scene's own placeholder
					// (an animation came before the start), or an earlier scene that missed
					// its end. Left in place, the poll would go on dressing it.
					RetireAt(a_location, retired, "a scene Rapport leaves alone began here", true, nullptr);
					// And its actors are in THIS scene now: every other record holding them
					// is over, and nothing here dresses them.
					EvictElsewhere(a_location, a_members, retired, nullptr, where);
					if (ours) {
						logger::warn(
							"foreign scene {:08X}: it holds the pair Rapport has in flight and carries Rapport's meta - "
							"it is ours, left to the request's own path",
							static_cast<std::uint32_t>(a_location));
					}
					return;
				}
				_ignored.erase(a_location);

				Scene scene;
				if (const auto found = _scenes.find(a_location);
					found != _scenes.end() && !found->second.started && !found->second.ended) {
					// A join placeholder: an animation event arrived before this start. It
					// becomes the scene.
					scene = std::move(found->second);
					_scenes.erase(found);
					for (const auto& member : a_members) {
						if (!Holds(scene.members, member.formID)) {
							scene.members.push_back(member);
						}
					}
				} else {
					scene.members = a_members;
					scene.startedAt = now;
				}
				scene.location = a_location;
				scene.started = true;
				scene.meta = std::move(a_meta);
				scene.npcControlled = a_npcControlled;
				scene.duration = a_duration;
				// A placeholder has already played: where it is and what it is doing are
				// LATER than what the init says it began with.
				if (!scene.animating) {
					scene.position = a_position;
					if (namesAct) {
						scene.liveAct = std::move(a_tags);
					}
				}
				if (scene.stages == 0) {
					scene.stages = StagesOf(a_position);
				}
				scene.lastEventAt = now;

				// Who this scene takes from other records, and what they are wearing, is
				// decided BEFORE those records go. A scene that names an act will dress them
				// -- at once if it has animated, else at its first animation, a fraction of a
				// second away -- so they are KEPT, face and skin heat alike ("a body can
				// walk out of one scene still sweating into the next"), and this record owns
				// their clearing from now on. A scene that names no act dresses nobody, so
				// what the old one put on them comes off.
				const auto  face = faces ? FaceNow(scene, schedule) : std::pair<std::string, std::string>{};
				const bool  willWear = faces && !scene.liveAct.empty();
				const auto* keep = willWear ? &scene.members : nullptr;
				std::vector<std::uint32_t> inherited;
				if (keep) {
					for (const auto& member : scene.members) {
						if (std::ranges::any_of(_scenes,
								[&](const auto& entry) { return Wears(entry.second.members, member.formID); })) {
							inherited.push_back(member.formID);
						}
					}
				}

				// A scene already here that is not a join placeholder: one that missed its
				// end, or one still in its afterglow. Finished properly before the new one
				// takes the key -- overwriting it would leave its actors' faces on.
				RetireAt(a_location, retired, "a new scene began at the same place", true, keep);
				EvictElsewhere(a_location, scene.members, retired, keep, where);
				// Owned from now on, still wearing what the old scene put on. Not for a
				// placeholder that has animated: its face goes on everybody below, in this
				// same call, and marking them first would skip them if that face is unchanged.
				if (!scene.animating) {
					for (auto& member : scene.members) {
						if (std::ranges::find(inherited, member.formID) != inherited.end()) {
							member.wearing = true;
						}
					}
				}

				// keep points into `scene`: not used past this move.
				auto& placed = _scenes.insert_or_assign(a_location, std::move(scene)).first->second;

				logger::info(
					"foreign scene {:08X}: {} ({}) - meta \"{}\", {}, position {}, {}",
					static_cast<std::uint32_t>(a_location), Members(placed.members),
					placed.members.size() == 1 ? "alone" : std::format("{} actors", placed.members.size()), placed.meta,
					placed.npcControlled ? "NPC-controlled" : "player-controlled",
					placed.position.empty() ? "(unnamed)" : placed.position,
					placed.stages > 1          ? std::format("a tree of {} steps", placed.stages)
					: placed.duration > 0.0f ? std::format("timed at {:.0f}s", placed.duration)
											  : std::string{ "no length (loops)" });

				if (faces) {
					Wear(placed, face, out, heat, held);
				}
			}();
		}

		Carry(retired);
		auto& expressions = Expressions::GetSingleton();
		for (const auto formID : held) {
			expressions.HoldForeign(formID);
		}
		expressions.RaiseHeat(heat, out);
		Expressions::Send(out);
	}

	void ForeignScenes::Animation(std::int32_t a_location, std::vector<Member> a_members, std::string a_position,
		std::string a_tags)
	{
		if (a_location == 0) {
			return;
		}

		// Read BEFORE our lock, as everywhere here.
		const auto verdict = a_members.empty() ? Eligible::kYes : Screen(a_location, a_members, false);
		const bool faces = FacesOn();
		const auto schedule = Expressions::GetSingleton().IntensitySchedule();
		const bool namesAct = !a_tags.empty() && !Expressions::FaceForAct(a_tags, a_position, 2).empty();
		const bool namesAftermath = Aftermath::GetSingleton().NamesAnAftermath(a_tags);
		const auto where = std::format("the scene at {:08X}", static_cast<std::uint32_t>(a_location));

		std::vector<Retired>                        retired;
		std::vector<Order>                          out;
		std::vector<std::pair<std::uint32_t, int>>  heat;
		std::vector<std::uint32_t>                  held;
		{
			NamedLock lock{ _lock, "foreign scenes" };
			if (!lock) {
				return;
			}
			// Every way out of the locked part still reaches the carrying-out below.
			[&] {
				const auto now = Clock::now();
				if (_ignored.contains(a_location)) {
					return;
				}
				if (verdict == Eligible::kChild) {
					// Seen only now: whatever this scene was given comes off, and it is left
					// alone from here on -- no cum, whatever its end says.
					_ignored.insert(a_location);
					RetireAt(a_location, retired, "a child is in it", false, nullptr);
					return;
				}
				if (verdict == Eligible::kNobody) {
					return;
				}

				auto found = _scenes.find(a_location);
				if (found != _scenes.end() && !a_members.empty() && !AllIn(a_members, found->second.members)) {
					// Somebody the record here does not have -- and only people a scene that
					// finished here had: that scene's late animation. Decided BEFORE the record
					// is touched, so a late event cannot end an afterglow and strand its faces.
					if (FitsFinished(a_location, a_members, now)) {
						return;
					}
					auto& there = found->second;
					if (there.ended) {
						// A scene that ENDED here, and an animation naming somebody no scene here
						// had: a new scene's first animation, come before its start. The old
						// one's afterglow ends now, and the new one is joined in progress --
						// keeping whoever it will dress at once.
						retired.push_back(Retire(there, false, "a new scene began at the same place",
							faces && namesAct ? &a_members : nullptr));
						_scenes.erase(found);
						found = _scenes.end();
					}
				}

				if (found == _scenes.end()) {
					// Nothing is running here. A late animation of a scene that finished here
					// is left alone.
					if (a_members.empty() || FitsFinished(a_location, a_members, now)) {
						return;
					}
					if (const auto quiet = _quiet.find(a_location); quiet != _quiet.end()) {
						const bool same = std::ranges::all_of(a_members, [&](const Member& m) {
							return std::ranges::find(quiet->second.actors, m.formID) != quiet->second.actors.end();
						});
						if (same && quiet->second.kind == Quiet::kOverdue) {
							return;   // taken for over, its aftermath decided: the rest is left alone
						}
						// Given up on for never animating and here it is animating -- or another
						// scene at the place, whose start was missed. Either way the given-up one
						// is remembered as finished and a placeholder follows.
						if (!same) {
							Remember(a_location, quiet->second.actors, now);
						}
						_quiet.erase(quiet);
					}
					// Joined in progress: its start came before we were listening -- a load,
					// a rebuilt bridge. A placeholder, until (if ever) its OnSceneInit arrives.
					Scene fresh;
					fresh.location = a_location;
					fresh.members = a_members;
					fresh.stages = StagesOf(a_position);
					fresh.startedAt = now;
					found = _scenes.emplace(a_location, std::move(fresh)).first;
					logger::info("foreign scene {:08X}: joined in progress - {}", static_cast<std::uint32_t>(a_location),
						Members(found->second.members));
				}

				auto& scene = found->second;
				if (scene.ended) {
					return;
				}
				scene.lastEventAt = now;

				// Steps TAKEN: the entry animation is step 0, as for our own scenes
				// (Scenarios::NoteAnimationAdvanced), and the count stops at the tree's last.
				if (!scene.animating) {
					scene.animating = true;
					scene.animatingAt = now;
					scene.steps = 0;
					if (scene.stages == 0) {
						scene.stages = StagesOf(a_position);
					}
				} else if (scene.stages == 0 || scene.steps + 1 < scene.stages) {
					++scene.steps;
				}
				if (!a_position.empty()) {
					scene.position = std::move(a_position);
				}

				// Grows, never shrinks: somebody who left keeps what they were given until
				// the scene finishes, when it clears everybody it put a face on.
				for (const auto& member : a_members) {
					if (!Holds(scene.members, member.formID)) {
						scene.members.push_back(member);
					}
				}

				scene.allTags = Join(scene.allTags, a_tags);
				// Only an animation that names an act moves either reading: a transition or
				// an idle between two positions must not erase where the scene actually is,
				// and a scene that went from vaginal to a blowjob finishes on the blowjob.
				if (namesAct) {
					scene.liveAct = a_tags;
				}
				if (namesAftermath) {
					scene.lastActTags = a_tags;
				}

				// The face before the handover, as at the start.
				const auto face = faces ? FaceNow(scene, schedule) : std::pair<std::string, std::string>{};
				if (!a_members.empty()) {
					// `scene` stays valid: only OTHER records are erased.
					EvictElsewhere(a_location, a_members, retired, face.first.empty() ? nullptr : &a_members, where);
				}
				if (faces) {
					Wear(scene, face, out, heat, held);
				}
			}();
		}

		Carry(retired);
		auto& expressions = Expressions::GetSingleton();
		for (const auto formID : held) {
			expressions.HoldForeign(formID);
		}
		expressions.RaiseHeat(heat, out);
		Expressions::Send(out);
	}

	void ForeignScenes::Ended(std::int32_t a_location, std::vector<Member> a_members, std::string a_position,
		std::string a_tags)
	{
		if (a_location == 0) {
			return;
		}

		// Read BEFORE our lock, as everywhere here.
		const auto verdict = a_members.empty() ? Eligible::kYes : Screen(a_location, a_members, false);
		const bool namesAftermath = Aftermath::GetSingleton().NamesAnAftermath(a_tags);
		const bool faces = FacesOn();
		const auto afterSet = Expressions::GetSingleton().AfterSet();

		std::vector<Retired>                        retired;
		std::vector<Order>                          out;
		std::vector<std::pair<std::uint32_t, int>>  heat;
		std::vector<std::uint32_t>                  held;
		{
			NamedLock lock{ _lock, "foreign scenes" };
			if (!lock) {
				return;
			}
			// Every way out of the locked part still reaches the carrying-out below.
			[&] {
				const auto now = Clock::now();
				if (verdict == Eligible::kChild) {
					_ignored.erase(a_location);
					RetireAt(a_location, retired, "a child is in it", false, nullptr);
					// Remembered too: a second end with the child unreadable must not be
					// taken for an end of adults.
					Remember(a_location, IdsOf(a_members), now);
					return;   // left alone to the end, and forgotten now
				}
				if (const auto which = EndOfFinished(a_location, a_members, now); !which.empty()) {
					logger::info("foreign scene {:08X}: {} - ignored, its aftermath was decided once",
						static_cast<std::uint32_t>(a_location), which);
					return;
				}
				if (const auto quiet = _quiet.find(a_location); quiet != _quiet.end()) {
					const auto& mark = quiet->second;
					const bool  same = a_members.empty() || std::ranges::all_of(a_members, [&](const Member& m) {
						return std::ranges::find(mark.actors, m.formID) != mark.actors.end();
					});
					if (same) {
						logger::info("foreign scene {:08X}: its end came after all - {}",
							static_cast<std::uint32_t>(a_location),
							mark.kind == Quiet::kOverdue ? "its aftermath was decided when it was taken for over"
														 : "it never began to animate, so it left nothing");
						Remember(a_location, mark.actors, now);
						_quiet.erase(quiet);
						return;
					}
					// Somebody else's end at the place: the given-up one stays recognisable.
					Remember(a_location, mark.actors, now);
					_quiet.erase(quiet);
				}
				if (_ignored.erase(a_location) > 0) {
					return;   // ours: left alone to the end, and forgotten now
				}

				auto   found = _scenes.find(a_location);
				Scene* scene = found != _scenes.end() ? &found->second : nullptr;
				if (scene && scene->ended) {
					// Ended already, and this end names somebody neither it nor any scene that
					// finished here had (its own second end was recognised above): the end of
					// another scene at the place, whose start and animations were missed. The
					// afterglow ends now, and this end stands on its own.
					retired.push_back(Retire(*scene, false, "another scene ended at the same place", nullptr));
					_scenes.erase(found);
					found = _scenes.end();
					scene = nullptr;
				}

				// Who was in it: the record's cast, and anybody the end names that it
				// lacked. An actor unloaded by the end is still somebody the act involved,
				// and dropping them would turn a pair "alone".
				std::vector<Member> who = scene ? scene->members : std::vector<Member>{};
				for (const auto& member : a_members) {
					if (!Holds(who, member.formID)) {
						who.push_back(member);
					}
				}
				Remember(a_location, IdsOf(who), now);

				// Decided on everybody the scene had, not only on whoever the end could read.
				// And nothing is left by a scene that never began to animate: a walk given up
				// on ends too, carrying the tags of the act it never played.
				Retired job;
				job.location = a_location;
				job.aftermath = std::ranges::any_of(who, [](const Member& m) { return m.raceAllowed; }) &&
				                std::ranges::none_of(who, [](const Member& m) { return m.child; }) &&
				                (!scene || scene->animating);
				for (const auto& member : who) {
					job.actors.push_back(member.formID);
					job.sexes.push_back(member.sex);
					job.dressable.push_back(member.raceAllowed);
				}
				job.allTags = Join(scene ? scene->allTags : std::string{}, a_tags);
				// The record's last act decides. The end's own tags only stand in when there
				// is nothing else: what a tree's end carries -- its root position's tags? --
				// is unmeasured, and A-22 puts the cum where the scene FINISHED.
				if (scene && !scene->lastActTags.empty()) {
					job.lastActTags = scene->lastActTags;
				} else if (namesAftermath) {
					job.lastActTags = a_tags;
				}
				job.why = "ended";
				retired.push_back(std::move(job));

				if (!scene) {
					logger::info("foreign scene {:08X}: ended, never having been seen start - {}",
						static_cast<std::uint32_t>(a_location), Members(a_members));
					return;
				}
				scene->ended = true;
				scene->endedAt = now;
				if (!a_position.empty()) {
					scene->position = std::move(a_position);
				}
				logger::info("foreign scene {:08X}: ended after {:.0f}s, {} step(s){}",
					static_cast<std::uint32_t>(a_location),
					SecondsBetween(scene->animating ? scene->animatingAt : scene->startedAt, now), scene->steps,
					scene->animating ? "" : " - it never began to animate, so it leaves nothing");
				if (faces && !scene->face.empty() && scene->animating) {
					// The afterglow, as for our own scenes; Pump takes it off when it is over.
					Wear(*scene, { afterSet, "the scene ended" }, out, heat, held);
				} else {
					// Nothing of ours is on them to wind down: finish now.
					retired.push_back(Retire(*scene, false, "ended", nullptr));
					_scenes.erase(found);
				}
			}();
		}

		// The afterglow's face first, then the clears and the aftermath.
		auto& expressions = Expressions::GetSingleton();
		for (const auto formID : held) {
			expressions.HoldForeign(formID);
		}
		expressions.RaiseHeat(heat, out);
		Expressions::Send(out);
		Carry(retired);
	}

	void ForeignScenes::OwnSceneStarted(std::uint32_t a_first, std::uint32_t a_second)
	{
		std::vector<Member> ours;
		for (const auto formID : { a_first, a_second }) {
			if (formID != 0) {
				Member member;
				member.formID = formID;
				ours.push_back(member);
			}
		}
		if (ours.empty()) {
			return;
		}

		std::vector<Retired> retired;
		{
			NamedLock lock{ _lock, "foreign scenes" };
			if (!lock) {
				return;
			}
			// No record is at location 0, so every one holding either is evicted. The two
			// are KEPT: our own scene puts its faces on them now, and our own afterglow
			// takes them off.
			EvictElsewhere(0, ours, retired, &ours, "Rapport's own scene");
		}
		Carry(retired);
	}

	void ForeignScenes::OwnSceneEnded(std::int32_t a_location, const std::vector<Member>& a_members)
	{
		if (a_location == 0) {
			return;
		}
		NamedLock lock{ _lock, "foreign scenes" };
		if (!lock) {
			return;
		}
		Remember(a_location, IdsOf(a_members), Clock::now());
	}

	std::pair<std::string, std::string> ForeignScenes::FaceNow(Scene& a_scene, const Schedule& a_schedule) const
	{
		if (a_scene.liveAct.empty()) {
			return {};   // nothing has said what this scene is yet: leave the face alone
		}
		if (!a_scene.animating) {
			// Not before the first animation. It follows the start within a fraction of
			// a second -- the walk is over by then -- so a face now would be overwritten
			// before it showed, at the cost of an AAF call per actor.
			return {};
		}

		// The story advances on tree steps, not on a clock (docs/aaf-under-the-hood.md,
		// sections 16 and 17). A timed scene with no tree ends on its own timer, so its
		// length is real. Only a scene with neither falls back to time.
		float       fraction = 0.0f;
		std::string why;
		const auto  elapsed = SecondsBetween(a_scene.animatingAt, Clock::now());
		if (a_scene.stages > 1) {
			fraction = static_cast<float>(a_scene.steps) / static_cast<float>(a_scene.stages - 1);
			why = std::format("tree step {} of {}", a_scene.steps, a_scene.stages - 1);
		} else if (a_scene.duration > 0.0f) {
			fraction = elapsed / a_scene.duration;
			why = std::format("{:.0f}s of its {:.0f}s", elapsed, a_scene.duration);
		} else {
			fraction = elapsed / kLoopNominalSeconds;
			why = std::format("{:.0f}s, no length to measure against", elapsed);
		}
		fraction = std::clamp(fraction, 0.0f, 1.0f);

		// The same schedule as our own scenes (expressions.json's steps), read as a
		// LEVEL: the act picks the family. The climax is never timed -- only a climax
		// tag gives it (section 17: a timed one arrived a minute early).
		int intensity = 1;
		for (const auto& [at, level] : a_schedule) {
			if (fraction >= at) {
				intensity = level;
			}
		}
		if (a_scene.climaxSeen) {
			// The post-climax ratchet, as Scenarios keeps it: after an orgasm the level
			// never falls, though the family still follows the act.
			intensity = 3;
		}
		auto face = std::string{ Expressions::FaceForAct(a_scene.liveAct, a_scene.position, (std::max)(intensity, 1)) };
		if (face == "Rapport_Climax") {
			a_scene.climaxSeen = true;
			why = "a climax tag";
		} else if (intensity == 0 && face.starts_with("Rapport_Pleasure")) {
			face = "Rapport_Anticipation";
		}
		return { face, why };
	}

	void ForeignScenes::Wear(Scene& a_scene, const std::pair<std::string, std::string>& a_face,
		std::vector<Order>& a_out, std::vector<std::pair<std::uint32_t, int>>& a_heat, std::vector<std::uint32_t>& a_held)
	{
		const auto& [face, why] = a_face;
		if (face.empty()) {
			return;
		}

		// A new face goes on everybody; the same face only on whoever does not wear it
		// yet -- a member read late, or one taken over from another record.
		const bool  change = face != a_scene.face;
		const int   level = Expressions::HeatLevelFor(face);
		std::size_t late = 0;
		for (auto& member : a_scene.members) {
			if (member.formID == 0 || !member.raceAllowed || (!change && member.wearing)) {
				continue;
			}
			a_out.push_back(
				Order{ Order::Kind::kApplyExpression, member.formID, Expressions::VariantFor(face, member.formID) });
			// Every time it goes on, not only the first: a load or the panic switch may
			// have swept the wearing list since, and a face nobody lists is a face nobody
			// clears.
			a_held.push_back(member.formID);
			if (level > 0) {
				a_heat.emplace_back(member.formID, level);
			}
			member.wearing = true;
			if (!change) {
				++late;
			}
		}

		if (change) {
			logger::info("foreign scene {:08X}: {} -> {} ({})", static_cast<std::uint32_t>(a_scene.location),
				a_scene.face.empty() ? "(none)" : a_scene.face, face, why);
			a_scene.face = face;
		} else if (late > 0) {
			logger::info("foreign scene {:08X}: {} on {} more actor(s)", static_cast<std::uint32_t>(a_scene.location),
				face, late);
		}
	}

	ForeignScenes::Retired ForeignScenes::Retire(
		Scene& a_scene, bool a_withAftermath, std::string_view a_why, const std::vector<Member>* a_keep) const
	{
		Retired out;
		out.location = a_scene.location;
		out.why = a_why;

		// Only members this record put a face on -- and whom nobody has dressed since:
		// another live record, or the scene replacing this one. Their release would
		// clear a face -- and a hold -- that is not this record's any more.
		for (const auto& member : a_scene.members) {
			if (member.formID == 0 || !member.wearing || (a_keep && Holds(*a_keep, member.formID))) {
				continue;
			}
			const bool elsewhere = std::ranges::any_of(_scenes, [&](const auto& entry) {
				return entry.first != a_scene.location && Wears(entry.second.members, member.formID);
			});
			if (!elsewhere) {
				out.release.push_back(member.formID);
			}
		}

		// Nothing is left behind by a scene that never began to animate.
		if (a_withAftermath && a_scene.animating) {
			out.aftermath = true;
			for (const auto& member : a_scene.members) {
				out.actors.push_back(member.formID);
				out.sexes.push_back(member.sex);
				out.dressable.push_back(member.raceAllowed);
			}
			out.allTags = a_scene.allTags;
			out.lastActTags = a_scene.lastActTags;
		}
		return out;
	}

	void ForeignScenes::Carry(std::vector<Retired>& a_retired)
	{
		if (a_retired.empty()) {
			return;
		}
		// The clears are the expression layer's, because the wearing list is: the same
		// list that makes a save taken mid-scene clear these faces on the load.
		std::vector<Order> clears;
		auto&              expressions = Expressions::GetSingleton();
		for (const auto& retired : a_retired) {
			if (!retired.release.empty()) {
				expressions.ReleaseForeign(retired.release, clears,
					std::format("foreign scene {:08X}: {}", static_cast<std::uint32_t>(retired.location), retired.why));
			}
		}
		Expressions::Send(clears);

		for (const auto& retired : a_retired) {
			if (retired.aftermath && !retired.actors.empty()) {
				Aftermath::GetSingleton().OnForeignSceneEnded(retired.actors, retired.sexes, retired.dressable,
					retired.allTags, retired.lastActTags,
					std::format("foreign scene {:08X}", static_cast<std::uint32_t>(retired.location)));
			}
		}
	}

	void ForeignScenes::EvictElsewhere(std::int32_t a_location, const std::vector<Member>& a_members,
		std::vector<Retired>& a_out, const std::vector<Member>* a_keep, std::string_view a_where)
	{
		const auto now = Clock::now();
		for (auto it = _scenes.begin(); it != _scenes.end();) {
			auto& other = it->second;
			const bool shares = it->first != a_location &&
			                    std::ranges::any_of(a_members, [&](const Member& m) { return Holds(other.members, m.formID); });
			if (!shares) {
				++it;
				continue;
			}
			// An actor is in one AAF scene at a time, so the record they were in is over
			// -- its end event was missed if it had not already arrived.
			logger::info("foreign scene {:08X}: its actor is in {} now - {}", static_cast<std::uint32_t>(it->first),
				a_where, other.ended ? "finished early" : "its end was missed");
			if (!other.ended) {
				Remember(it->first, IdsOf(other.members), now);
			}
			a_out.push_back(Retire(other, !other.ended,
				other.ended ? "its actor moved on" : "its actor turned up in another scene", a_keep));
			it = _scenes.erase(it);
		}
	}

	void ForeignScenes::RetireAt(std::int32_t a_location, std::vector<Retired>& a_out, std::string_view a_why,
		bool a_aftermathIfOwed, const std::vector<Member>* a_keep)
	{
		const auto found = _scenes.find(a_location);
		if (found == _scenes.end()) {
			return;
		}
		auto& old = found->second;
		// A started scene with no end yet still owes one, which may arrive after this:
		// remembered, so it finishes nothing of what comes next.
		const bool owed = old.started && !old.ended;
		if (owed) {
			Remember(a_location, IdsOf(old.members), Clock::now());
		}
		a_out.push_back(Retire(old, owed && a_aftermathIfOwed, a_why, a_keep));
		_scenes.erase(found);
	}

	void ForeignScenes::Remember(std::int32_t a_location, std::vector<std::uint32_t> a_actors, Clock::time_point a_now)
	{
		Finished done;
		done.location = a_location;
		done.actors = std::move(a_actors);
		done.at = a_now;
		_finished.push_back(std::move(done));
	}

	const ForeignScenes::Finished* ForeignScenes::FinishedAt(std::int32_t a_location, Clock::time_point a_now) const
	{
		const Finished* latest = nullptr;
		for (const auto& done : _finished) {
			if (done.location == a_location && SecondsBetween(done.at, a_now) < kFinishedSeconds &&
				(!latest || done.at > latest->at)) {
				latest = &done;
			}
		}
		return latest;
	}

	bool ForeignScenes::FitsFinished(std::int32_t a_location, const std::vector<Member>& a_members,
		Clock::time_point a_now) const
	{
		if (a_members.empty()) {
			return false;
		}
		return std::ranges::any_of(_finished, [&](const Finished& done) {
			return done.location == a_location && SecondsBetween(done.at, a_now) < kFinishedSeconds &&
			       std::ranges::all_of(a_members, [&](const Member& m) {
					   return std::ranges::find(done.actors, m.formID) != done.actors.end();
				   });
		});
	}

	std::string_view ForeignScenes::EndOfFinished(std::int32_t a_location, const std::vector<Member>& a_members,
		Clock::time_point a_now) const
	{
		const auto live = _scenes.find(a_location);
		const bool running = live != _scenes.end() && !live->second.ended;
		if (!running) {
			// Nothing plays here. A record still in its afterglow is its own evidence,
			// however long the afterglow is set to last: an end naming nobody readable,
			// or only its cast, is its second.
			if (live != _scenes.end() && (a_members.empty() || AllIn(a_members, live->second.members))) {
				return "a second end"sv;
			}
			// Otherwise one naming nobody readable, or only people a scene that finished
			// here had, is that scene's second end.
			if (!FinishedAt(a_location, a_now)) {
				return {};
			}
			return a_members.empty() || FitsFinished(a_location, a_members, a_now) ? "a second end"sv
			                                                                          : std::string_view{};
		}
		// A scene plays here now. Before its first animation -- a fraction of a second
		// after its start -- an end a finished scene here explains is that scene's: taking
		// it for this one's would wipe out a scene that has not begun. Nothing is lost if
		// it was this one's after all: a scene that never animated leaves nothing, and
		// the never-animated reaper clears it.
		if (!live->second.animating && FitsFinished(a_location, a_members, a_now)) {
			return "the late end of the scene before this one"sv;
		}
		// Nobody readable, or only its own cast: its end. Swallowing a live scene's
		// real end would strand its face and lose its cum.
		if (a_members.empty() || AllIn(a_members, live->second.members)) {
			return {};
		}
		// Somebody it does not have, and only people a scene that finished here had.
		if (FitsFinished(a_location, a_members, a_now)) {
			return "the late end of the scene before this one"sv;
		}
		return {};
	}

	void ForeignScenes::Pump()
	{
		// Read BEFORE our lock, as everywhere here.
		const bool faces = FacesOn();
		const auto dazed = Expressions::GetSingleton().DazedSeconds();
		const auto schedule = Expressions::GetSingleton().IntensitySchedule();

		std::vector<Retired>                        retired;
		std::vector<Order>                          out;
		std::vector<std::pair<std::uint32_t, int>>  heat;
		std::vector<std::uint32_t>                  held;
		{
			NamedLock lock{ _lock, "foreign scenes" };
			if (!lock) {
				return;
			}
			const auto now = Clock::now();
			std::erase_if(_finished, [&](const Finished& f) { return SecondsBetween(f.at, now) >= kFinishedSeconds; });
			if (_scenes.empty()) {
				return;
			}

			for (auto it = _scenes.begin(); it != _scenes.end();) {
				auto& scene = it->second;
				if (scene.ended) {
					if (SecondsBetween(scene.endedAt, now) >= dazed) {
						retired.push_back(Retire(scene, false, "the afterglow is over", nullptr));
						it = _scenes.erase(it);
						continue;
					}
				} else if (!scene.animating) {
					if (SecondsBetween(scene.startedAt, now) >= kNeverAnimatedSeconds) {
						// A start whose scene never played -- and whose end never reached us.
						// Anybody it took over from another scene is cleared here.
						logger::warn("foreign scene {:08X}: not animating {:.0f}s after it started - taken for never "
									 "begun, and it leaves nothing",
							static_cast<std::uint32_t>(scene.location), kNeverAnimatedSeconds);
						retired.push_back(Retire(scene, false, "it never began to animate", nullptr));
						_quiet.insert_or_assign(scene.location, QuietMark{ Quiet::kNeverAnimated, IdsOf(scene.members) });
						it = _scenes.erase(it);
						continue;
					}
				} else if (scene.npcControlled && scene.duration > 0.0f && scene.stages <= 1 &&
						   SecondsBetween(scene.lastEventAt, now) >= OverdueAfter(scene.duration)) {
					// Only a TIMED scene can be overdue: a tree ends through its own exit,
					// and a looping one when somebody stops it -- neither has a length to
					// be past, and clearing one mid-act would be the worse mistake. And only
					// an NPC-controlled one: that is where finding H was measured, and a
					// player can hold a scene of their own open as long as they like.
					logger::warn("foreign scene {:08X}: long past its {:.0f}s with no end event - taken for over",
						static_cast<std::uint32_t>(scene.location), scene.duration);
					retired.push_back(Retire(scene, true, "its end never arrived", nullptr));
					// Until its end or a new scene here, not on a timer: if it was in fact
					// still running, its end would otherwise leave the cum a second time.
					_quiet.insert_or_assign(scene.location, QuietMark{ Quiet::kOverdue, IdsOf(scene.members) });
					it = _scenes.erase(it);
					continue;
				} else if (faces) {
					// A timed or looping scene builds with time, so its face is re-read on
					// every poll and not only when an event arrives.
					Wear(scene, FaceNow(scene, schedule), out, heat, held);
				}
				++it;
			}
		}

		Carry(retired);
		auto& expressions = Expressions::GetSingleton();
		for (const auto formID : held) {
			expressions.HoldForeign(formID);
		}
		expressions.RaiseHeat(heat, out);
		Expressions::Send(out);
	}

	void ForeignScenes::Reset()
	{
		NamedLock lock{ _lock, "foreign scenes" };
		if (!_scenes.empty()) {
			logger::info("foreign scenes: {} forgotten at a load - their faces are the wearing list's to clear",
				_scenes.size());
		}
		_scenes.clear();
		_ignored.clear();
		_finished.clear();
		_quiet.clear();
	}
}

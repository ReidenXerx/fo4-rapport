#include "Expressions.h"

#include "PapyrusLink.h"

namespace
{
	[[nodiscard]] std::string Lower(std::string_view a_text)
	{
		std::string out{ a_text };
		std::ranges::transform(out, out.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return out;
	}

	// Split on ANY non-alphanumeric, which is what makes this work on what
	// actually arrives. Papyrus hands us `akArgs[3] as String` -- the coercion of
	// a Var holding a string array -- so the text carries brackets, quotes and
	// commas. Treating all of them as separators is why the same parser reads it
	// correctly in Aftermath, where it has been doing so in game.
	[[nodiscard]] std::vector<std::string> SplitTags(std::string_view a_text)
	{
		std::vector<std::string> tags;
		std::string              current;
		for (const char c : a_text) {
			if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
				current.push_back(c);
			} else if (!current.empty()) {
				tags.push_back(Lower(current));
				current.clear();
			}
		}
		if (!current.empty()) {
			tags.push_back(Lower(current));
		}
		return tags;
	}

	// Any tag naming an act. A scene that only ever shows these is kissing, and
	// kissing does not want the face that sex wants.
	[[nodiscard]] bool LooksLikeSex(std::string_view a_tags)
	{
		static constexpr std::array kSex{
			"penisto"sv, "mouthto"sv, "handto"sv, "vaginal"sv, "anal"sv, "blowjob"sv,
			"cunnilingus"sv, "masturbation"sv, "straponto"sv, "dildoto"sv, "climax"sv,
			"analingus"sv, "anusto"sv, "vaginato"sv, "footto"sv
		};
		const auto lowered = Lower(a_tags);
		return std::ranges::any_of(kSex, [&](std::string_view needle) {
			return lowered.find(needle) != std::string::npos;
		});
	}
}

namespace RP
{
	Expressions& Expressions::GetSingleton() noexcept
	{
		static Expressions singleton;
		return singleton;
	}

	std::filesystem::path Expressions::ConfigPath()
	{
		return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "expressions.json";
	}

	void Expressions::Load()
	{
		NamedLock lock{ _lock, "expressions" };
		_steps.clear();

		const auto    path = ConfigPath();
		std::ifstream file{ path };
		if (!file) {
			_enabled = false;
			logger::warn("expressions: no {} - faces are left to the animation packs", path.string());
			return;
		}

		nlohmann::json document;
		try {
			file >> document;
		} catch (const std::exception& e) {
			_enabled = false;
			logger::error("expressions: {} is not valid json ({}) - the feature is off", path.string(), e.what());
			return;
		}

		_enabled = document.value("enabled", true);
		_dazedSeconds = document.value("dazedSeconds", 20.0f);
		_clearSet = document.value("clearSet", std::string{ "Rapport_Clear" });
		_afterSet = document.value("afterSet", std::string{ "Rapport_Dazed" });
		_kissSet = document.value("kissSet", std::string{ "Rapport_Kiss" });

		if (const auto steps = document.find("steps"); steps != document.end() && steps->is_array()) {
			for (const auto& step : *steps) {
				Step entry;
				entry.at = step.value("at", -1.0f);
				entry.set = step.value("set", std::string{});
				if (entry.at < 0.0f || entry.set.empty()) {
					logger::warn("expressions: a step is missing \"at\" or \"set\" - skipped");
					continue;
				}
				_steps.push_back(std::move(entry));
			}
		}

		// Out of order in the file would mean steps silently never firing, because
		// the pump only ever looks at the next one.
		std::ranges::sort(_steps, [](const Step& a, const Step& b) { return a.at < b.at; });

		if (!_enabled) {
			logger::info("expressions: switched off in expressions.json");
			return;
		}
		if (_steps.empty()) {
			_enabled = false;
			logger::warn("expressions: no steps were read - nothing could ever be applied, so it is off");
			return;
		}

		std::string schedule;
		for (const auto& step : _steps) {
			if (!schedule.empty()) {
				schedule += ", ";
			}
			schedule += std::format("{:.0f}% {}", step.at * 100.0f, step.set);
		}
		logger::info("expressions: {}; then {} for {:.0f}s, then {}",
			schedule, _afterSet, _dazedSeconds, _clearSet);
	}

	void Expressions::OnSceneStarted(std::uint32_t a_first, std::uint32_t a_second, float a_durationSeconds)
	{
		NamedLock lock{ _lock, "expressions" };
		if (!_enabled) {
			return;
		}

		_first = a_first;
		_second = a_second;
		_duration = (std::max)(a_durationSeconds, 1.0f);
		_startedAt = std::chrono::steady_clock::now();
		_running = true;
		_stoodDown = false;
		_dazing = false;
		_nextStep = 0;
		_tags.clear();
		_sawSexTag = false;

		logger::info("expressions: driving {:08X} and {:08X} over {:.0f}s", a_first, a_second, _duration);
	}

	void Expressions::NoteTags(std::string_view a_tags)
	{
		NamedLock lock{ _lock, "expressions" };
		if (!_tags.empty()) {
			_tags.push_back(',');
		}
		_tags.append(a_tags);
		if (!_sawSexTag && LooksLikeSex(a_tags)) {
			_sawSexTag = true;
		}

		// Only an animation that names an act moves this. AAF sends a tag list for
		// transitions and idles too, and letting one of those overwrite the act
		// would blank the face in the middle of the scene.
		if (!FaceForAct(a_tags, 2).empty()) {
			_liveAct.assign(a_tags);
		}
	}

	std::string Expressions::LiveAct() const
	{
		NamedLock lock{ _lock, "expressions" };
		return _liveAct;
	}

	std::string_view Expressions::FaceForAct(std::string_view a_actTags, int a_intensity)
	{
		const auto tags = SplitTags(a_actTags);

		// SUBSTRING, not equality. Pack authors do not agree on tag spelling and
		// never will: tonight's log carried "SEUKissing" -- one mod prefixing its
		// own name onto a standard tag -- and exact matching only found the act
		// because a bare "KISSING" happened to sit beside it. Elsewhere the same
		// act appears as CLIMAX and CLIMAXM.
		//
		// So match loosely and let a near miss land on the generous side. The cost
		// of the two directions is not symmetric: a slightly-too-expressive face
		// during sex reads as enthusiasm, and a stony calm one reads as broken.
		const auto any = [&](std::initializer_list<std::string_view> needles) {
			return std::ranges::any_of(tags, [&](const std::string& tag) {
				return std::ranges::any_of(needles, [&](std::string_view n) {
					return tag.find(n) != std::string::npos;
				});
			});
		};
		const auto pleasure = [&]() -> std::string_view {
			switch (a_intensity) {
			case 1:
				return "Rapport_Pleasure_1"sv;
			case 2:
				return "Rapport_Pleasure_2"sv;
			default:
				return "Rapport_Pleasure_3"sv;
			}
		};

		// A climax outranks everything, including the stage. This is the moment the
		// whole scene is for and AAF is the only thing that knows when it arrives.
		if (any({ "climax"sv, "orgasm"sv })) {
			return "Rapport_Climax"sv;
		}

		// Mouth working around something.
		//
		// MouthToMouth must not reach here -- it is kissing, and an open-jawed
		// blowjob face on a kiss is this same mistake pointed the other way. It is
		// excluded explicitly rather than by leaving "mouthto" out, because
		// "mouthto" is exactly how MouthToVagina and MouthToPenis are spelled.
		const auto kissOnly = std::ranges::all_of(tags, [](const std::string& tag) {
			return tag.find("mouthto") == std::string::npos ||
			       tag.find("mouthtomouth") != std::string::npos;
		});
		// "tongueto", "rimjob" and "licking" are here because the install has them
		// and nothing else would catch them: BP70 spells a rimjob
		// "RimJob,TongueToAnus" with no mouth tag anywhere, so an exact list built
		// from the acts you thought of leaves that scene stony-faced.
		if (any({ "blowjob"sv, "cunnilingus"sv, "analingus"sv, "fellatio"sv, "irrumatio"sv,
				  "oral"sv, "tomouth"sv, "69"sv, "tongueto"sv, "rimjob"sv, "rimming"sv,
				  "licking"sv }) ||
			(!kissOnly && any({ "mouthto"sv }))) {
			return "Rapport_Oral"sv;
		}

		if (any({ "penisto"sv, "vaginal"sv, "anal"sv, "vaginato"sv, "anusto"sv, "strapon"sv,
				  "dildo"sv, "handjob"sv, "handto"sv, "footto"sv, "fingering"sv, "titfuck"sv,
				  "masturbat"sv, "spanking"sv })) {
			return pleasure();
		}

		// Foreplay with no penetration yet.
		if (any({ "kissing"sv, "mouthtomouth"sv, "foreplay"sv, "tease"sv, "grope"sv, "fondle"sv })) {
			return "Rapport_Kiss"sv;
		}

		// Nothing named an act we know -- but if the tags look sexual AT ALL, that
		// is a pack spelling something in a way nobody anticipated, not a scene
		// where nothing is happening. Give a pleasure face at the story's own
		// intensity rather than leaving a blank one on during sex.
		if (LooksLikeSex(a_actTags)) {
			return pleasure();
		}

		// Last resort, and the most author-independent signal there is: AAF's own
		// arousal number. Nearly every position in this install carries Stim0..Stim9
		// regardless of which pack wrote it, so a high one says these two are
		// worked up even when no tag names what they are doing -- and the install
		// has positions like "F_M, DoubleBed, FromFront, Stim3, Love5" with no act
		// tag at all, which is plainly sex and was getting a blank face.
		//
		// SFW and NonSex veto it outright. A cuddle at Stim1 is meant to look calm,
		// and overriding an author who said so is the one place this tolerance would
		// do harm rather than good.
		if (!any({ "sfw"sv, "nonsex"sv })) {
			for (const auto& tag : tags) {
				if (!tag.starts_with("stim") || tag.size() < 5) {
					continue;
				}
				const auto digit = tag[4];
				if (digit >= '3' && digit <= '9') {
					return pleasure();
				}
			}
		}

		// Genuinely says nothing sexual -- a walk, an idle, a transition. Change
		// nothing, because here a neutral face is the correct one.
		return {};
	}

	void Expressions::Collect(std::string_view a_setID, std::vector<Order>& a_out)
	{
		for (const auto formID : { _first, _second }) {
			if (formID == 0) {
				continue;
			}
			a_out.push_back(Order{ Order::Kind::kApplyExpression, formID, std::string{ a_setID } });

			if (std::ranges::find(_wearing, formID) == _wearing.end()) {
				_wearing.push_back(formID);
			}
		}
	}

	void Expressions::Pump()
	{
		// Everything decided under the lock; nothing SENT under it. Calling into
		// PapyrusLink while holding this mutex is where the poll stopped, and the
		// rule that prevents it is simply: never call another subsystem while
		// holding your own lock.
		std::vector<Order> outgoing;

		{
			NamedLock lock{ _lock, "expressions" };
			if (!_enabled) {
				return;
			}

			const auto now = std::chrono::steady_clock::now();

			// Before anything else: a face left on somebody by a session that is
			// over. This is the whole reason the wearing list is in the save.
			if (_clearPending) {
				_clearPending = false;
				CollectClear(outgoing, "a save was made while a scene was running");
			}

			if (_running && !_stoodDown) {
				const auto elapsed =
					std::chrono::duration<float>{ now - _startedAt }.count();
				const auto fraction = elapsed / _duration;

				// A scene with no act tag at all is kissing or foreplay, and gets
				// one face for its whole length rather than a build to a climax it
				// is never going to have. Waiting for the first step's moment
				// before deciding gives the animation time to say what it is.
				if (!_sawSexTag && !_tags.empty()) {
					if (_nextStep == 0 && fraction >= _steps.front().at) {
						logger::info("expressions: no act tag in this scene - holding {}", _kissSet);
						Collect(_kissSet, outgoing);
						_nextStep = _steps.size();   // nothing further; hold this face
					}
				} else {
					while (_nextStep < _steps.size() && fraction >= _steps[_nextStep].at) {
						// Logged BEFORE it is queued. If this ever stops working
						// again, the log says which side of the queue it died on
						// rather than leaving it to be reasoned about.
						logger::info(
							"expressions: {:.0f}% through - {}",
							fraction * 100.0f, _steps[_nextStep].set);
						Collect(_steps[_nextStep].set, outgoing);
						++_nextStep;
					}
				}
			} else if (_dazing) {
				if (std::chrono::duration<float>{ now - _endedAt }.count() >= _dazedSeconds) {
					_dazing = false;
					// Clear EVERYONE on the list, not just this scene's two. If an
					// earlier scene ended badly its actors are still on it, and
					// this is the only thing that ever takes a face off.
					CollectClear(outgoing, "the afterglow is over");
				}
			}
		}

		Send(outgoing);
	}

	void Expressions::Send(const std::vector<Order>& a_orders)
	{
		auto& link = PapyrusLink::GetSingleton();
		for (const auto& order : a_orders) {
			link.QueueOrder(order);
		}
	}

	void Expressions::CollectClear(std::vector<Order>& a_out, std::string_view a_why)
	{
		if (_wearing.empty()) {
			return;
		}
		for (const auto formID : _wearing) {
			a_out.push_back(Order{ Order::Kind::kClearExpression, formID, _clearSet });
		}
		logger::info("expressions: clearing {} face(s) - {}", _wearing.size(), a_why);
		_wearing.clear();
	}

	void Expressions::StandDown()
	{
		NamedLock lock{ _lock, "expressions" };
		_stoodDown = true;
		logger::info("expressions: a scenario is driving the faces this scene - schedule stood down");
	}

	void Expressions::OnSceneEnded()
	{
		std::vector<Order> outgoing;
		{
			NamedLock lock{ _lock, "expressions" };
			if (!_enabled || !_running) {
				return;
			}

			_running = false;
			_endedAt = std::chrono::steady_clock::now();
			_dazing = true;
			logger::info("expressions: scene over - {} for {:.0f}s, then clearing", _afterSet, _dazedSeconds);
			Collect(_afterSet, outgoing);
		}
		Send(outgoing);
	}

	void Expressions::ClearEveryone(std::string_view a_why)
	{
		std::vector<Order> outgoing;
		{
			NamedLock lock{ _lock, "expressions" };
			CollectClear(outgoing, a_why);
		}
		Send(outgoing);
	}

	std::vector<std::uint32_t> Expressions::Wearing() const
	{
		NamedLock lock{ _lock, "expressions" };
		return _wearing;
	}

	void Expressions::RestoreWearing(std::vector<std::uint32_t> a_wearing)
	{
		NamedLock lock{ _lock, "expressions" };
		_wearing = std::move(a_wearing);
		_clearPending = !_wearing.empty();
		// Deliberately not queued here: the bridge is not listening yet. The first
		// pump after the handshake does it, and the log says why.
		if (!_wearing.empty()) {
			logger::warn(
				"expressions: {} actor(s) were wearing a Rapport face when this save was made - "
				"they will be cleared as soon as the bridge is listening",
				_wearing.size());
		}
	}

	void Expressions::Reset()
	{
		NamedLock lock{ _lock, "expressions" };
		_running = false;
		_dazing = false;
		_clearPending = false;
		_nextStep = 0;
		_first = 0;
		_second = 0;
		_tags.clear();
		_sawSexTag = false;
		_wearing.clear();
	}
}

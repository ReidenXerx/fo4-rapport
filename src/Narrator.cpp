#include "Narrator.h"

#include "Barks.h"
#include "Candidates.h"
#include "Config.h"
#include "Ledger.h"
#include "McmSettings.h"
#include "Orders.h"
#include "PapyrusLink.h"
#include "Pairing.h"

namespace RP
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		std::filesystem::path ConfigPath()
		{
			return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "narrator.json";
		}

		[[nodiscard]] std::uint64_t Key(std::uint32_t a_first, std::uint32_t a_second) noexcept
		{
			return a_first < a_second ? (static_cast<std::uint64_t>(a_first) << 32) | a_second
			                          : (static_cast<std::uint64_t>(a_second) << 32) | a_first;
		}

		[[nodiscard]] std::string NameOf(std::uint32_t a_formID)
		{
			auto*       actor = RE::TESForm::GetFormByID<RE::Actor>(a_formID);
			const char* name = actor ? actor->GetDisplayFullName() : nullptr;
			return name && *name ? std::string{ name } : std::string{ "someone" };
		}

		// "+0.45" / "-0.90": the sign always shown, so a list of parts reads as a sum.
		[[nodiscard]] std::string Signed(float a_value)
		{
			return std::format("{}{:.2f}", a_value < 0.0f ? "-" : "+", std::abs(a_value));
		}

		// What the one who starts it does, by persona. The persona is the thing the
		// player can hear in the barks, so the narration uses the same four voices.
		[[nodiscard]] std::string_view Verb(std::string_view a_persona, bool a_indoors)
		{
			if (a_persona == "romantic") {
				return "drift together";
			}
			if (a_persona == "vulgar") {
				return a_indoors ? "don't waste any time" : "don't bother finding a room";
			}
			if (a_persona == "mercantile") {
				return "come to an arrangement";
			}
			return "slip away together";
		}

		[[nodiscard]] std::string Join(const std::vector<std::string>& a_parts)
		{
			if (a_parts.empty()) {
				return {};
			}
			if (a_parts.size() == 1) {
				return a_parts[0];
			}
			std::string out;
			for (std::size_t i = 0; i < a_parts.size(); ++i) {
				out += i == 0 ? "" : (i + 1 == a_parts.size() ? ", and " : ", ");
				out += a_parts[i];
			}
			return out;
		}
	}

	Narrator& Narrator::GetSingleton() noexcept
	{
		static Narrator singleton;
		return singleton;
	}

	void Narrator::Load()
	{
		nlohmann::json document = nlohmann::json::object();
		if (std::ifstream file{ ConfigPath() }; file) {
			try {
				file >> document;
			} catch (const std::exception& e) {
				logger::error("narrator: {} is not valid json ({}) - using the defaults", ConfigPath().string(), e.what());
				document = nlohmann::json::object();
			}
		}
		McmSettings::Overlay("Narrator", document);

		NamedLock lock{ _lock, "narrator" };
		_enabled = McmSettings::ReadBool(document, "enabled", true);
		_sceneStarts = McmSettings::ReadBool(document, "sceneStarts", true);
		_nearMisses = McmSettings::ReadBool(document, "nearMisses", false);
		_relationshipTurns = McmSettings::ReadBool(document, "relationshipTurns", false);
		_bystanders = McmSettings::ReadBool(document, "bystanders", false);
		_addonLines = McmSettings::ReadBool(document, "addonLines", true);
		_numbers = McmSettings::ReadBool(document, "numbers", true);
		_nearMissCooldown = document.value("nearMissCooldownSeconds", 300.0f);
		_pairMissCooldown = document.value("pairMissCooldownSeconds", 1800.0f);
		_historySize = (std::max)(std::size_t{ 1 }, document.value("historySize", std::size_t{ 12 }));
		logger::info("narrator: {} - scene starts {}, near misses {}, relationship turns {}, bystanders {}, addon moments {}, numbers {}",
			_enabled ? "on" : "OFF", _sceneStarts, _nearMisses, _relationshipTurns, _bystanders, _addonLines, _numbers);
	}

	void Narrator::AddBonus(std::uint32_t a_first, std::uint32_t a_second, std::string_view a_label, float a_value)
	{
		if (a_first == 0 || a_second == 0 || a_label.empty()) {
			return;
		}
		NamedLock lock{ _lock, "narrator" };
		auto& pending = _pending[Key(a_first, a_second)];
		// A report older than two minutes belongs to a decision that was never acted on.
		if (Clock::now() - pending.at > std::chrono::minutes{ 5 }) {
			pending.bonuses.clear();
		}
		pending.at = Clock::now();
		// Lower-cased: Papyrus pools strings CASE-INSENSITIVELY, so "bond" can arrive
		// as "Bond" if anything else in the game ever used that word - seen on the
		// first narration, 2026-09-22 - and every comparison below would miss it.
		std::string label{ a_label };
		std::ranges::transform(label, label.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		// A label REPLACES its earlier report: a request that was declined and asked
		// again next poll used to list every part twice and double the total.
		const auto same = std::ranges::find(pending.bonuses, label, &Bonus::label);
		if (same != pending.bonuses.end()) {
			same->value = a_value;
		} else {
			pending.bonuses.push_back(Bonus{ std::move(label), a_value });
		}
	}

	void Narrator::OnRequestAccepted(std::uint32_t a_first, std::uint32_t a_second)
	{
		const auto offer = Candidates::GetSingleton().Find(a_first, a_second);
		NamedLock  lock{ _lock, "narrator" };
		auto&      pending = _pending[Key(a_first, a_second)];
		pending.offer = offer;
		pending.at = Clock::now();
	}

	void Narrator::OnSceneRequested(std::uint32_t a_first, std::uint32_t a_second, std::string_view a_scenario)
	{
		std::vector<Bonus>              bonuses;
		std::optional<Candidates::Offer> kept;
		bool               speak = false;
		bool               numbers = false;
		{
			NamedLock lock{ _lock, "narrator" };
			_lastScene = { a_first, a_second };
			if (const auto it = _pending.find(Key(a_first, a_second)); it != _pending.end()) {
				if (Clock::now() - it->second.at <= std::chrono::minutes{ 5 }) {
					bonuses = std::move(it->second.bonuses);
					kept = it->second.offer;
				}
				_pending.erase(it);
			}
			speak = _enabled && _sceneStarts;
			numbers = _numbers;
		}
		if (!speak) {
			return;
		}

		const auto  offer = kept ? kept : Candidates::GetSingleton().Find(a_first, a_second);
		const auto& weights = Config::GetSingleton().Weights();
		auto&       ledger = Ledger::GetSingleton();
		const auto  persona = std::string{ Barks::GetSingleton().PersonaOf(a_first) };
		// Narrated at scene START, after the bridge has seeded the store, so the
		// ledger is usually current here. The addon's markers still win: they are
		// what it decided on.
		float bond = ledger.Bond(a_first, a_second);
		bool  married = ledger.IsPartner(a_first, a_second);
		// Markers (a leading '_') carry facts for the WORDS and are never printed:
		// "_bond" is the raw bond (the "bond" part is a capped score share, not a
		// bond), "_couple" says they are partners, "_score" is the base score the
		// addon decided on.
		std::optional<float> decidedOn;
		for (const auto& bonus : bonuses) {
			if (bonus.label == "_bond") {
				bond = bonus.value;
			} else if (bonus.label == "_couple" || bonus.label == "couple") {
				married = true;
			} else if (bonus.label == "_score") {
				decidedOn = bonus.value;
			}
		}

		// THE WHY, in words: at most three reasons, the relationship first, then the
		// crowd, then the time and place - the order a person would give them in.
		std::vector<std::string> reasons;
		if (married) {
			reasons.emplace_back("they're a couple");
		} else if (bond >= 0.5f) {
			reasons.emplace_back("they're close");
		} else if (bond >= 0.15f) {
			reasons.emplace_back("they get along");
		} else if (bond <= -0.15f) {
			reasons.emplace_back("bad blood and all");
		}
		for (const auto& bonus : bonuses) {
			if (bonus.label == "own place" && bonus.value > 0.0f) {
				reasons.emplace_back("they're on their own turf");
			} else if (bonus.label == "spoken for" && bonus.value < 0.0f) {
				reasons.emplace_back("never mind that one of them is spoken for");
			}
		}
		if (offer) {
			const auto& s = offer->signals;
			if (s.observers == 0) {
				reasons.emplace_back("nobody's watching");
			} else if (s.observers <= weights.observerTolerance) {
				reasons.emplace_back("hardly anyone's around");
			} else if (s.observers == 1) {
				reasons.emplace_back("someone's watching and they don't care");
			} else {
				reasons.emplace_back(std::format("{} people are watching and they don't care", s.observers));
			}
			if (s.night) {
				reasons.emplace_back("it's late");
			} else if (s.interior) {
				reasons.emplace_back("behind closed doors");
			}
		}
		if (reasons.size() > 3) {
			reasons.resize(3);
		}

		const bool indoors = offer && offer->signals.interior;
		auto headline = std::format("{} and {} {}", NameOf(a_first), NameOf(a_second), Verb(persona, indoors));
		if (!reasons.empty()) {
			headline += " - " + Join(reasons);
		}
		headline += ".";

		// THE NUMBERS: Rapport's parts exactly as they were summed (Breakdown is the
		// function RankPairs uses), then each addon's own report.
		std::string line;
		if (numbers) {
			std::vector<std::string> parts;
			float                    total = 0.0f;
			// The breakdown only when the published offer is the one the addon decided
			// on: Rapport republishes every 20s, and a stale or missing offer printed a
			// total that was never compared with the bar.
			const bool same = offer && (!decidedOn || std::abs(offer->score - *decidedOn) < 0.005f);
			if (!same && decidedOn) {
				parts.push_back(std::format("rapport {}", Signed(*decidedOn)));
				total = *decidedOn;
			}
			if (same) {
				const auto p = Breakdown(offer->signals, weights);
				const auto add = [&](std::string_view a_label, float a_value) {
					if (std::abs(a_value) >= 0.005f) {
						parts.push_back(std::format("{} {}", a_label, Signed(a_value)));
					}
				};
				add("near", p.proximity);
				add("same faction", p.faction);
				add("indoors", p.interior);
				add("night", p.night);
				add(std::format("{} watching", offer->signals.observers), p.crowd);
				add("player near", p.player);
				total = offer->score;
			}
			for (const auto& bonus : bonuses) {
				if (!bonus.label.empty() && bonus.label.front() != '_' && bonus.label != "couple" && std::abs(bonus.value) >= 0.005f) {
					parts.push_back(std::format("{} {}", bonus.label, Signed(bonus.value)));
					total += bonus.value;
				}
			}
			// No '|' and no brackets: FallUI's HUD splits a notification at '|' into a
			// title and a body, and the vanilla HUD prints it literally.
			std::string sum;
			for (const auto& part : parts) {
				sum += sum.empty() ? part : ", " + part;
			}
			line = same || decidedOn || !bonuses.empty()
			           ? std::format("{} - score {:.2f} = {}", a_scenario.empty() ? "scene" : a_scenario, total,
			                 sum.empty() ? std::string{ "0" } : sum)
			           : std::format("{} - asked for directly", a_scenario.empty() ? "scene" : a_scenario);
		}
		Emit(headline, line);
	}

	void Narrator::OnNearMiss(std::uint32_t a_first, std::uint32_t a_second, std::string_view a_why, float a_score,
		float a_bar)
	{
		{
			NamedLock lock{ _lock, "narrator" };
			if (!_enabled || !_nearMisses || a_why.empty()) {
				return;
			}
			const auto now = Clock::now();
			// Chatty is the failure here: a near miss is true every poll for as long as
			// the two stand there. Once in a while overall, and rarely for the same two.
			if (_lastMiss && now - *_lastMiss < std::chrono::duration<float>{ _nearMissCooldown }) {
				return;
			}
			const auto key = Key(a_first, a_second);
			if (const auto it = _missedAt.find(key);
				it != _missedAt.end() && now - it->second < std::chrono::duration<float>{ _pairMissCooldown }) {
				return;
			}
			_lastMiss = now;
			_missedAt[key] = now;
		}
		Emit(std::format("{} and {} would, but {}.", NameOf(a_first), NameOf(a_second), a_why),
			_numbers ? std::format("score {:.2f}, needs {:.2f}", a_score, a_bar) : std::string{});
	}

	void Narrator::OnBondChanged(std::uint32_t a_first, std::uint32_t a_second, float a_before, float a_after,
		std::uint32_t a_scenes, bool a_fromScene)
	{
		{
			NamedLock lock{ _lock, "narrator" };
			if (!_enabled || !_relationshipTurns) {
				return;
			}
		}
		const auto a = NameOf(a_first);
		const auto b = NameOf(a_second);
		std::string headline;
		// Crossings only, both ways: a number moving is not news, a line crossed is.
		constexpr std::array<std::pair<float, std::string_view>, 3> up{ {
			{ 0.75f, "are inseparable now" }, { 0.5f, "are close now" }, { 0.25f, "are getting close" } } };
		for (const auto& [line, words] : up) {
			if (a_before < line && a_after >= line) {
				headline = std::format("{} and {} {}.", a, b, words);
				break;
			}
		}
		if (headline.empty() && a_before > -0.25f && a_after <= -0.25f) {
			headline = std::format("{} and {} have fallen out.", a, b);
		}
		// Not for a couple: a married pair's first scene in Rapport's books is not
		// their first time, and saying so read as a joke (2026-09-22, the Longs).
		if (headline.empty() && a_fromScene && a_scenes == 1 && !Ledger::GetSingleton().IsPartner(a_first, a_second)) {
			headline = std::format("A first time for {} and {}.", a, b);
		}
		if (headline.empty()) {
			return;
		}
		Emit(headline, _numbers ? std::format("bond {} -> {}, {} scene(s) together", Signed(a_before), Signed(a_after),
		                              a_scenes)
		                        : std::string{});
	}

	void Narrator::OnBystander(std::uint32_t a_watcher, bool a_heardOnly)
	{
		std::pair<std::uint32_t, std::uint32_t> scene;
		{
			NamedLock lock{ _lock, "narrator" };
			if (!_enabled || !_bystanders) {
				return;
			}
			scene = _lastScene;
		}
		if (scene.first == 0) {
			return;
		}
		Emit(std::format("{} {} {} and {}.", NameOf(a_watcher), a_heardOnly ? "hears" : "has noticed",
			     NameOf(scene.first), NameOf(scene.second)),
			{});
	}

	void Narrator::OnAddonLine(std::uint32_t a_first, std::uint32_t a_second, std::string_view a_headline,
		std::string_view a_numbers)
	{
		bool numbers = false;
		{
			NamedLock lock{ _lock, "narrator" };
			if (!_enabled || !_addonLines || a_headline.empty()) {
				return;
			}
			numbers = _numbers;
		}
		// The addon writes the words; the names are ours to fill, because Papyrus has
		// no name accessor of its own and the Narrator already knows how to ask.
		const auto fill = [&](std::string_view a_text) {
			std::string out{ a_text };
			for (const auto& [token, formID] : { std::pair{ std::string_view{ "{first}" }, a_first },
				                                  std::pair{ std::string_view{ "{second}" }, a_second } }) {
				for (auto at = out.find(token); at != std::string::npos; at = out.find(token, at)) {
					const auto name = NameOf(formID);
					out.replace(at, token.size(), name);
					at += name.size();
				}
			}
			return out;
		};
		Emit(fill(a_headline), numbers ? fill(a_numbers) : std::string{});
	}

	std::string Narrator::History() const
	{
		NamedLock lock{ _lock, "narrator" };
		if (_history.empty()) {
			return "Nothing narrated yet this session.";
		}
		std::string out;
		for (const auto& entry : _history) {
			out += out.empty() ? entry : "\n" + entry;
		}
		return out;
	}

	void Narrator::Emit(const std::string& a_headline, const std::string& a_numbers)
	{
		{
			NamedLock lock{ _lock, "narrator" };
			// The game clock, not the wall clock: "02:16" is when it happened in the world.
			std::string stamp;
			if (const auto calendar = RE::Calendar::GetSingleton(); calendar && calendar->gameHour) {
				const auto hour = calendar->gameHour->GetValue();
				stamp = std::format("{:02d}:{:02d} ", static_cast<int>(hour), static_cast<int>((hour - std::floor(hour)) * 60.0f));
			}
			_history.push_back(stamp + a_headline);
			while (_history.size() > _historySize) {
				_history.pop_front();
			}
		}
		logger::info("narrator: {}{}{}", a_headline, a_numbers.empty() ? "" : " ", a_numbers);
		// ONE order, both lines: two orders ran on two CallFunctionNoWait stacks and
		// were not guaranteed to arrive in order, and cost two of the drain budget.
		PapyrusLink::GetSingleton().QueueOrder(Order{ Order::Kind::kNarrate, 0, a_headline, a_numbers });
	}
}

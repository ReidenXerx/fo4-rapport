#include "Aftermath.h"

#include "Ledger.h"
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

	// AAF hands tags over as an array printed into one string. Splitting on
	// anything that is not part of a tag means the same code reads
	// "A,B,C", "[A, B, C]" and "\"A\",\"B\"" without caring which it got.
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
}

namespace RP
{
	Aftermath& Aftermath::GetSingleton() noexcept
	{
		static Aftermath singleton;
		return singleton;
	}

	std::filesystem::path Aftermath::ConfigPath()
	{
		return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "aftermath.json";
	}

	void Aftermath::Load()
	{
		std::scoped_lock lock{ _lock };
		_rules.clear();

		const auto  path = ConfigPath();
		std::ifstream file{ path };
		if (!file) {
			_enabled = false;
			logger::warn("aftermath: no {} - scenes will leave nothing behind", path.string());
			return;
		}

		nlohmann::json document;
		try {
			file >> document;
		} catch (const std::exception& e) {
			_enabled = false;
			logger::error("aftermath: {} is not valid json ({}) - the feature is off", path.string(), e.what());
			return;
		}

		_enabled = document.value("enabled", true);
		_hours = document.value("hours", 12.0f);
		_requireClimax = document.value("requireClimax", false);

		if (const auto tags = document.find("tags"); tags != document.end() && tags->is_object()) {
			for (const auto& [tag, sets] : tags->items()) {
				if (!sets.is_array()) {
					continue;
				}
				std::vector<std::string> ids;
				for (const auto& set : sets) {
					if (set.is_string()) {
						ids.push_back(set.get<std::string>());
					}
				}
				if (!ids.empty()) {
					_rules.emplace_back(Lower(tag), std::move(ids));
				}
			}
		}

		if (!_enabled) {
			logger::info("aftermath: switched off in aftermath.json");
			return;
		}
		if (_rules.empty()) {
			_enabled = false;
			logger::warn("aftermath: no tag rules were read - nothing could ever be applied, so it is off");
			return;
		}

		logger::info(
			"aftermath: {} tag rule(s), {:.0f} game hours{}",
			_rules.size(), _hours, _requireClimax ? ", climax required" : "");
	}

	void Aftermath::NoteTags(std::string_view a_tags)
	{
		std::scoped_lock lock{ _lock };
		if (!_sceneTags.empty()) {
			_sceneTags.push_back(',');
		}
		_sceneTags.append(a_tags);
	}

	std::vector<std::string> Aftermath::SetsFor(std::string_view a_tags) const
	{
		const auto tags = SplitTags(a_tags);
		if (tags.empty()) {
			return {};
		}

		if (_requireClimax) {
			const auto climaxed = std::ranges::any_of(tags, [](const std::string& tag) {
				return tag.rfind("climax", 0) == 0;
			});
			if (!climaxed) {
				return {};
			}
		}

		std::vector<std::string> sets;
		for (const auto& [tag, ids] : _rules) {
			if (std::ranges::find(tags, tag) == tags.end()) {
				continue;
			}
			for (const auto& id : ids) {
				if (std::ranges::find(sets, id) == sets.end()) {
					sets.push_back(id);
				}
			}
		}
		return sets;
	}

	void Aftermath::OnSceneEnded(std::uint32_t a_first, std::uint32_t a_second)
	{
		std::scoped_lock lock{ _lock };

		const auto tags = std::exchange(_sceneTags, {});
		if (!_enabled) {
			return;
		}

		if (tags.empty()) {
			// Not a silent nothing: a scene we heard nothing about is a different
			// state from a scene that was only kissing, and only one of them is a bug.
			logger::warn(
				"aftermath: the scene ended without a single animation tag reaching us - "
				"nothing applied, and that is a gap in what we were told, not a decision");
			return;
		}

		const auto sets = SetsFor(tags);
		if (sets.empty()) {
			logger::info("aftermath: nothing to leave behind (tags: {})", tags);
			return;
		}

		const auto now = Ledger::GameHours();
		if (now < 0.0f) {
			logger::warn("aftermath: no game clock yet - nothing applied");
			return;
		}
		const auto expires = now + _hours;

		std::string named;
		for (const auto& set : sets) {
			if (!named.empty()) {
				named += ", ";
			}
			named += set;
			Apply(a_first, set, expires);
			Apply(a_second, set, expires);
		}

		logger::info(
			"aftermath: {:08X} and {:08X} keep [{}] until hour {:.1f} (now {:.1f})",
			a_first, a_second, named, expires, now);
	}

	void Aftermath::Apply(std::uint32_t a_formID, const std::string& a_setID, float a_expiresAt)
	{
		// Already wearing this set: push the hour out rather than stacking a
		// second copy. Two marks for one set would queue two removals, and the
		// second would strip an overlay a later scene had just re-applied.
		for (auto& mark : _marks) {
			if (mark.formID == a_formID && mark.setID == a_setID) {
				mark.expiresAt = (std::max)(mark.expiresAt, a_expiresAt);
				mark.asked = false;   // ask again: a fresh scene should look fresh
				return;
			}
		}

		_marks.push_back(Mark{ a_formID, a_expiresAt, a_setID, false });
	}

	void Aftermath::Tick(const std::vector<std::uint32_t>& a_here)
	{
		const auto now = Ledger::GameHours();
		if (now < 0.0f) {
			return;
		}

		std::scoped_lock lock{ _lock };

		std::uint32_t expired = 0;
		std::uint32_t asked = 0;
		std::uint32_t waiting = 0;

		for (auto mark = _marks.begin(); mark != _marks.end();) {
			if (now >= mark->expiresAt) {
				// Removal goes out whether or not they are here. An overlay left on
				// someone who wandered off is the failure this whole feature exists
				// to prevent, and AAF takes the call either way.
				PapyrusLink::GetSingleton().QueueOrder(
					Order{ Order::Kind::kRemoveOverlay, mark->formID, mark->setID });
				mark = _marks.erase(mark);
				++expired;
				continue;
			}

			// Anything not yet asked for in this session is asked for as soon as
			// its owner is in front of us. That covers the first application and
			// the re-application after a load with one line, because they are the
			// same thing: the plugin has no memory of having asked.
			if (!mark->asked) {
				if (std::ranges::find(a_here, mark->formID) != a_here.end()) {
					PapyrusLink::GetSingleton().QueueOrder(
						Order{ Order::Kind::kApplyOverlay, mark->formID, mark->setID });
					mark->asked = true;
					++asked;
				} else {
					++waiting;
				}
			}
			++mark;
		}

		if (expired > 0 || asked > 0) {
			logger::info(
				"aftermath: hour {:.1f} - {} applied, {} expired, {} still standing, {} waiting for "
				"their owner to be nearby",
				now, asked, expired, _marks.size(), waiting);
		}
	}

	std::vector<Aftermath::Mark> Aftermath::Marks() const
	{
		std::scoped_lock lock{ _lock };
		return _marks;
	}

	void Aftermath::Restore(std::vector<Mark> a_marks)
	{
		std::scoped_lock lock{ _lock };
		_marks = std::move(a_marks);
		// Nothing is asked for here. The next tick does it, by which time the game
		// is actually running and the bridge is listening.
		for (auto& mark : _marks) {
			mark.asked = false;
		}
	}

	void Aftermath::Clear()
	{
		std::scoped_lock lock{ _lock };
		_marks.clear();
		_sceneTags.clear();
	}

	void Aftermath::RemoveEverything(std::string_view a_why)
	{
		std::scoped_lock lock{ _lock };
		if (_marks.empty()) {
			return;
		}

		auto& link = PapyrusLink::GetSingleton();
		for (const auto& mark : _marks) {
			link.QueueOrder(Order{ Order::Kind::kRemoveOverlay, mark.formID, mark.setID });
		}
		logger::warn("aftermath: removing all {} standing overlay(s) - {}", _marks.size(), a_why);
		_marks.clear();
	}

	std::size_t Aftermath::Size() const
	{
		std::scoped_lock lock{ _lock };
		return _marks.size();
	}
}

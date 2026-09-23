#include "Names.h"

#include "McmSettings.h"
#include "Traits.h"

namespace RP
{
	namespace
	{
		std::filesystem::path OverridePath()
		{
			return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "Rapport" / "names.json";
		}

		// Built in, so names work with no file deployed. The Commonwealth's own
		// register: the names on the pre-war census, which is what the people who
		// outlived it would still be called. names.json may replace any list.
		const std::vector<std::string>& DefaultFemale()
		{
			static const std::vector<std::string> names{
				"Agnes", "Alma", "Annie", "Beatrice", "Betty", "Bonnie", "Carol", "Clara", "Connie", "Darla",
				"Delia", "Dolores", "Dorothy", "Dottie", "Edna", "Eileen", "Elsie", "Esther", "Evelyn", "Faye",
				"Flora", "Frances", "Georgia", "Gladys", "Gloria", "Hazel", "Helen", "Ida", "Irene", "Iris",
				"Jean", "Joan", "Josie", "June", "Kitty", "Lena", "Lois", "Loretta", "Louise", "Lucille",
				"Mabel", "Maggie", "Marge", "Marion", "Martha", "Maxine", "Millie", "Minnie", "Myrtle", "Nell",
				"Nora", "Opal", "Pearl", "Peggy", "Rita", "Rosa", "Ruby", "Ruth", "Sadie", "Stella",
				"Thelma", "Vera", "Violet", "Wanda", "Wilma", "Winnie"
			};
			return names;
		}

		const std::vector<std::string>& DefaultMale()
		{
			static const std::vector<std::string> names{
				"Abe", "Al", "Amos", "Archie", "Arnie", "Art", "Barney", "Ben", "Bert", "Buck",
				"Carl", "Chester", "Clyde", "Dale", "Doyle", "Earl", "Eddie", "Elmer", "Ernie", "Floyd",
				"Frank", "Fred", "Gus", "Hank", "Harlan", "Herb", "Homer", "Hugh", "Ike", "Jack",
				"Jed", "Joe", "Lefty", "Leon", "Lester", "Lloyd", "Lou", "Mack", "Marty", "Mel",
				"Milt", "Moe", "Ned", "Norm", "Otis", "Pete", "Ralph", "Ray", "Roy", "Rusty",
				"Sal", "Sid", "Stan", "Ted", "Tom", "Vern", "Virgil", "Walt", "Wes", "Wilbur",
				"Willis", "Woody"
			};
			return names;
		}

		const std::vector<std::string>& DefaultSurnames()
		{
			static const std::vector<std::string> names{
				"Abbott", "Ackerman", "Baker", "Barlow", "Bishop", "Boyle", "Brennan", "Briggs", "Burke", "Carver",
				"Coleman", "Conway", "Crane", "Dalton", "Doyle", "Duffy", "Dunn", "Ellis", "Farrell", "Fenton",
				"Fisk", "Flynn", "Garrity", "Gibbs", "Grady", "Haskell", "Hayes", "Hodges", "Holt", "Hooper",
				"Kane", "Keller", "Kincaid", "Lowell", "Lynch", "Mahoney", "Marsh", "McCabe", "McGee", "Mercer",
				"Monroe", "Mulligan", "Nash", "Nolan", "Oakes", "O'Brien", "Pike", "Porter", "Quinn", "Rafferty",
				"Riggs", "Rourke", "Sawyer", "Shaw", "Slater", "Sloane", "Sullivan", "Tate", "Thorne", "Tully",
				"Vance", "Walsh", "Webb", "Whitaker", "Wolcott", "Yates"
			};
			return names;
		}

		// A mixer of its own: personas use a multiplicative hash and faithfulness
		// murmur's finaliser, and a name that correlated with either would make
		// every "Earl" secretly vulgar, which nobody decided.
		[[nodiscard]] std::uint32_t Mix(std::uint32_t a_value, std::uint32_t a_seed) noexcept
		{
			auto h = a_value ^ a_seed;
			h ^= h >> 15;
			h *= 0x2C1B3C6Du;
			h ^= h >> 12;
			h *= 0x297A2D39u;
			h ^= h >> 15;
			return h;
		}

		void ReadList(const nlohmann::json& a_doc, const char* a_key, std::vector<std::string>& a_out)
		{
			const auto it = a_doc.find(a_key);
			if (it == a_doc.end() || !it->is_array()) {
				return;
			}
			std::vector<std::string> read;
			for (const auto& entry : *it) {
				if (entry.is_string() && !entry.get<std::string>().empty()) {
					read.push_back(entry.get<std::string>());
				}
			}
			if (read.empty()) {
				logger::warn("names: names.json's \"{}\" has no names - keeping the built-in list", a_key);
				return;
			}
			a_out = std::move(read);
		}
	}

	Names& Names::GetSingleton() noexcept
	{
		static Names singleton;
		return singleton;
	}

	void Names::Load()
	{
		std::vector<std::string> female = DefaultFemale();
		std::vector<std::string> male = DefaultMale();
		std::vector<std::string> surnames = DefaultSurnames();
		nlohmann::json           document = nlohmann::json::object();
		document["enabled"] = true;
		if (std::ifstream file{ OverridePath() }; file) {
			try {
				nlohmann::json read;
				file >> read;
				if (read.is_object()) {
					ReadList(read, "female", female);
					ReadList(read, "male", male);
					ReadList(read, "surnames", surnames);
					if (const auto it = read.find("enabled"); it != read.end()) {
						document["enabled"] = *it;
					}
				}
			} catch (const std::exception& e) {
				logger::error("names: {} is not valid json ({}) - using the built-in lists", OverridePath().string(), e.what());
			}
		}
		McmSettings::Overlay("Names", document);

		NamedLock lock{ _lock, "names" };
		_enabled = McmSettings::ReadBool(document, "enabled", true);
		_female = std::move(female);
		_male = std::move(male);
		_surnames = std::move(surnames);
		logger::info("names: {} - {} female, {} male first names, {} surnames", _enabled ? "on" : "OFF",
			_female.size(), _male.size(), _surnames.size());
	}

	std::string Names::NameFor(std::uint32_t a_formID, bool a_female) const
	{
		NamedLock  lock{ _lock, "names" };
		const auto& first = a_female ? _female : _male;
		if (first.empty() || _surnames.empty()) {
			return {};
		}
		const auto stable = Traits::StableID(a_formID);
		return first[Mix(stable, 0xA5A5F00Du) % first.size()] + " " + _surnames[Mix(stable, 0x1B873593u) % _surnames.size()];
	}

	bool Names::Nameless(RE::Actor* a_actor)
	{
		if (!a_actor || a_actor == RE::PlayerCharacter::GetSingleton()) {
			return false;
		}
		auto* npc = a_actor->GetNPC();
		return npc && !npc->IsUnique();
	}

	std::string Names::Introduce(RE::Actor* a_actor)
	{
		if (!Nameless(a_actor)) {
			return {};
		}
		const auto formID = a_actor->GetFormID();
		const bool female = a_actor->GetNPC()->GetSex() == RE::SEX::kFemale;
		{
			NamedLock lock{ _lock, "names" };
			if (!_enabled || _introduced.contains(formID)) {
				return {};
			}
		}
		// Somebody else's name -- the player's own rename, another mod's -- is theirs
		// to keep. Only a stranger still wearing their base's label gets one of ours.
		if (a_actor->extraList && a_actor->extraList->HasType(RE::EXTRA_DATA_TYPE::kTextDisplayData)) {
			logger::info("names: {:08X} already carries a custom name - left as it is", formID);
			return {};
		}
		const auto name = NameFor(formID, female);
		if (name.empty()) {
			return {};
		}
		{
			NamedLock lock{ _lock, "names" };
			_introduced.insert(formID);
			_count = _introduced.size();
		}
		// The rename touches the reference's extra data, which is the main thread's.
		// Papyrus calls this from a VM thread, so it goes through F4SE's task queue,
		// the way the mailbox runs its verbs.
		if (auto* task = F4SE::GetTaskInterface()) {
			task->AddTask([formID, name] {
				if (auto* actor = RE::TESForm::GetFormByID<RE::Actor>(formID)) {
					Apply(actor, name);
				}
			});
		} else {
			Apply(a_actor, name);
		}
		logger::info("names: {:08X} introduced as {}", formID, name);
		return name;
	}

	bool Names::Apply(RE::Actor* a_actor, const std::string& a_name)
	{
		if (!a_actor || !a_actor->extraList || a_name.empty()) {
			return false;
		}
		if (a_actor->extraList->HasType(RE::EXTRA_DATA_TYPE::kTextDisplayData)) {
			return false;
		}
		a_actor->extraList->SetOverrideName(a_name.c_str());
		return true;
	}

	std::vector<std::uint32_t> Names::Introduced() const
	{
		NamedLock lock{ _lock, "names" };
		return { _introduced.begin(), _introduced.end() };
	}

	void Names::Restore(std::vector<std::uint32_t> a_introduced)
	{
		NamedLock lock{ _lock, "names" };
		_introduced.clear();
		_introduced.insert(a_introduced.begin(), a_introduced.end());
		_count = _introduced.size();
	}

	void Names::Clear()
	{
		NamedLock lock{ _lock, "names" };
		_introduced.clear();
		_count = 0;
	}

	void Names::Forget(std::uint32_t a_formID)
	{
		if (_count == 0) {
			return;
		}
		NamedLock lock{ _lock, "names" };
		if (_introduced.erase(a_formID) > 0) {
			_count = _introduced.size();
			logger::info("names: {:08X} died - forgotten, so the id cannot name somebody new", a_formID);
		}
	}

	void Names::OnLoaded(std::uint32_t a_formID)
	{
		if (_count == 0) {
			return;
		}
		{
			NamedLock lock{ _lock, "names" };
			if (!_enabled || !_introduced.contains(a_formID)) {
				return;
			}
		}
		auto* actor = RE::TESForm::GetFormByID<RE::Actor>(a_formID);
		if (!actor || !actor->GetNPC()) {
			return;
		}
		// Said, because it answers the open question: does the game keep a custom
		// name on an actor by itself? A line here means it did not.
		if (Apply(actor, NameFor(a_formID, actor->GetNPC()->GetSex() == RE::SEX::kFemale))) {
			logger::info("names: {:08X} loaded without their name - named again", a_formID);
		}
	}

	namespace
	{
		class LoadSink final : public RE::BSTEventSink<RE::TESObjectLoadedEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent& a_event,
				RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
			{
				if (a_event.loaded) {
					Names::GetSingleton().OnLoaded(a_event.formID);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Names::RegisterLoadSink()
	{
		static LoadSink sink;
		if (auto* source = RE::TESObjectLoadedEvent::GetEventSource()) {
			source->RegisterSink(&sink);
		} else {
			logger::error("names: no object-loaded event - a name the game does not keep comes back only on a load");
		}
	}

	void Names::Reapply()
	{
		std::vector<std::uint32_t> ids;
		{
			NamedLock lock{ _lock, "names" };
			if (!_enabled) {
				return;
			}
			ids.assign(_introduced.begin(), _introduced.end());
		}
		std::size_t inMemory = 0;
		std::size_t renamed = 0;
		for (const auto id : ids) {
			auto* actor = RE::TESForm::GetFormByID<RE::Actor>(id);
			if (!actor || !actor->GetNPC()) {
				continue;
			}
			++inMemory;
			if (Apply(actor, NameFor(id, actor->GetNPC()->GetSex() == RE::SEX::kFemale))) {
				++renamed;
			}
		}
		if (!ids.empty()) {
			// "kept" = the game held on to the custom name by itself.
			logger::info("names: {} introduced in this save, {} in memory: {} kept their name, {} named again",
				ids.size(), inMemory, inMemory - renamed, renamed);
		}
	}
}

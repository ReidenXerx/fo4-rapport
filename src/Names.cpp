#include "Names.h"

#include "McmSettings.h"
#include "Traits.h"

namespace RP
{
	namespace
	{
		// A name on this many NPC records, at least one of them not Unique, is a
		// label ("Drifter"), not somebody's name. Three let Preston Garvey through.
		constexpr std::uint32_t kLabelRecords = 5;
		// HasBeenCompanionFaction, Fallout4.esm.
		constexpr RE::TESFormID kHasBeenCompanionFaction = 0x000A1B85;

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

		bool first = false;
		{
			NamedLock lock{ _lock, "names" };
			_enabled = McmSettings::ReadBool(document, "enabled", true);
			_female = std::move(female);
			_male = std::move(male);
			_surnames = std::move(surnames);
			first = !_counted;
			logger::info("names: {} - {} female, {} male first names, {} surnames", _enabled ? "on" : "OFF",
				_female.size(), _male.size(), _surnames.size());
		}
		if (first) {
			CountLabels();
		}
	}

	void Names::CountLabels()
	{
		std::unordered_map<std::string, std::uint32_t> counts;
		std::unordered_map<std::string, std::uint32_t> uniques;
		std::size_t                                    records = 0;
		if (auto* data = RE::TESDataHandler::GetSingleton()) {
			for (auto* npc : data->GetFormArray<RE::TESNPC>()) {
				if (!npc) {
					continue;
				}
				++records;
				const auto name = RE::TESFullName::GetFullName(*npc);
				if (!name.empty()) {
					++counts[std::string{ name }];
					if (npc->IsUnique()) {
						++uniques[std::string{ name }];
					}
				}
			}
		}
		std::vector<std::pair<std::string, std::uint32_t>> labels;
		for (const auto& [name, n] : counts) {
			if (n >= kLabelRecords && uniques[name] < n) {
				labels.emplace_back(name, n);
			}
		}
		std::sort(labels.begin(), labels.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
		std::string sample;
		for (std::size_t i = 0; i < labels.size() && i < 16; ++i) {
			sample += std::format("{}{} ({})", i ? ", " : "", labels[i].first, labels[i].second);
		}
		{
			NamedLock lock{ _lock, "names" };
			_labels.clear();
			for (const auto& [name, n] : labels) {
				_labels.insert(name);
			}
			_counted = true;
		}
		logger::info("names: {} NPC records, {} names are labels (on {} or more records, not all Unique): {}", records,
			labels.size(), kLabelRecords, sample);

		// Every label, next to Rapport.log, so a real name that happens to sit on three
		// records can be found by reading one file: records, how many of them are
		// flagged Unique, the name.
		if (auto path = logger::log_directory()) {
			*path /= "Rapport-labels.txt";
			if (std::ofstream out{ *path, std::ios::trunc }; out) {
				out << "# records\tunique\tname - every NPC name Rapport treats as a label (Names.cpp)\n";
				for (const auto& [name, n] : labels) {
					out << n << '\t' << uniques[name] << '\t' << name << '\n';
				}
			}
		}
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

	std::string Names::WhyNotNameless(RE::Actor* a_actor) const
	{
		if (!a_actor || a_actor == RE::PlayerCharacter::GetSingleton()) {
			return "the player";
		}
		auto* npc = a_actor->GetNPC();
		if (!npc) {
			return "no base";
		}
		// Anyone who has ever been the player's companion is a person the player
		// knows by name, whatever their records say. Recruitment adds this faction
		// and nothing removes it (Overture's O-8 reads the same one).
		if (auto* faction = RE::TESForm::GetFormByID<RE::TESFaction>(kHasBeenCompanionFaction);
			faction && a_actor->IsInFaction(faction)) {
			return "has been a companion";
		}
		const char* shown = a_actor->GetDisplayFullName();
		if (!shown || !*shown) {
			return "no name to read";
		}
		{
			NamedLock lock{ _lock, "names" };
			if (!_labels.contains(shown)) {
				return std::format("\"{}\" is a name, not a label", shown);
			}
		}
		if (npc->IsUnique()) {
			// Unique AND a label: nameless only if the name is not their own but
			// their template's (TrainBar's patrons inherit "Drifter"). A unique
			// character who carries a common word as a name keeps it.
			const bool inherited = npc->baseTemplateForm &&
			                       npc->actorData.templateUseFlags.all(RE::ACTOR_BASE_DATA::TEMPLATE_USE_FLAG::kBaseData);
			if (!inherited) {
				return std::format("unique, and \"{}\" is their own", shown);
			}
		}
		return {};
	}

	std::string Names::Introduce(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return {};
		}
		if (const auto why = WhyNotNameless(a_actor); !why.empty()) {
			logger::info("names: {:08X} keeps their own name ({})", a_actor->GetFormID(), why);
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
		logger::info("names: {:08X} introduced as {} (was \"{}\", base {:08X}{})", formID, name,
			a_actor->GetDisplayFullName() ? a_actor->GetDisplayFullName() : "", a_actor->GetNPC()->GetFormID(),
			a_actor->GetNPC()->IsUnique() ? ", unique with an inherited name" : "");
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
				// Once, so the log proves the source below is the real one: a sink on
				// the wrong address would simply never hear anything.
				if (!_heard.exchange(true)) {
					logger::info("names: object-loaded events are arriving");
				}
				if (a_event.loaded) {
					Names::GetSingleton().OnLoaded(a_event.formID);
				}
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			std::atomic_bool _heard{ false };
		};

		// NOT RE::TESObjectLoadedEvent::GetEventSource(). That header function CALLS
		// relocation 416662, and on 1.10.163 the address there is the event source
		// ITSELF, not a getter: calling it executed data and took the game down at
		// data load. Crash log 2026-09-23 11:41:27: "Tried to execute memory at
		// 0x7FF67C328500" (Fallout4.exe+59D8500), one line after "names: on".
		//
		// F4MCP found the same source at the same address a different way. It calls
		// the header's getter under a guard, sees it fault, and scans the script
		// event holder by type name: "matrix: loaded attached (scan:
		// TESObjectLoadedEvent at 7FF67C328500)". Its sink there works. So the
		// relocation's address is taken AS the source. Rapport runs on 1.10.163
		// only (F4SEPlugin_Query refuses anything else), so the address is fixed.
		RE::BSTEventSource<RE::TESObjectLoadedEvent>* LoadedSource()
		{
			static REL::Relocation<std::uintptr_t> where{ REL::RelocationID(416662, 2201853) };
			return reinterpret_cast<RE::BSTEventSource<RE::TESObjectLoadedEvent>*>(where.address());
		}
	}

	void Names::RegisterLoadSink()
	{
		static LoadSink sink;
		if (auto* source = LoadedSource()) {
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

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

		void ReadSet(const nlohmann::json& a_doc, const char* a_key, std::unordered_set<std::string>& a_out)
		{
			const auto it = a_doc.find(a_key);
			if (it == a_doc.end() || !it->is_array()) {
				return;
			}
			for (const auto& entry : *it) {
				if (entry.is_string() && !entry.get<std::string>().empty()) {
					a_out.insert(entry.get<std::string>());
				}
			}
		}
	}

	Names& Names::GetSingleton() noexcept
	{
		static Names singleton;
		return singleton;
	}

	void Names::Load()
	{
		std::vector<std::string>        female = DefaultFemale();
		std::vector<std::string>        male = DefaultMale();
		std::vector<std::string>        surnames = DefaultSurnames();
		std::unordered_set<std::string> keep;
		std::unordered_set<std::string> forceLabels;
		nlohmann::json                  document = nlohmann::json::object();
		document["enabled"] = true;
		if (std::ifstream file{ OverridePath() }; file) {
			try {
				nlohmann::json read;
				file >> read;
				if (read.is_object()) {
					ReadList(read, "female", female);
					ReadList(read, "male", male);
					ReadList(read, "surnames", surnames);
					ReadSet(read, "keep", keep);
					ReadSet(read, "label", forceLabels);
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
			_keep = std::move(keep);
			_forceLabels = std::move(forceLabels);
			first = !_counted;
			logger::info("names: {} - {} female, {} male first names, {} surnames; {} kept, {} forced labels",
				_enabled ? "on" : "OFF", _female.size(), _male.size(), _surnames.size(), _keep.size(),
				_forceLabels.size());
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

		// Every label, next to Rapport.log, so a real name the count gets wrong can be
		// found by reading one file -- and put on names.json's "keep" list.
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
		const auto  stable = Traits::StableID(a_formID);
		const auto& given = first[Mix(stable, 0xA5A5F00Du) % first.size()];
		auto        index = Mix(stable, 0x1B873593u) % _surnames.size();
		// "Doyle" is a first name and a surname: never "Doyle Doyle".
		if (_surnames[index] == given && _surnames.size() > 1) {
			index = (index + 1) % _surnames.size();
		}
		return given + " " + _surnames[index];
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
			if (_keep.contains(shown)) {
				return std::format("\"{}\" is on names.json's keep list", shown);
			}
			if (_forceLabels.contains(shown)) {
				return {};
			}
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
		if (!a_actor || !a_actor->GetNPC()) {
			return {};
		}
		const auto formID = a_actor->GetFormID();
		const auto base = a_actor->GetNPC()->GetFormID();
		const bool female = a_actor->GetNPC()->GetSex() == RE::SEX::kFemale;

		bool known = false;
		{
			NamedLock lock{ _lock, "names" };
			if (!_enabled) {
				return {};
			}
			if (const auto it = _introduced.find(formID); it != _introduced.end()) {
				if (it->second == 0 || it->second == base) {
					known = true;
				} else {
					// The engine handed this id to somebody else (a spawned actor
					// cleaned up and replaced): a stranger, not the one introduced.
					logger::info("names: {:08X} is somebody new (base {:08X}, was {:08X}) - the old name is not theirs",
						formID, base, it->second);
					_introduced.erase(it);
				}
			}
		}
		if (known) {
			// Introduced before: no new name and no "her name is" line. If the game
			// lost the name (a cell that reset), give it back quietly -- it is still
			// the same person.
			if (!a_actor->extraList || !a_actor->extraList->HasType(RE::EXTRA_DATA_TYPE::kTextDisplayData)) {
				if (const auto name = NameFor(formID, female); !name.empty()) {
					ApplyOnMainThread(formID, name);
					logger::info("names: {:08X} had lost their name - given back as {}", formID, name);
				}
			}
			return {};
		}
		if (a_actor->IsDead(true)) {
			return {};
		}
		if (const auto why = WhyNotNameless(a_actor); !why.empty()) {
			logger::info("names: {:08X} keeps their own name ({})", formID, why);
			return {};
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
			_introduced[formID] = base;
		}
		logger::info("names: {:08X} introduced as {} (was \"{}\", base {:08X}{})", formID, name,
			a_actor->GetDisplayFullName() ? a_actor->GetDisplayFullName() : "", base,
			a_actor->GetNPC()->IsUnique() ? ", unique with an inherited name" : "");
		ApplyOnMainThread(formID, name);
		return name;
	}

	void Names::ApplyOnMainThread(std::uint32_t a_formID, std::string a_name)
	{
		// The rename writes the reference's extra data, which belongs to the main
		// thread. A native registered without tasklets is in practice run on the VM's
		// main-thread pass already; the task hop costs a frame and stays correct if
		// that is ever not so.
		if (auto* task = F4SE::GetTaskInterface()) {
			task->AddTask([a_formID, name = std::move(a_name)] {
				if (auto* actor = RE::TESForm::GetFormByID<RE::Actor>(a_formID)) {
					Apply(actor, name);
				}
			});
		} else if (auto* actor = RE::TESForm::GetFormByID<RE::Actor>(a_formID)) {
			Apply(actor, a_name);
		}
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

	std::vector<std::pair<std::uint32_t, std::uint32_t>> Names::Introduced() const
	{
		NamedLock lock{ _lock, "names" };
		return { _introduced.begin(), _introduced.end() };
	}

	void Names::Restore(std::vector<std::pair<std::uint32_t, std::uint32_t>> a_introduced)
	{
		NamedLock lock{ _lock, "names" };
		_introduced.clear();
		for (const auto& [id, base] : a_introduced) {
			_introduced[id] = base;
		}
	}

	void Names::Clear()
	{
		NamedLock lock{ _lock, "names" };
		_introduced.clear();
	}

	void Names::Forget(std::uint32_t a_formID)
	{
		NamedLock lock{ _lock, "names" };
		if (_introduced.erase(a_formID) > 0) {
			logger::info("names: {:08X} died - forgotten, so the id cannot name somebody new", a_formID);
		}
	}
}

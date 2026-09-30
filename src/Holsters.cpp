#include "Holsters.h"

namespace RP
{
	namespace
	{
		constexpr std::size_t kMaxFound = 64;
		constexpr int         kMaxDepth = 96;

		struct Found
		{
			RE::NiAVObject* nodes[kMaxFound];
			std::size_t     count;
		};

		void Walk(RE::NiAVObject* a_object, Found& a_found, int a_depth)
		{
			if (!a_object || a_depth > kMaxDepth || a_found.count == kMaxFound) {
				return;
			}
			if (a_object->GetName().starts_with("VisFav")) {
				// The display's root. What is under it is the weapon's own mesh.
				a_found.nodes[a_found.count++] = a_object;
				return;
			}
			if (auto* node = a_object->IsNode()) {
				for (auto& child : node->children) {
					Walk(child.get(), a_found, a_depth + 1);
				}
			}
		}

		// A skeleton read from a plugin is a pointer walk: a fault here must cost the walk,
		// never the game. No object with a destructor lives in this frame.
		bool GuardedWalk(RE::NiAVObject* a_root, Found* a_found) noexcept
		{
			__try {
				Walk(a_root, *a_found, 0);
				return true;
			} __except (1) {
				return false;
			}
		}

		RE::NiAVObject* RootOf(std::uint32_t a_formID)
		{
			auto* form = RE::TESForm::GetFormByID(a_formID);
			auto* actor = form ? form->As<RE::Actor>() : nullptr;
			if (!actor) {
				return nullptr;
			}
			// The player's THIRD-person body: an AAF scene is played in third person.
			if (actor == RE::PlayerCharacter::GetSingleton()) {
				return actor->Get3D(false);
			}
			return actor->Get3D();
		}

		bool Culled(const RE::NiAVObject* a_object) noexcept
		{
			return (a_object->flags.flags & 1) != 0;
		}

		void SetCulled(RE::NiAVObject* a_object, bool a_culled) noexcept
		{
			// The flag the engine's own GetAppCulled reads (NiAVObject::flags bit 0), written
			// directly: SetAppCulled is a virtual further down the table than IsNode.
			if (a_culled) {
				a_object->flags.flags |= 1;
			} else {
				a_object->flags.flags &= ~static_cast<std::uint64_t>(1);
			}
		}
	}

	Holsters& Holsters::GetSingleton() noexcept
	{
		static Holsters singleton;
		return singleton;
	}

	bool Holsters::Present()
	{
		if (_present < 0) {
			_present = F4SE::WinAPI::GetModuleHandle(L"VisibleFavorites.dll") ? 1 : 0;
			logger::info("holsters: Visible Favorites {}", _present ? "is loaded - its displays are hidden during AAF scenes"
																	: "is not loaded - nothing to hide");
		}
		return _present == 1;
	}

	bool Holsters::OnMainThread() const
	{
		const auto main = _mainThread.load();
		return main != std::thread::id{} && std::this_thread::get_id() == main;
	}

	void Holsters::Hide(Held& a_held)
	{
		auto* root = RootOf(a_held.actor);
		if (!root) {
			return;
		}
		Found found{};
		if (!GuardedWalk(root, &found)) {
			logger::warn("holsters: reading {:08X}'s skeleton faulted - left as it is", a_held.actor);
			return;
		}
		std::size_t hidden = 0;
		for (std::size_t i = 0; i < found.count; ++i) {
			auto* node = found.nodes[i];
			if (Culled(node)) {
				continue;   // hidden already: by us on an earlier poll, or by the mod itself
			}
			SetCulled(node, true);
			std::string name{ node->GetName() };
			if (std::ranges::find(a_held.culled, name) == a_held.culled.end()) {
				a_held.culled.push_back(std::move(name));
			}
			++hidden;
		}
		if (hidden > 0 && !a_held.logged) {
			a_held.logged = true;
			logger::info("holsters: hid {} Visible Favorites display(s) on {:08X} for the scene", hidden, a_held.actor);
		}
	}

	void Holsters::Show(Held& a_held)
	{
		if (a_held.culled.empty()) {
			return;
		}
		auto* root = RootOf(a_held.actor);
		if (!root) {
			return;   // unloaded: the displays are rebuilt with the body
		}
		Found found{};
		if (!GuardedWalk(root, &found)) {
			return;
		}
		std::size_t shown = 0;
		for (std::size_t i = 0; i < found.count; ++i) {
			auto* node = found.nodes[i];
			if (Culled(node) && std::ranges::find(a_held.culled, std::string{ node->GetName() }) != a_held.culled.end()) {
				SetCulled(node, false);
				++shown;
			}
		}
		logger::info("holsters: showed {} Visible Favorites display(s) on {:08X} again", shown, a_held.actor);
	}

	void Holsters::SceneStarted(const std::vector<std::uint32_t>& a_actors)
	{
		if (!Present()) {
			return;
		}
		std::lock_guard lock{ _lock };
		for (const auto actor : a_actors) {
			if (actor == 0) {
				continue;
			}
			auto it = std::ranges::find(_held, actor, &Held::actor);
			if (it == _held.end()) {
				_held.push_back(Held{ actor, {}, std::chrono::steady_clock::now() });
				it = std::prev(_held.end());
			}
			it->ending = false;
			if (OnMainThread()) {
				Hide(*it);
			}
		}
	}

	void Holsters::SceneEnded(const std::vector<std::uint32_t>& a_actors)
	{
		if (!Present()) {
			return;
		}
		std::lock_guard lock{ _lock };
		for (const auto actor : a_actors) {
			const auto it = std::ranges::find(_held, actor, &Held::actor);
			if (it == _held.end()) {
				continue;
			}
			if (OnMainThread()) {
				Show(*it);
				_held.erase(it);
			} else {
				it->ending = true;
			}
		}
	}

	void Holsters::Pump()
	{
		_mainThread.store(std::this_thread::get_id());
		if (!Present()) {
			return;
		}
		std::lock_guard lock{ _lock };
		const auto      now = std::chrono::steady_clock::now();
		for (auto it = _held.begin(); it != _held.end();) {
			if (it->ending || now - it->since > kLongest) {
				if (!it->ending) {
					logger::warn("holsters: {:08X}'s scene end never came in {} minutes - showing their displays",
						it->actor, kLongest.count());
				}
				Show(*it);
				it = _held.erase(it);
			} else {
				Hide(*it);
				++it;
			}
		}
	}

	void Holsters::Reset()
	{
		std::lock_guard lock{ _lock };
		_held.clear();
	}
}

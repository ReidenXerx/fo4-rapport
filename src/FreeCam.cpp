#include "FreeCam.h"

#include "Config.h"
#include "Orders.h"
#include "PapyrusLink.h"

namespace RP
{
	namespace
	{
		using States = RE::CameraStates;

		[[nodiscard]] bool PlayerIn(const std::vector<std::uint32_t>& a_actors)
		{
			return std::ranges::find(a_actors, 0x14u) != a_actors.end();
		}

		// -1 when there is no camera state to read.
		[[nodiscard]] int StateNow()
		{
			// By POINTER, not by the state's id field: on AE through the RD layout the id read 3
			// (free) on a first-person camera, and the free camera quit as "already free" without
			// a word (Fo4-mcp, 0.2.15). Which slot of cameraStates is current cannot be misread.
			const auto* camera = RE::PlayerCamera::GetSingleton();
			if (!camera || !camera->currentState) {
				return -1;
			}
			const auto* current = camera->currentState.get();
			for (std::uint32_t i = 0; i < RE::CameraStates::kTotal; ++i) {
				if (camera->cameraStates[i].get() == current) {
					return static_cast<int>(i);
				}
			}
			return -2;   // a state that is none of the player camera's own
		}

		[[nodiscard]] bool Settled(int a_id)
		{
			return a_id == static_cast<int>(States::kFirstPerson) || a_id == static_cast<int>(States::kIronSights) ||
			       a_id == static_cast<int>(States::k3rdPerson);
		}

		[[nodiscard]] bool First(int a_id)
		{
			return a_id == static_cast<int>(States::kFirstPerson) || a_id == static_cast<int>(States::kIronSights);
		}

		// PlayerCamera::ToggleFreeCameraMode(bool freezeTime) -- the console's tfc. OG 224913 /
		// AE 2248368 (alandtse CommonLibF4; the AE id is VATS Bullets', in use there).
		void ToggleFree(RE::PlayerCamera* a_camera)
		{
			using func_t = void(RE::PlayerCamera*, bool);
			static REL::Relocation<func_t> func{ REL::ID(224913, 2248368) };
			func(a_camera, false);   // false: the world, and the scene, keep running
		}

		// First or third person through the bridge: Game.ForceFirstPerson / ForceThirdPerson.
		// A raw PlayerCamera::SetState did NOT take on a player held by AAF (Fo4-mcp, 0.2.14,
		// two runs: the camera stayed first person); the Papyrus call did, mid-scene.
		void SetView(bool a_first)
		{
			PapyrusLink::GetSingleton().QueueOrders(
				{ Order{ Order::Kind::kCameraView, 0x14, a_first ? "first" : "third" } });
		}

		// A job for the game's thread, run a_tries times every 100 ms until it answers true.
		// Tasks stall during a loading screen, so every job re-reads the camera when it runs.
		void Retry(std::function<bool()> a_job, int a_tries, std::function<void()> a_gaveUp = {})
		{
			std::thread([job = std::move(a_job), a_tries, gaveUp = std::move(a_gaveUp)] {
				auto done = std::make_shared<std::atomic_bool>(false);
				for (int i = 0; i < a_tries && !done->load(); ++i) {
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
					if (const auto* tasks = F4SE::GetTaskInterface()) {
						tasks->AddTask([job, done] {
							if (!done->load() && job()) {
								done->store(true);
							}
						});
					}
				}
				if (gaveUp) {
					std::this_thread::sleep_for(std::chrono::milliseconds(300));   // the last job's turn
					if (!done->load()) {
						gaveUp();
					}
				}
			}).detach();
		}
	}

	FreeCam& FreeCam::GetSingleton() noexcept
	{
		static FreeCam singleton;
		return singleton;
	}

	namespace
	{
		[[nodiscard]] std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
		}
	}

	void FreeCam::Walking(const std::vector<std::uint32_t>& a_actors)
	{
		if (!PlayerIn(a_actors) || _inScene) {
			return;
		}
		if (const auto id = StateNow(); Settled(id) && !_held) {
			_lastFirst = First(id);
		}
		_held = true;
		_heldAtMs = NowMs();
	}

	void FreeCam::Animating(const std::vector<std::uint32_t>& a_actors)
	{
		if (!Config::GetSingleton().freeCamera || !PlayerIn(a_actors) || _inScene.exchange(true)) {
			return;   // off, not the player's scene, or already following it
		}
		const auto generation = ++_generation;
		_misses = 0;
		logger::info("free camera: the player's scene is animating - the free camera goes on (view before it: {} person)",
			_lastFirst ? "first" : "third");
		// Up to 15 s: AAF's own camera settles, and a first-person view is asked into third
		// person ONCE through the bridge (a poll away). A body seen from first person is
		// culled and not animated, so the free camera is entered from third person.
		auto asked = std::make_shared<bool>(false);
		auto tookOver = std::make_shared<bool>(false);
		Retry([this, generation, asked, tookOver] {
			if (generation != _generation || !_inScene) {
				logger::info("free camera: the scene ended before the camera came free");
				return true;
			}
			const auto id = StateNow();
			if (id == static_cast<int>(States::kFree)) {
				// Free at the first animation and not by us: AAF's own scene fly-cam, which flies
				// but does not turn with the mouse (owner, live, 2026-10-07). Taken over ONCE:
				// out of it, and ours goes on from the third-person view it lands in.
				if (!*tookOver) {
					*tookOver = true;
					if (auto* camera = RE::PlayerCamera::GetSingleton()) {
						ToggleFree(camera);
					}
					logger::info("free camera: AAF's own fly-cam is up - taking it over (now state {})", StateNow());
					return false;
				}
				logger::info("free camera: free again (state {}) after the takeover - AAF holds it, left alone", id);
				return true;
			}
			if (First(id)) {
				if (!*asked) {
					*asked = true;
					_changed = true;
					SetView(false);
					logger::info("free camera: first person - asking the bridge for third person first");
				}
				return false;   // enter on a later try, from the third-person state
			}
			if (id != static_cast<int>(States::k3rdPerson)) {
				return false;   // a transition, a tween, a dialogue camera: wait for it to settle
			}
			if (auto* camera = RE::PlayerCamera::GetSingleton()) {
				ToggleFree(camera);
				_entered = StateNow() == static_cast<int>(States::kFree);
				logger::info("free camera: {}", _entered ? "on" : "the toggle did not take - left alone");
				if (_entered) {
					// The mouse: if AAF disabled looking, the bridge says so and asks a layer of
					// its own to enable it (released at the end).
					PapyrusLink::GetSingleton().QueueOrders({ Order{ Order::Kind::kCameraView, 0x14, "probe" } });
				}
			}
			return true;
		}, 150, [this, generation] {
			if (generation == _generation && _inScene && !_entered) {
				logger::info("free camera: gave up - the camera was in state {} after 15 s, never third person "
				             "(RE::CameraStates order)", StateNow());
			}
		});
	}

	void FreeCam::SceneEnded(const std::vector<std::uint32_t>& a_actors)
	{
		if (!PlayerIn(a_actors)) {
			return;
		}
		_held = false;
		if (!_inScene.exchange(false)) {
			return;
		}
		const auto generation = ++_generation;
		const bool entered = _entered.exchange(false);
		const bool changed = _changed.exchange(false);
		const bool ours = entered || changed;
		if (entered) {
			PapyrusLink::GetSingleton().QueueOrders({ Order{ Order::Kind::kCameraView, 0x14, "release" } });
		}
		const bool first = _lastFirst;
		if (!ours) {
			return;   // never on, or the player left it: their camera, untouched
		}
		logger::info("free camera: the scene ended - off, then back to {} person", first ? "first" : "third");
		auto off = std::make_shared<bool>(false);
		// Off first, then the first SETTLED landing put right -- that one only (General-mods'
		// VATS Bullets scar: correcting a later change overrode the player's own switch).
		Retry([this, generation, first, off, entered] {
			if (generation != _generation) {
				return true;   // a new scene took over
			}
			const auto id = StateNow();
			if (!*off) {
				*off = true;
				if (entered && id == static_cast<int>(States::kFree)) {   // ours to take off, nobody else's
					if (auto* camera = RE::PlayerCamera::GetSingleton()) {
						ToggleFree(camera);
					}
				}
				return false;
			}
			if (!Settled(id)) {
				return false;
			}
			if (First(id) != first) {
				SetView(first);   // once: this is the first landing, and the job ends here
				logger::info("free camera: landed in {} person - put back in {}", First(id) ? "first" : "third",
					first ? "first" : "third");
			}
			return true;
		}, 30);
	}

	void FreeCam::Pump()
	{
		const auto id = StateNow();
		if (!_inScene) {
			// A walk that never became a scene lets go after a minute.
			if (_held && NowMs() - _heldAtMs > 60000) {
				_held = false;
			}
			if (!_held && Settled(id)) {
				_lastFirst = First(id);
			}
			return;
		}
		if (_entered && id != static_cast<int>(States::kFree)) {
			// Two polls (6 s by default) out of it: taken back -- a menu, VATS, a death, or
			// the player's own tfc. Never re-entered, never toggled at the end.
			if (++_misses >= 2) {
				_entered = false;
				logger::info("free camera: no longer up during the scene - left to the player");
			}
		} else {
			_misses = 0;
		}
	}

	void FreeCam::Reset()
	{
		++_generation;
		_inScene = false;
		_entered = false;
		_changed = false;
		_held = false;
		_misses = 0;
	}
}

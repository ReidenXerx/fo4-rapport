#include "FreeCam.h"

#include "Config.h"

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
			const auto* camera = RE::PlayerCamera::GetSingleton();
			return camera && camera->currentState ? static_cast<int>(camera->currentState->id.get()) : -1;
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

		void SetView(bool a_first)
		{
			if (auto* camera = RE::PlayerCamera::GetSingleton()) {
				if (auto* state = camera->cameraStates[a_first ? States::kFirstPerson : States::k3rdPerson].get()) {
					camera->SetState(state);
				}
			}
		}

		// A job for the game's thread, run a_tries times every 100 ms until it answers true.
		// Tasks stall during a loading screen, so every job re-reads the camera when it runs.
		void Retry(std::function<bool()> a_job, int a_tries)
		{
			std::thread([job = std::move(a_job), a_tries] {
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
		// Up to 5 s for AAF's own camera to settle. A body seen from first person is
		// culled and not animated, so the free camera is entered from third person.
		Retry([this, generation] {
			if (generation != _generation || !_inScene) {
				return true;   // that scene is over
			}
			const auto id = StateNow();
			if (id == static_cast<int>(States::kFree)) {
				return true;   // already free: the player's own tfc, not ours to take off
			}
			if (!Settled(id)) {
				return false;
			}
			if (First(id)) {
				SetView(false);
				return false;   // enter on a later try, from the third-person state
			}
			if (auto* camera = RE::PlayerCamera::GetSingleton()) {
				ToggleFree(camera);
				_entered = StateNow() == static_cast<int>(States::kFree);
				logger::info("free camera: {}", _entered ? "on" : "the toggle did not take - left alone");
			}
			return true;
		}, 50);
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
		const bool ours = _entered.exchange(false);
		const bool first = _lastFirst;
		if (!ours) {
			return;   // never on, or the player left it: their camera, untouched
		}
		logger::info("free camera: the scene ended - off, then back to {} person", first ? "first" : "third");
		auto off = std::make_shared<bool>(false);
		// Off first, then the first SETTLED landing put right -- that one only (General-mods'
		// VATS Bullets scar: correcting a later change overrode the player's own switch).
		Retry([this, generation, first, off] {
			if (generation != _generation) {
				return true;   // a new scene took over
			}
			const auto id = StateNow();
			if (!*off) {
				*off = true;
				if (id == static_cast<int>(States::kFree)) {
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
				SetView(first);
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
		_held = false;
		_misses = 0;
	}
}

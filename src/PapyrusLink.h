#pragma once

namespace RP
{
	// The join between the native scheduler and the one Papyrus script.
	//
	// Papyrus calls in; the plugin never calls out. Dispatching into the VM from
	// here crashed the game twice inside DispatchMethodCallImpl: F4SE tasks run on
	// a BSJobs job thread, and the VM packs call arguments through the per-thread
	// scrap heap. The scheduler therefore leaves a request on the counter and the
	// bridge collects it, which keeps every VM operation on the VM's own thread.
	//
	// The cost is one native call every PollSeconds that almost always returns 0.
	// That is not the polling the design rules out: no actor is touched, nothing
	// is enumerated, and the scheduler still does all the work natively.
	class PapyrusLink
	{
	public:
		[[nodiscard]] static PapyrusLink& GetSingleton() noexcept;

		static bool RegisterNatives(RE::BSScript::IVirtualMachine* a_vm);

		void OnDataReady();

		[[nodiscard]] bool Ready() const noexcept { return _bridgeReady.load(); }
		[[nodiscard]] bool Busy() const noexcept { return _sceneInFlight.load(); }

		// Leaves a request for the bridge to collect. False when one is already
		// outstanding or the bridge is not listening.
		bool RequestScene(RE::Actor* a_first, RE::Actor* a_second, float a_duration);

		// ---- called from Papyrus ----
		std::int32_t TakeRequest();

		// The plugin is the only part of this mod that reliably starts fresh every
		// session, so it owns the answer to "have we introduced ourselves yet?".
		// The script cannot answer it: its variables persist in the save.
		[[nodiscard]] bool NeedsHandshake() const noexcept { return !_bridgeReady.load(); }
		[[nodiscard]] std::int32_t TakenFirstID() const noexcept { return _takenFirst; }
		[[nodiscard]] std::int32_t TakenSecondID() const noexcept { return _takenSecond; }
		[[nodiscard]] float        TakenDuration() const noexcept { return _takenDuration; }

		// Belt and braces: Papyrus has its own timer, but if the bridge itself stops
		// answering, nothing else would ever clear the in-flight flag.
		void CheckWatchdog(float a_sceneSeconds);

		void OnBridgeReady(bool a_aafPresent);
		void OnSceneStarted(std::int32_t a_request);
		void OnSceneEnded(std::int32_t a_request);
		void OnRequestFailed(std::int32_t a_request, std::string_view a_why);

	private:
		struct Pending
		{
			std::int32_t request{ 0 };
			std::int32_t first{ 0 };
			std::int32_t second{ 0 };
			float        duration{ 0.0f };
		};

		mutable std::mutex _counter;
		Pending            _pending{};

		// Latched by TakeRequest so the three follow-up calls cannot see a torn
		// or replaced request.
		std::int32_t _takenFirst{ 0 };
		std::int32_t _takenSecond{ 0 };
		float        _takenDuration{ 0.0f };

		std::atomic_bool          _bridgeReady{ false };
		std::atomic_bool          _sceneInFlight{ false };
		std::atomic<std::int32_t> _nextRequest{ 1 };
		std::chrono::steady_clock::time_point _requestedAt{};
	};
}

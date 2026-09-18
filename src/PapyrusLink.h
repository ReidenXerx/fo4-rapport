#pragma once

namespace RP
{
	// The join between the native scheduler and the one Papyrus script.
	//
	// Native code decides; Papyrus acts, because AAF's API is Papyrus-only. The
	// traffic is deliberately thin: one call out per scene, four calls back.
	class PapyrusLink
	{
	public:
		[[nodiscard]] static PapyrusLink& GetSingleton() noexcept;

		// Registered with F4SE's Papyrus interface at load.
		static bool RegisterNatives(RE::BSScript::IVirtualMachine* a_vm);

		// Resolve the bridge quest. Needs the data handler, so: game data ready.
		void OnDataReady();

		[[nodiscard]] bool Ready() const noexcept { return _bridgeReady.load() && _quest != nullptr; }
		[[nodiscard]] bool Busy() const noexcept { return _sceneInFlight.load(); }

		// Asks the bridge to start a scene. Returns false if the call could not be
		// made at all; a scene that AAF then refuses comes back through RequestFailed.
		bool RequestScene(RE::Actor* a_first, RE::Actor* a_second, float a_duration);

		// Called from Papyrus.
		void OnBridgeReady(bool a_aafPresent);
		void OnSceneStarted(std::int32_t a_request);
		void OnSceneEnded(std::int32_t a_request);
		void OnRequestFailed(std::int32_t a_request, std::string_view a_why);

	private:
		RE::TESQuest*             _quest{ nullptr };
		std::atomic_bool          _bridgeReady{ false };
		std::atomic_bool          _sceneInFlight{ false };
		std::atomic<std::int32_t> _nextRequest{ 1 };
	};
}

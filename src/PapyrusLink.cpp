#include "PapyrusLink.h"

namespace
{
	constexpr auto kPluginName = "Rapport.esp"sv;
	constexpr std::uint32_t kBridgeQuestID = 0x00000800;

	constexpr auto kCoreScript = "Rapport:Core"sv;
	constexpr auto kBridgeScript = "Rapport:Bridge"sv;

	// ---- called from Papyrus -------------------------------------------------

	void Trace(std::monostate, RE::BSFixedString a_text)
	{
		logger::info("{}", a_text.c_str());
	}

	void BridgeReady(std::monostate, bool a_aafPresent)
	{
		RP::PapyrusLink::GetSingleton().OnBridgeReady(a_aafPresent);
	}

	void SceneStarted(std::monostate, std::int32_t a_request)
	{
		RP::PapyrusLink::GetSingleton().OnSceneStarted(a_request);
	}

	void SceneEnded(std::monostate, std::int32_t a_request)
	{
		RP::PapyrusLink::GetSingleton().OnSceneEnded(a_request);
	}

	void RequestFailed(std::monostate, std::int32_t a_request, RE::BSFixedString a_why)
	{
		RP::PapyrusLink::GetSingleton().OnRequestFailed(a_request, a_why.c_str());
	}
}

namespace RP
{
	PapyrusLink& PapyrusLink::GetSingleton() noexcept
	{
		static PapyrusLink singleton;
		return singleton;
	}

	bool PapyrusLink::RegisterNatives(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm) {
			return false;
		}

		a_vm->BindNativeMethod(kCoreScript, "Trace"sv, Trace, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "BridgeReady"sv, BridgeReady, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SceneStarted"sv, SceneStarted, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SceneEnded"sv, SceneEnded, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "RequestFailed"sv, RequestFailed, std::nullopt, false);

		logger::info("papyrus: bound 5 native functions on {}", kCoreScript);
		return true;
	}

	void PapyrusLink::OnDataReady()
	{
		const auto handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			logger::error("papyrus: no data handler");
			return;
		}

		_quest = handler->LookupForm<RE::TESQuest>(kBridgeQuestID, kPluginName);
		if (!_quest) {
			logger::error(
				"papyrus: {} is not loaded, so no scene can ever start. Enable it in the load order.",
				kPluginName);
			return;
		}

		logger::info("papyrus: bridge quest {:08X} found in {}", _quest->GetFormID(), kPluginName);
	}

	bool PapyrusLink::RequestScene(RE::Actor* a_first, RE::Actor* a_second, float a_duration)
	{
		if (!a_first || !a_second || !_quest) {
			return false;
		}
		if (!_bridgeReady.load()) {
			logger::warn("scene requested before the bridge reported ready");
			return false;
		}
		// One scene at a time is the configured maximum; the flag is cleared when
		// Papyrus reports the scene ended or the request failed, never on a timer.
		if (_sceneInFlight.exchange(true)) {
			return false;
		}

		const auto game = RE::GameVM::GetSingleton();
		const auto vm = game ? game->GetVM() : nullptr;
		if (!vm) {
			_sceneInFlight.store(false);
			return false;
		}

		const auto& handles = vm->GetObjectHandlePolicy();
		const auto  handle = handles.GetHandleForObject(
            RE::BSScript::GetVMTypeID<RE::TESQuest>(),
            const_cast<const void*>(static_cast<const volatile void*>(_quest)));
		if (handle == handles.EmptyHandle()) {
			logger::error("papyrus: no handle for the bridge quest");
			_sceneInFlight.store(false);
			return false;
		}

		const auto request = _nextRequest.fetch_add(1);

		logger::info(
			"request {}: asking the bridge to pair {} and {} for {:.0f}s",
			request, a_first->GetDisplayFullName(), a_second->GetDisplayFullName(), a_duration);

		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{};
		const bool dispatched = vm->DispatchMethodCall(
			handle, kBridgeScript, "BeginRequest"sv, callback,
			request, a_first, a_second, a_duration);

		if (!dispatched) {
			logger::error("papyrus: BeginRequest could not be dispatched");
			_sceneInFlight.store(false);
		}
		return dispatched;
	}

	void PapyrusLink::OnBridgeReady(bool a_aafPresent)
	{
		_bridgeReady.store(a_aafPresent);
		if (a_aafPresent) {
			logger::info("bridge ready");
		} else {
			logger::error("bridge reported AAF missing - nothing will be started");
		}
	}

	void PapyrusLink::OnSceneStarted(std::int32_t a_request)
	{
		logger::info("request {}: scene started", a_request);
	}

	void PapyrusLink::OnSceneEnded(std::int32_t a_request)
	{
		logger::info("request {}: scene ended", a_request);
		_sceneInFlight.store(false);
	}

	void PapyrusLink::OnRequestFailed(std::int32_t a_request, std::string_view a_why)
	{
		logger::warn("request {}: {}", a_request, a_why);
		_sceneInFlight.store(false);
	}
}

#include "PapyrusLink.h"

#include "Config.h"

namespace
{
	constexpr auto kCoreScript = "Rapport:Core"sv;

	// ---- called from Papyrus -------------------------------------------------

	void Papyrus_Trace(std::monostate, RE::BSFixedString a_text)
	{
		logger::info("{}", a_text.c_str());
	}

	std::int32_t Papyrus_TakeRequest(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().TakeRequest();
	}

	std::int32_t Papyrus_TakenFirstID(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().TakenFirstID();
	}

	std::int32_t Papyrus_TakenSecondID(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().TakenSecondID();
	}

	float Papyrus_TakenDuration(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().TakenDuration();
	}

	bool Papyrus_NeedsHandshake(std::monostate)
	{
		return RP::PapyrusLink::GetSingleton().NeedsHandshake();
	}

	float Papyrus_PollSeconds(std::monostate)
	{
		return RP::Config::GetSingleton().pollSeconds;
	}

	void Papyrus_BridgeReady(std::monostate, bool a_aafPresent)
	{
		RP::PapyrusLink::GetSingleton().OnBridgeReady(a_aafPresent);
	}

	void Papyrus_SceneStarted(std::monostate, std::int32_t a_request)
	{
		RP::PapyrusLink::GetSingleton().OnSceneStarted(a_request);
	}

	void Papyrus_SceneEnded(std::monostate, std::int32_t a_request)
	{
		RP::PapyrusLink::GetSingleton().OnSceneEnded(a_request);
	}

	void Papyrus_RequestFailed(std::monostate, std::int32_t a_request, RE::BSFixedString a_why)
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

		a_vm->BindNativeMethod(kCoreScript, "Trace"sv, Papyrus_Trace, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakeRequest"sv, Papyrus_TakeRequest, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakenFirstID"sv, Papyrus_TakenFirstID, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakenSecondID"sv, Papyrus_TakenSecondID, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "TakenDuration"sv, Papyrus_TakenDuration, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "PollSeconds"sv, Papyrus_PollSeconds, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "NeedsHandshake"sv, Papyrus_NeedsHandshake, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "BridgeReady"sv, Papyrus_BridgeReady, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SceneStarted"sv, Papyrus_SceneStarted, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "SceneEnded"sv, Papyrus_SceneEnded, std::nullopt, false);
		a_vm->BindNativeMethod(kCoreScript, "RequestFailed"sv, Papyrus_RequestFailed, std::nullopt, false);

		logger::info("papyrus: bound 11 native functions on {}", kCoreScript);
		return true;
	}

	void PapyrusLink::OnDataReady()
	{
		// Nothing to resolve any more: the bridge finds us, not the other way
		// round. Kept so the log still says whether the plugin is present.
		const auto handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			return;
		}
		if (const auto quest = handler->LookupForm<RE::TESQuest>(0x00000800, "Rapport.esp"sv)) {
			logger::info("papyrus: bridge quest {:08X} is loaded", quest->GetFormID());
		} else {
			logger::error("papyrus: Rapport.esp is not loaded - enable it in the load order");
		}
	}

	bool PapyrusLink::RequestScene(RE::Actor* a_first, RE::Actor* a_second, float a_duration)
	{
		if (!a_first || !a_second) {
			return false;
		}
		if (!_bridgeReady.load()) {
			logger::warn("scene wanted before the bridge reported ready");
			return false;
		}
		if (_sceneInFlight.exchange(true)) {
			return false;
		}

		const auto request = _nextRequest.fetch_add(1);
		{
			std::scoped_lock lock{ _counter };
			_pending = Pending{
				request,
				static_cast<std::int32_t>(a_first->GetFormID()),
				static_cast<std::int32_t>(a_second->GetFormID()),
				a_duration
			};
		}

		_requestedAt = std::chrono::steady_clock::now();

		logger::info(
			"request {}: queued {} ({:08X}) and {} ({:08X}) for {:.0f}s",
			request, a_first->GetDisplayFullName(), a_first->GetFormID(),
			a_second->GetDisplayFullName(), a_second->GetFormID(), a_duration);
		return true;
	}

	std::int32_t PapyrusLink::TakeRequest()
	{
		std::scoped_lock lock{ _counter };
		if (_pending.request == 0) {
			return 0;
		}

		_takenFirst = _pending.first;
		_takenSecond = _pending.second;
		_takenDuration = _pending.duration;

		const auto request = _pending.request;
		_pending = Pending{};
		return request;
	}

	void PapyrusLink::CheckWatchdog(float a_sceneSeconds)
	{
		if (!_sceneInFlight.load()) {
			return;
		}

		// Generous: the scene's own length, the bridge's own timeout, and room for
		// AAF to walk two people across a market before anyone calls it stuck.
		const auto limit = std::chrono::seconds{ static_cast<std::int64_t>(a_sceneSeconds) + 180 };
		if (std::chrono::steady_clock::now() - _requestedAt < limit) {
			return;
		}

		logger::error(
			"watchdog: nothing has been heard about the running scene for {}s - releasing",
			limit.count());
		{
			std::scoped_lock lock{ _counter };
			_pending = Pending{};
		}
		_sceneInFlight.store(false);
	}

	void PapyrusLink::OnBridgeReady(bool a_aafPresent)
	{
		_bridgeReady.store(a_aafPresent);
		if (a_aafPresent) {
			logger::info("bridge ready and listening every {:.1f}s", Config::GetSingleton().pollSeconds);
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

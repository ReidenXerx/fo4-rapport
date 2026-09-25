#include "DialogueVoice.h"

#include "Voices.h"

namespace RP::DialogueVoice
{
	namespace
	{
		// 1.10.163, measured 2026-09-25 (capstone over Fallout4.exe): the only call to the voice
		// path builder, and the builder itself. Rapport refuses every other runtime at load.
		constexpr std::uintptr_t kCallSite = 0xCA1CCB;
		constexpr std::uintptr_t kBuilder = 0x6135D0;
		constexpr std::size_t    kPathSize = 0x104;   // the caller's buffer (memset 0x104 before the call)
		constexpr std::string_view kPrefix = "Data\\Sound\\Voice\\";

		// (response, path out, voice type, the response's topic data, the INFO) -> built.
		using Build_t = bool(void*, char*, RE::BGSVoiceType*, void*, RE::TESTopicInfo*);
		REL::Relocation<Build_t> g_build;

		std::mutex                      g_saidLock;
		std::unordered_set<std::string> g_said;   // one log line per voice type AND line

		// Per line, not per voice type: the first run logged the Mayor's wordless "..." greeting and
		// then nothing, so whether his next line borrowed could not be read (2026-09-25).
		void SayOnce(const RE::BGSVoiceType* a_voice, std::string_view a_path, std::string_view a_what)
		{
			const auto id = a_voice ? a_voice->GetFormID() : 0u;
			const auto file = a_path.substr(a_path.find_last_of('\\') + 1);
			{
				std::scoped_lock lock{ g_saidLock };
				if (g_said.size() > 4096 || !g_said.insert(std::format("{:08X}{}", id, file)).second) {
					return;
				}
			}
			logger::info("dialogue voice: {:08X} ({}) line {} - {}", id, a_voice ? a_voice->GetFormEditorID() : "?", file,
				a_what);
		}

		// The plugin folder of a path the builder made, or "" if it is not that shape.
		[[nodiscard]] std::string_view PluginOf(std::string_view a_path)
		{
			if (a_path.size() <= kPrefix.size() || _strnicmp(a_path.data(), kPrefix.data(), kPrefix.size()) != 0) {
				return {};
			}
			const auto rest = a_path.substr(kPrefix.size());
			const auto end = rest.find('\\');
			return end == std::string_view::npos ? std::string_view{} : rest.substr(0, end);
		}

		// The engine names the .wav and loads the .fuz or .xwm beside it.
		[[nodiscard]] bool HasAudio(std::string_view a_path)
		{
			std::error_code ec;
			std::filesystem::path path{ a_path };
			for (const auto* ext : { ".fuz", ".xwm", ".wav" }) {
				path.replace_extension(ext);
				if (std::filesystem::exists(path, ec)) {
					return true;
				}
			}
			return false;
		}

		bool Thunk(void* a_response, char* a_path, RE::BGSVoiceType* a_voice, void* a_topic, RE::TESTopicInfo* a_info)
		{
			const bool built = g_build(a_response, a_path, a_voice, a_topic, a_info);
			if (!built || !a_path || !*a_path || !a_voice) {
				return built;
			}
			try {
				const std::string_view path{ a_path };
				auto&                  voices = Voices::GetSingleton();
				if (!voices.IsDialoguePlugin(PluginOf(path)) || HasAudio(path)) {
					return built;
				}
				const auto as = voices.DialogueBorrow(a_voice->GetFormID());
				auto*      borrowed = as ? RE::TESForm::GetFormByID<RE::BGSVoiceType>(as) : nullptr;
				if (!borrowed) {
					SayOnce(a_voice, path, "no file of its own and nothing to borrow - subtitle only");
					return built;
				}
				char alt[kPathSize]{};
				if (!g_build(a_response, alt, borrowed, a_topic, a_info) || !HasAudio(alt)) {
					SayOnce(a_voice, path, std::format("borrows {} but that has no file here either - subtitle only",
											   borrowed->GetFormEditorID()));
					return built;
				}
				SayOnce(a_voice, path, std::format("speaks it as {}", borrowed->GetFormEditorID()));
				strcpy_s(a_path, kPathSize, alt);
			} catch (const std::exception& e) {
				logger::error("dialogue voice: {} - the engine's own path kept", e.what());
			}
			return built;
		}
	}

	void Install()
	{
		const REL::Relocation<std::uintptr_t> site{ REL::Offset(kCallSite) };
		const REL::Relocation<std::uintptr_t> builder{ REL::Offset(kBuilder) };
		const auto*                           bytes = reinterpret_cast<const std::uint8_t*>(site.address());
		std::int32_t                          rel = 0;
		std::memcpy(&rel, bytes + 1, sizeof(rel));
		if (bytes[0] != 0xE8 || site.address() + 5 + rel != builder.address()) {
			logger::warn("dialogue voice: the call at +{:X} is not the voice path builder this build knows - "
						 "not hooked, so unrendered voices stay subtitle-only",
				kCallSite);
			return;
		}
		auto& trampoline = F4SE::GetTrampoline();
		g_build = trampoline.write_call<5>(site.address(), Thunk);
		logger::info("dialogue voice: hooked the voice path builder - dialogue plugins' lines borrow a voice "
					 "when the speaker's own has no file");
	}
}

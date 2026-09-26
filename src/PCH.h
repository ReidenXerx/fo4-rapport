#pragma once

#include "RE/Fallout.h"
#include "F4SE/F4SE.h"

#include <spdlog/sinks/basic_file_sink.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <cstdint>
#include <filesystem>
#include <random>
#include <format>
#include <fstream>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define DLLEXPORT __declspec(dllexport)

namespace logger = F4SE::log;
using namespace std::literals;

namespace RP
{
	// A path as UTF-8 text, for a log line. path::string() converts through the ANSI
	// code page and THROWS on a character it cannot hold -- a Cyrillic, Polish or
	// Chinese Windows user name in the Documents path (found by the Silhouette session,
	// 2026-09-24: Вадим, Łukasz and 中文 all throw on code page 1252). Open files with
	// the path itself, which stays wide; print them with this.
	[[nodiscard]] inline std::string PathText(const std::filesystem::path& a_path)
	{
		const auto utf8 = a_path.u8string();
		return { utf8.begin(), utf8.end() };
	}
}

#include "Compat.h"

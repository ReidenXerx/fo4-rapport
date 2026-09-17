#pragma once

#include "RE/Fallout.h"
#include "F4SE/F4SE.h"

#include <spdlog/sinks/basic_file_sink.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#define DLLEXPORT __declspec(dllexport)

namespace logger = F4SE::log;
using namespace std::literals;

#pragma once

// Precompiled header for Framework
// Includes stable STL and platform headers to avoid redundant parsing.

// C++ Standard Library (most frequently used across 61 TUs)
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <functional>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cassert>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <array>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>

// Engine stable headers (rarely change)
#include <Config.h>
#include <utils/xMaths.h>
#include <utils/xDefs.h>

// Windows platform headers (guarded)
#ifdef OS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

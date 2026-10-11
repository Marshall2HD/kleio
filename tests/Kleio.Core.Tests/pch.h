// Stand-in for src/Kleio/pch.h when compiling the Core sources into the test runner: the same
// Win32 + standard library headers, but no WinRT / Windows App SDK. This directory comes first in
// AdditionalIncludeDirectories so that `#include "pch.h"` in src/Kleio/Core/*.cpp resolves here.
#pragma once

#include <windows.h>
#include <winsqlite/winsqlite3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <regex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

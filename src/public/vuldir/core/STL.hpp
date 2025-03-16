#pragma once

#include "vuldir/core/Definitions.hpp"
#include "vuldir/core/Types.hpp"

#include <algorithm>
#include <atomic>
#include <bitset>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

#ifdef VD_STL
  #include "vuldir/core/Array.hpp"
  #include "vuldir/core/String.hpp"
  #include "vuldir/core/Vector.hpp"
#else
  #include <array>
  #include <span>
  #include <string>
  #include <vector>
#endif

namespace vd {

namespace fs = std::filesystem;

template<typename T>
using Ptr = T*;

template<typename T>
using UPtr = std::unique_ptr<T>;

template<typename T>
using SPtr = std::shared_ptr<T>;

template<typename T>
using WPtr = std::weak_ptr<T>;

template<typename K, typename V>
using Map = std::unordered_map<K, V>;

template<typename K, typename V>
using OMap = std::map<K, V>;

template<typename T>
using Deq = std::deque<T>;

template<typename T>
using Opt = std::optional<T>;

template<typename... T>
using Var = std::variant<T...>;

#ifndef VD_STL

using Str   = std::string;
using WStr  = std::wstring;
using Strv  = std::string_view;
using WStrv = std::wstring_view;

template<typename T>
using Arr = std::vector<T>;
template<typename T, u64 S>
using SArr = std::array<T, S>;
template<typename T, u64 S>
using FArr = std::vector<T>;

template<typename T>
using Span = std::span<T>;

template<typename T>
using Init = std::initializer_list<T>;

#endif

} // namespace vd

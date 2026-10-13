#pragma once

#include "threading/CanMessage.h"

#include <cstdint>
#include <functional>
#include <string>

namespace rnetemu {

using ReplayMatcher = std::function<bool(const CanMessage&)>;
using ReplayConsumer = std::function<void(const CanMessage&,
                                          std::uint64_t generation,
                                          const std::string& owner)>;

} // namespace rnetemu

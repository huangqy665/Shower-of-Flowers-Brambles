#pragma once

#include <string_view>

namespace dillen::parser {

bool MatchPathPattern(
    std::string_view pattern,
    std::string_view path
) noexcept;

}

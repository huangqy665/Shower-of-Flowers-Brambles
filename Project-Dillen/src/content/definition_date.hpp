#pragma once

namespace dillen::content {

struct DefinitionDate
{
    int year = 0;
    int month = 0;
    int day = 0;
};

bool operator==(
    const DefinitionDate& first,
    const DefinitionDate& second
) noexcept;
bool operator!=(
    const DefinitionDate& first,
    const DefinitionDate& second
) noexcept;
bool operator<(
    const DefinitionDate& first,
    const DefinitionDate& second
) noexcept;

}

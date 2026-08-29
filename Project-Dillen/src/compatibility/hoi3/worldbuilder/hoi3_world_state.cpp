#include "hoi3_world_state.hpp"

#include <algorithm>
#include <utility>

namespace dillen::compatibility::hoi3::worldbuilder {

RuntimeUnitId::operator bool() const noexcept
{
    return value != 0;
}

bool operator==(RuntimeUnitId first, RuntimeUnitId second) noexcept
{
    return first.value == second.value;
}

bool operator!=(RuntimeUnitId first, RuntimeUnitId second) noexcept
{
    return !(first == second);
}

bool operator<(RuntimeUnitId first, RuntimeUnitId second) noexcept
{
    return first.value < second.value;
}

std::optional<double> ProvinceState::Numeric(
    dillen::compatibility::hoi3::content::ProvinceHistoryField field
) const
{
    const auto iterator = numericValues.find(field);
    return iterator == numericValues.end()
        ? std::nullopt
        : std::optional<double>{iterator->second};
}

const dillen::compatibility::hoi3::content::DefinitionDate& Hoi3WorldState::Date() const noexcept
{
    return date_;
}

const std::vector<CountryState>&
Hoi3WorldState::Countries() const noexcept
{
    return countries_;
}

const std::vector<ProvinceState>&
Hoi3WorldState::Provinces() const noexcept
{
    return provinces_;
}

const std::vector<RuntimeUnitState>&
Hoi3WorldState::Units() const noexcept
{
    return units_;
}

const std::vector<CountryRelationState>&
Hoi3WorldState::Relations() const noexcept
{
    return relations_;
}

const std::vector<RuntimeWarState>&
Hoi3WorldState::Wars() const noexcept
{
    return wars_;
}

const kernel::MechanismInstanceStore&
Hoi3WorldState::Mechanisms() const noexcept
{
    return world_.Mechanisms();
}

const world::AuthoritativeWorld&
Hoi3WorldState::World() const noexcept
{
    return world_;
}

world::AuthoritativeWorld& Hoi3WorldState::World() noexcept
{
    return world_;
}

const std::set<std::string>&
Hoi3WorldState::GlobalFlags() const noexcept
{
    return globalFlags_;
}

std::optional<dillen::compatibility::hoi3::content::BookmarkDefinitionId>
Hoi3WorldState::Bookmark() const noexcept
{
    return bookmark_;
}

std::optional<dillen::compatibility::hoi3::content::ScenarioDefinitionId>
Hoi3WorldState::Scenario() const noexcept
{
    return scenario_;
}

const CountryState* Hoi3WorldState::FindCountry(
    dillen::compatibility::hoi3::content::CountryDefinitionId id
) const
{
    const auto iterator = std::lower_bound(
        countries_.begin(),
        countries_.end(),
        id,
        [](const CountryState& country, dillen::compatibility::hoi3::content::CountryDefinitionId value)
        {
            return country.id < value;
        }
    );
    return iterator == countries_.end() || iterator->id != id
        ? nullptr
        : &*iterator;
}

const CountryState* Hoi3WorldState::FindCountry(
    std::string_view tag
) const
{
    const auto parsed = dillen::compatibility::hoi3::content::CountryTag::Parse(tag);
    return parsed ? FindCountry(parsed->StableId()) : nullptr;
}

const ProvinceState* Hoi3WorldState::FindProvince(
    dillen::compatibility::hoi3::content::ProvinceDefinitionId id
) const
{
    const auto iterator = std::lower_bound(
        provinces_.begin(),
        provinces_.end(),
        id,
        [](const ProvinceState& province,
           dillen::compatibility::hoi3::content::ProvinceDefinitionId value)
        {
            return province.id < value;
        }
    );
    return iterator == provinces_.end() || iterator->id != id
        ? nullptr
        : &*iterator;
}

const ProvinceState* Hoi3WorldState::FindProvince(
    std::uint32_t id
) const
{
    return FindProvince(dillen::compatibility::hoi3::content::ProvinceDefinitionId{id});
}

const RuntimeUnitState* Hoi3WorldState::FindUnit(RuntimeUnitId id) const
{
    const auto iterator = std::lower_bound(
        units_.begin(),
        units_.end(),
        id,
        [](const RuntimeUnitState& unit, RuntimeUnitId value)
        {
            return unit.id < value;
        }
    );
    return iterator == units_.end() || iterator->id != id
        ? nullptr
        : &*iterator;
}

const RuntimeWarState* Hoi3WorldState::FindWar(
    dillen::compatibility::hoi3::content::WarHistoryDefinitionId id
) const
{
    const auto iterator = std::lower_bound(
        wars_.begin(),
        wars_.end(),
        id,
        [](const RuntimeWarState& war,
           dillen::compatibility::hoi3::content::WarHistoryDefinitionId value)
        {
            return war.id < value;
        }
    );
    return iterator == wars_.end() || iterator->id != id
        ? nullptr
        : &*iterator;
}

CountryState* Hoi3WorldState::FindCountryMutable(
    dillen::compatibility::hoi3::content::CountryDefinitionId id
)
{
    return const_cast<CountryState*>(
        static_cast<const Hoi3WorldState&>(*this).FindCountry(id)
    );
}

ProvinceState* Hoi3WorldState::FindProvinceMutable(
    dillen::compatibility::hoi3::content::ProvinceDefinitionId id
)
{
    return const_cast<ProvinceState*>(
        static_cast<const Hoi3WorldState&>(*this).FindProvince(id)
    );
}

RuntimeUnitState* Hoi3WorldState::FindUnitMutable(RuntimeUnitId id)
{
    return const_cast<RuntimeUnitState*>(
        static_cast<const Hoi3WorldState&>(*this).FindUnit(id)
    );
}

}

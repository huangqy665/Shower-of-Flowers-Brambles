#include "definition_registry.hpp"

namespace dillen::content {

CountryDefinitionRegistry& DefinitionRegistry::Countries() noexcept
{
    return countries_;
}

const CountryDefinitionRegistry& DefinitionRegistry::Countries() const noexcept
{
    return countries_;
}

CountryHistoryRegistry& DefinitionRegistry::CountryHistories() noexcept
{
    return countryHistories_;
}

const CountryHistoryRegistry&
DefinitionRegistry::CountryHistories() const noexcept
{
    return countryHistories_;
}

DiplomacyHistoryRegistry& DefinitionRegistry::DiplomacyHistories() noexcept
{
    return diplomacyHistories_;
}

const DiplomacyHistoryRegistry&
DefinitionRegistry::DiplomacyHistories() const noexcept
{
    return diplomacyHistories_;
}

LaunchDefinitionRegistry& DefinitionRegistry::Launches() noexcept
{
    return launches_;
}

const LaunchDefinitionRegistry& DefinitionRegistry::Launches() const noexcept
{
    return launches_;
}

ProvinceDefinitionRegistry& DefinitionRegistry::Provinces() noexcept
{
    return provinces_;
}

const ProvinceDefinitionRegistry&
DefinitionRegistry::Provinces() const noexcept
{
    return provinces_;
}

RegionDefinitionRegistry& DefinitionRegistry::Regions() noexcept
{
    return regions_;
}

const RegionDefinitionRegistry& DefinitionRegistry::Regions() const noexcept
{
    return regions_;
}

ProvinceHistoryRegistry& DefinitionRegistry::ProvinceHistories() noexcept
{
    return provinceHistories_;
}

const ProvinceHistoryRegistry&
DefinitionRegistry::ProvinceHistories() const noexcept
{
    return provinceHistories_;
}

UnitTypeDefinitionRegistry& DefinitionRegistry::UnitTypes() noexcept
{
    return unitTypes_;
}

const UnitTypeDefinitionRegistry& DefinitionRegistry::UnitTypes() const noexcept
{
    return unitTypes_;
}

TechnologyDefinitionRegistry& DefinitionRegistry::Technologies() noexcept
{
    return technologies_;
}

const TechnologyDefinitionRegistry&
DefinitionRegistry::Technologies() const noexcept
{
    return technologies_;
}

UnitModelDefinitionRegistry& DefinitionRegistry::UnitModels() noexcept
{
    return unitModels_;
}

const UnitModelDefinitionRegistry&
DefinitionRegistry::UnitModels() const noexcept
{
    return unitModels_;
}

OrderOfBattleDefinitionRegistry&
DefinitionRegistry::OrdersOfBattle() noexcept
{
    return ordersOfBattle_;
}

const OrderOfBattleDefinitionRegistry&
DefinitionRegistry::OrdersOfBattle() const noexcept
{
    return ordersOfBattle_;
}

WarHistoryRegistry& DefinitionRegistry::WarHistories() noexcept
{
    return warHistories_;
}

const WarHistoryRegistry& DefinitionRegistry::WarHistories() const noexcept
{
    return warHistories_;
}

void DefinitionRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    countries_.Clear();
    countryHistories_.Clear();
    diplomacyHistories_.Clear();
    launches_.Clear();
    provinces_.Clear();
    regions_.Clear();
    provinceHistories_.Clear();
    unitTypes_.Clear();
    technologies_.Clear();
    unitModels_.Clear();
    ordersOfBattle_.Clear();
    warHistories_.Clear();
}

void DefinitionRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    countries_.Freeze();
    countryHistories_.Freeze();
    diplomacyHistories_.Freeze();
    launches_.Freeze();
    provinces_.Freeze();
    regions_.Freeze();
    provinceHistories_.Freeze();
    unitTypes_.Freeze();
    technologies_.Freeze();
    unitModels_.Freeze();
    ordersOfBattle_.Freeze();
    warHistories_.Freeze();
    frozen_ = true;
}

bool DefinitionRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

}

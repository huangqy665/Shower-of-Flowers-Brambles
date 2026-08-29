#pragma once

#include "country_definition_registry.hpp"
#include "country_history_registry.hpp"
#include "diplomacy_history_registry.hpp"
#include "launch_definition_registry.hpp"
#include "order_of_battle_definition_registry.hpp"
#include "province_definition_registry.hpp"
#include "province_history_registry.hpp"
#include "region_definition_registry.hpp"
#include "technology_definition_registry.hpp"
#include "unit_model_definition_registry.hpp"
#include "unit_type_definition_registry.hpp"
#include "war_history_registry.hpp"

namespace dillen::compatibility::hoi3::content {

class DefinitionRegistry
{
public:
    CountryDefinitionRegistry& Countries() noexcept;
    const CountryDefinitionRegistry& Countries() const noexcept;
    CountryHistoryRegistry& CountryHistories() noexcept;
    const CountryHistoryRegistry& CountryHistories() const noexcept;
    DiplomacyHistoryRegistry& DiplomacyHistories() noexcept;
    const DiplomacyHistoryRegistry& DiplomacyHistories() const noexcept;
    LaunchDefinitionRegistry& Launches() noexcept;
    const LaunchDefinitionRegistry& Launches() const noexcept;
    ProvinceDefinitionRegistry& Provinces() noexcept;
    const ProvinceDefinitionRegistry& Provinces() const noexcept;
    RegionDefinitionRegistry& Regions() noexcept;
    const RegionDefinitionRegistry& Regions() const noexcept;
    ProvinceHistoryRegistry& ProvinceHistories() noexcept;
    const ProvinceHistoryRegistry& ProvinceHistories() const noexcept;
    UnitTypeDefinitionRegistry& UnitTypes() noexcept;
    const UnitTypeDefinitionRegistry& UnitTypes() const noexcept;
    TechnologyDefinitionRegistry& Technologies() noexcept;
    const TechnologyDefinitionRegistry& Technologies() const noexcept;
    UnitModelDefinitionRegistry& UnitModels() noexcept;
    const UnitModelDefinitionRegistry& UnitModels() const noexcept;
    OrderOfBattleDefinitionRegistry& OrdersOfBattle() noexcept;
    const OrderOfBattleDefinitionRegistry& OrdersOfBattle() const noexcept;
    WarHistoryRegistry& WarHistories() noexcept;
    const WarHistoryRegistry& WarHistories() const noexcept;
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;

private:
    CountryDefinitionRegistry countries_;
    CountryHistoryRegistry countryHistories_;
    DiplomacyHistoryRegistry diplomacyHistories_;
    LaunchDefinitionRegistry launches_;
    ProvinceDefinitionRegistry provinces_;
    RegionDefinitionRegistry regions_;
    ProvinceHistoryRegistry provinceHistories_;
    UnitTypeDefinitionRegistry unitTypes_;
    TechnologyDefinitionRegistry technologies_;
    UnitModelDefinitionRegistry unitModels_;
    OrderOfBattleDefinitionRegistry ordersOfBattle_;
    WarHistoryRegistry warHistories_;
    bool frozen_ = false;
};

}

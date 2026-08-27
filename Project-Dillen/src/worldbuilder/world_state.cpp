#include "world_state.hpp"

#include <algorithm>
#include <utility>

namespace dillen::worldbuilder {

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
    content::ProvinceHistoryField field
) const
{
    const auto iterator = numericValues.find(field);
    return iterator == numericValues.end()
        ? std::nullopt
        : std::optional<double>{iterator->second};
}

const content::DefinitionDate& AuthoritativeWorld::Date() const noexcept
{
    return date_;
}

const std::vector<CountryState>&
AuthoritativeWorld::Countries() const noexcept
{
    return countries_;
}

const std::vector<ProvinceState>&
AuthoritativeWorld::Provinces() const noexcept
{
    return provinces_;
}

const std::vector<RuntimeUnitState>&
AuthoritativeWorld::Units() const noexcept
{
    return units_;
}

const std::vector<CountryRelationState>&
AuthoritativeWorld::Relations() const noexcept
{
    return relations_;
}

const std::vector<RuntimeWarState>&
AuthoritativeWorld::Wars() const noexcept
{
    return wars_;
}

const kernel::MechanismInstanceStore&
AuthoritativeWorld::Mechanisms() const noexcept
{
    return mechanisms_;
}

const kernel::MechanismQuerySnapshot&
AuthoritativeWorld::MechanismSnapshot() const noexcept
{
    return mechanismSnapshot_;
}

const kernel::WorldCommandQueue&
AuthoritativeWorld::WorldCommands() const noexcept
{
    return worldCommands_;
}

const kernel::WorldEventQueue&
AuthoritativeWorld::WorldEvents() const noexcept
{
    return worldEvents_;
}

std::uint64_t AuthoritativeWorld::Tick() const noexcept
{
    return mechanismScheduler_.CurrentTick();
}

std::uint64_t AuthoritativeWorld::Revision() const noexcept
{
    return revision_;
}

std::uint64_t AuthoritativeWorld::EnqueueWorldTransaction(
    kernel::WorldTransaction transaction,
    std::uint64_t notBeforeTick
)
{
    return worldCommands_.Enqueue(
        std::move(transaction),
        notBeforeTick
    );
}

kernel::WorldTransactionResult AuthoritativeWorld::ApplyWorldTransaction(
    const kernel::WorldTransaction& transaction,
    const kernel::MechanismDefinitionRegistry& definitions,
    const kernel::MechanismSchemaRegistry& schemas,
    std::uint64_t currentTick
)
{
    const std::uint64_t sequence = worldCommands_.ReserveSequence();
    if (currentTick < mechanismScheduler_.CurrentTick())
    {
        kernel::MechanismTransactionResult mechanism;
        mechanism.status = kernel::MechanismTransactionStatus::TickRegression;
        kernel::WorldTransactionResult rejected{
            kernel::WorldTransactionStatus::TickRegression,
            std::move(mechanism)
        };
        worldEvents_.PublishTransactionResult(
            currentTick,
            sequence,
            rejected
        );
        return rejected;
    }

    kernel::WorldTransactionResult result = kernel::ApplyWorldTransaction(
        mechanisms_,
        transaction,
        definitions,
        schemas,
        currentTick
    );
    worldEvents_.PublishTransactionResult(currentTick, sequence, result);
    if (result)
    {
        if (result.mechanism.changedInstances != 0)
        {
            ++revision_;
        }
        mechanismScheduler_.Reset(currentTick);
        mechanismSnapshot_.Publish(
            mechanisms_,
            currentTick,
            revision_
        );
    }
    return result;
}

kernel::MechanismTransactionResult
AuthoritativeWorld::ApplyMechanismTransaction(
    const std::vector<kernel::MechanismCommand>& commands,
    const kernel::MechanismDefinitionRegistry& definitions,
    const kernel::MechanismSchemaRegistry& schemas,
    std::uint64_t currentTick
)
{
    kernel::WorldTransactionResult result = ApplyWorldTransaction(
        kernel::WorldTransaction::FromMechanismCommands(commands),
        definitions,
        schemas,
        currentTick
    );
    return std::move(result.mechanism);
}

kernel::MechanismSchedulerTickResult
AuthoritativeWorld::RunMechanismSchedulerTick(
    const kernel::MechanismDefinitionRegistry& definitions,
    const kernel::MechanismSchemaRegistry& schemas,
    std::uint64_t nextTick
)
{
    return mechanismScheduler_.RunTick(
        mechanisms_,
        worldCommands_,
        worldEvents_,
        mechanismSnapshot_,
        definitions,
        schemas,
        nextTick,
        revision_
    );
}

std::vector<kernel::WorldEvent> AuthoritativeWorld::DrainWorldEvents()
{
    return worldEvents_.Drain();
}

const std::set<std::string>&
AuthoritativeWorld::GlobalFlags() const noexcept
{
    return globalFlags_;
}

std::optional<content::BookmarkDefinitionId>
AuthoritativeWorld::Bookmark() const noexcept
{
    return bookmark_;
}

std::optional<content::ScenarioDefinitionId>
AuthoritativeWorld::Scenario() const noexcept
{
    return scenario_;
}

const CountryState* AuthoritativeWorld::FindCountry(
    content::CountryDefinitionId id
) const
{
    const auto iterator = std::lower_bound(
        countries_.begin(),
        countries_.end(),
        id,
        [](const CountryState& country, content::CountryDefinitionId value)
        {
            return country.id < value;
        }
    );
    return iterator == countries_.end() || iterator->id != id
        ? nullptr
        : &*iterator;
}

const CountryState* AuthoritativeWorld::FindCountry(
    std::string_view tag
) const
{
    const auto parsed = content::CountryTag::Parse(tag);
    return parsed ? FindCountry(parsed->StableId()) : nullptr;
}

const ProvinceState* AuthoritativeWorld::FindProvince(
    content::ProvinceDefinitionId id
) const
{
    const auto iterator = std::lower_bound(
        provinces_.begin(),
        provinces_.end(),
        id,
        [](const ProvinceState& province,
           content::ProvinceDefinitionId value)
        {
            return province.id < value;
        }
    );
    return iterator == provinces_.end() || iterator->id != id
        ? nullptr
        : &*iterator;
}

const ProvinceState* AuthoritativeWorld::FindProvince(
    std::uint32_t id
) const
{
    return FindProvince(content::ProvinceDefinitionId{id});
}

const RuntimeUnitState* AuthoritativeWorld::FindUnit(RuntimeUnitId id) const
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

const RuntimeWarState* AuthoritativeWorld::FindWar(
    content::WarHistoryDefinitionId id
) const
{
    const auto iterator = std::lower_bound(
        wars_.begin(),
        wars_.end(),
        id,
        [](const RuntimeWarState& war,
           content::WarHistoryDefinitionId value)
        {
            return war.id < value;
        }
    );
    return iterator == wars_.end() || iterator->id != id
        ? nullptr
        : &*iterator;
}

CountryState* AuthoritativeWorld::FindCountryMutable(
    content::CountryDefinitionId id
)
{
    return const_cast<CountryState*>(
        static_cast<const AuthoritativeWorld&>(*this).FindCountry(id)
    );
}

ProvinceState* AuthoritativeWorld::FindProvinceMutable(
    content::ProvinceDefinitionId id
)
{
    return const_cast<ProvinceState*>(
        static_cast<const AuthoritativeWorld&>(*this).FindProvince(id)
    );
}

RuntimeUnitState* AuthoritativeWorld::FindUnitMutable(RuntimeUnitId id)
{
    return const_cast<RuntimeUnitState*>(
        static_cast<const AuthoritativeWorld&>(*this).FindUnit(id)
    );
}

}

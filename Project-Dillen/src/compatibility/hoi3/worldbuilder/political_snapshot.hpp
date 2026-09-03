#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "country_definition.hpp"
#include "country_tag_definition.hpp"
#include "definition_date.hpp"
#include "definition_origin.hpp"
#include "definition_registry.hpp"
#include "province_definition.hpp"

namespace dillen::compatibility::hoi3::worldbuilder {

enum class PoliticalSnapshotIssueSeverity
{
    Warning,
    Error
};

struct PoliticalSnapshotIssue
{
    PoliticalSnapshotIssueSeverity severity =
        PoliticalSnapshotIssueSeverity::Error;

    std::string code;
    std::string message;

    content::DefinitionOrigin origin;
};

struct PoliticalSnapshotReport
{
    std::vector<PoliticalSnapshotIssue> issues;

    void Clear();

    void Warning(
        std::string code,
        std::string message,
        content::DefinitionOrigin origin = {}
    );

    void Error(
        std::string code,
        std::string message,
        content::DefinitionOrigin origin = {}
    );

    bool HasErrors() const noexcept;

    std::size_t WarningCount() const noexcept;
    std::size_t ErrorCount() const noexcept;
};

struct PoliticalCountryState
{
    content::CountryDefinitionId id;
    content::CountryTag tag;

    std::optional<content::CountryColor> color;

    std::optional<
        content::ProvinceDefinitionId
    > capital;
};

struct PoliticalProvinceState
{
    content::ProvinceDefinitionId id;

    std::optional<
        content::CountryDefinitionId
    > owner;

    std::optional<
        content::CountryDefinitionId
    > controller;

    std::vector<
        content::CountryDefinitionId
    > cores;
};

struct PoliticalSnapshot
{
    content::DefinitionDate date;

    std::vector<
        PoliticalCountryState
    > countries;

    std::vector<
        PoliticalProvinceState
    > provinces;
};

bool BuildPoliticalSnapshot(
    const content::DefinitionRegistry& definitions,
    content::DefinitionDate date,
    PoliticalSnapshot& output,
    PoliticalSnapshotReport& report
);

}
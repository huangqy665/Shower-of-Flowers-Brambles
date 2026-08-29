#pragma once

#include <variant>

#include "algorithm_registry.hpp"
#include "component_schema.hpp"
#include "entity_definition.hpp"
#include "mechanism_definition.hpp"
#include "mechanism_schema.hpp"
#include "mechanism_spawn_definition.hpp"
#include "package_manifest.hpp"
#include "parse_result.hpp"
#include "parser_cursor.hpp"
#include "ruleset_composition.hpp"
#include "relation_definition.hpp"
#include "relation_schema.hpp"
#include "runtime_capability_contract.hpp"

namespace dillen::authoring {

inline constexpr parser::DialectId kDillenAuthoringDialect =
    0x44494C4C454E0001ULL;

inline constexpr parser::ParserId kMechanismTemplateParser =
    0x44494C4C454E1001ULL;
inline constexpr parser::ParserId kAlgorithmDescriptorParser =
    0x44494C4C454E1002ULL;
inline constexpr parser::ParserId kMechanismDefinitionParser =
    0x44494C4C454E1003ULL;
inline constexpr parser::ParserId kMechanismSpawnParser =
    0x44494C4C454E1004ULL;
inline constexpr parser::ParserId kRulesetParser =
    0x44494C4C454E1005ULL;
inline constexpr parser::ParserId kComponentSchemaParser =
    0x44494C4C454E1006ULL;
inline constexpr parser::ParserId kEntityDefinitionParser =
    0x44494C4C454E1007ULL;
inline constexpr parser::ParserId kRelationSchemaParser =
    0x44494C4C454E1008ULL;
inline constexpr parser::ParserId kRelationDefinitionParser =
    0x44494C4C454E1009ULL;
inline constexpr parser::ParserId kPackageManifestParser =
    0x44494C4C454E100AULL;
inline constexpr parser::ParserId kCapabilityContractParser =
    0x44494C4C454E100BULL;

inline constexpr parser::DefinitionTypeId kMechanismTemplateDocumentType =
    0x44494C4C454E2001ULL;
inline constexpr parser::DefinitionTypeId kAlgorithmDescriptorDocumentType =
    0x44494C4C454E2002ULL;
inline constexpr parser::DefinitionTypeId kMechanismDefinitionDocumentType =
    0x44494C4C454E2003ULL;
inline constexpr parser::DefinitionTypeId kMechanismSpawnDocumentType =
    0x44494C4C454E2004ULL;
inline constexpr parser::DefinitionTypeId kRulesetDocumentType =
    0x44494C4C454E2005ULL;
inline constexpr parser::DefinitionTypeId kComponentSchemaDocumentType =
    0x44494C4C454E2006ULL;
inline constexpr parser::DefinitionTypeId kEntityDefinitionDocumentType =
    0x44494C4C454E2007ULL;
inline constexpr parser::DefinitionTypeId kRelationSchemaDocumentType =
    0x44494C4C454E2008ULL;
inline constexpr parser::DefinitionTypeId kRelationDefinitionDocumentType =
    0x44494C4C454E2009ULL;
inline constexpr parser::DefinitionTypeId kPackageManifestDocumentType =
    0x44494C4C454E200AULL;
inline constexpr parser::DefinitionTypeId kCapabilityContractDocumentType =
    0x44494C4C454E200BULL;

template <typename T>
struct AuthoringDocument
{
    T value;
    parser::SourceSpan declarationSpan;
};

using MechanismTemplateDocument =
    AuthoringDocument<kernel::MechanismSchema>;
using AlgorithmDescriptorDocument =
    AuthoringDocument<kernel::AlgorithmDescriptor>;
using MechanismDefinitionDocument =
    AuthoringDocument<kernel::MechanismDefinition>;
using MechanismSpawnDocument =
    AuthoringDocument<kernel::MechanismSpawnDefinition>;
using ComponentSchemaDocument =
    AuthoringDocument<kernel::ComponentSchema>;
using EntityDefinitionDocument =
    AuthoringDocument<kernel::EntityDefinition>;
using RelationSchemaDocument =
    AuthoringDocument<kernel::RelationSchema>;
using RelationDefinitionDocument =
    AuthoringDocument<kernel::RelationDefinition>;
using PackageManifestDocument =
    AuthoringDocument<kernel::PackageManifest>;
using CapabilityContractDocument =
    AuthoringDocument<kernel::RuntimeCapabilityContract>;

struct RulesetDocument
{
    std::variant<
        kernel::RootRulesetDefinition,
        kernel::ExtensionRulesetDefinition
    > value;
    parser::SourceSpan declarationSpan;
};

bool ParseMechanismTemplate(
    parser::ParserCursor& cursor,
    parser::ParseArtifact& artifact
);
bool ParseAlgorithmDescriptor(
    parser::ParserCursor& cursor,
    parser::ParseArtifact& artifact
);
bool ParseMechanismDefinition(
    parser::ParserCursor& cursor,
    parser::ParseArtifact& artifact
);
bool ParseMechanismSpawn(
    parser::ParserCursor& cursor,
    parser::ParseArtifact& artifact
);
bool ParseRuleset(
    parser::ParserCursor& cursor,
    parser::ParseArtifact& artifact
);
bool ParseComponentSchema(
    parser::ParserCursor& cursor,
    parser::ParseArtifact& artifact
);
bool ParseEntityDefinition(
    parser::ParserCursor& cursor,
    parser::ParseArtifact& artifact
);
bool ParseRelationSchema(
    parser::ParserCursor& cursor,
    parser::ParseArtifact& artifact
);
bool ParseRelationDefinition(
    parser::ParserCursor& cursor,
    parser::ParseArtifact& artifact
);
bool ParsePackageManifest(
    parser::ParserCursor& cursor,
    parser::ParseArtifact& artifact
);
bool ParseCapabilityContract(
    parser::ParserCursor& cursor,
    parser::ParseArtifact& artifact
);

}

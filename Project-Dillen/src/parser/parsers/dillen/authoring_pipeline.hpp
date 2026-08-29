#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "algorithm_registry.hpp"
#include "component_schema.hpp"
#include "entity_definition_registry.hpp"
#include "file_catalog.hpp"
#include "frozen_runtime_catalog.hpp"
#include "mechanism_definition_registry.hpp"
#include "mechanism_schema_registry.hpp"
#include "mechanism_spawn_definition_registry.hpp"
#include "package_manifest.hpp"
#include "relation_definition_registry.hpp"
#include "parser_registry.hpp"
#include "resolver.hpp"
#include "ruleset.hpp"
#include "ruleset_composition.hpp"
#include "runtime_capability_contract.hpp"
#include "runtime_compiler.hpp"
#include "source_lock.hpp"
#include "template_registry.hpp"

namespace dillen::authoring {

struct SelectedRulesetVersion
{
    kernel::RulesetId id;
    std::string canonicalName;
    std::uint32_t version = 0;
};

struct AuthoringLaunchSelection
{
    SelectedRulesetVersion root;
    std::vector<SelectedRulesetVersion> extensions;
};

class AuthoringSession
{
public:
    explicit AuthoringSession(AuthoringLaunchSelection selection);

    bool Register(
        parser::TemplateRegistry& templates,
        parser::ParserRegistry& parsers,
        parser::Resolver& resolver
    );

    const kernel::MechanismSchemaRegistry& MechanismSchemas()
        const noexcept;
    const kernel::AlgorithmRegistry& Algorithms() const noexcept;
    const kernel::MechanismDefinitionRegistry& MechanismDefinitions()
        const noexcept;
    const kernel::MechanismSpawnDefinitionRegistry& MechanismSpawns()
        const noexcept;
    const kernel::RelationSchemaRegistry& RelationSchemas() const noexcept;
    const kernel::RelationDefinitionRegistry& RelationDefinitions()
        const noexcept;
    const kernel::RulesetRegistry& Rulesets() const noexcept;
    const kernel::RulesetDefinition* ComposedRuleset() const noexcept;
    const kernel::FrozenRuntimeCatalog& RuntimeCatalog() const noexcept;
    const kernel::PackageLock& LockedPackages() const noexcept;
    const kernel::SourceLock& LockedSources() const noexcept;
    const kernel::RuntimeCompileReport& CompileReport() const noexcept;

private:
    bool Declare(
        parser::ParseWorkspace& workspace,
        parser::DiagnosticBag& diagnostics
    );
    bool Resolve(
        parser::ParseWorkspace& workspace,
        parser::DiagnosticBag& diagnostics
    );
    bool ValidateAndCompile(
        parser::ParseWorkspace& workspace,
        parser::DiagnosticBag& diagnostics
    );

    AuthoringLaunchSelection selection_;
    kernel::MechanismSchemaRegistry mechanismSchemas_;
    kernel::ComponentSchemaRegistry componentSchemas_;
    kernel::RelationSchemaRegistry relationSchemas_;
    kernel::AlgorithmRegistry algorithms_;
    kernel::MechanismDefinitionRegistry mechanismDefinitions_;
    kernel::EntityDefinitionRegistry entityDefinitions_;
    kernel::RelationDefinitionRegistry relationDefinitions_;
    kernel::MechanismSpawnDefinitionRegistry mechanismSpawns_;
    kernel::RuntimeCapabilityContractRegistry capabilityContracts_;
    kernel::PackageManifestRegistry packageManifests_;
    kernel::RulesetRegistry rulesets_;
    std::vector<kernel::RootRulesetDefinition> rootRulesets_;
    std::vector<kernel::ExtensionRulesetDefinition> extensionRulesets_;
    kernel::RulesetDefinition composedRuleset_;
    kernel::PackageLock packageLock_;
    kernel::SourceLock sourceLock_;
    kernel::FrozenRuntimeCatalog runtimeCatalog_;
    kernel::RuntimeCompileReport compileReport_;
    bool composed_ = false;
};

}

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "mechanism_ids.hpp"
#include "mechanism_value.hpp"
#include "runtime_capability_contract.hpp"

namespace dillen::kernel {

struct MechanismDefinitionSource
{
    std::string sourceName;
    std::string virtualPath;
    std::uint32_t line = 1;
    std::uint32_t column = 1;
};

struct MechanismDefinition
{
    MechanismDefinitionId id;
    MechanismTypeId type;
    std::string canonicalName;
    std::uint32_t schemaVersion = 0;
    AlgorithmId algorithm;
    std::uint32_t algorithmVersion = 0;
    std::map<std::string, MechanismValue> fields;
    std::map<std::string, std::vector<MechanismReference>> roles;
    // Capability Contracts this Definition's instances answer, each with the
    // contract version range the Definition accepts. A consumer invokes a
    // Capability by contract id (+ optional version) alone; the runtime fans
    // the invocation out to every instance whose Definition lists it here and
    // whose resolved version matches, with no identity coupling between sides.
    std::vector<CapabilityProvisionDeclaration> providedCapabilities;
    MechanismDefinitionSource source;
};

}

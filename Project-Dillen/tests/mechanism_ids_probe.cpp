#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>

#include "mechanism_ids.hpp"

// Golden-value probe for the stable identity layer. These hashes feed the
// Ruleset Fingerprint, the Source Lock, every persisted authoritative
// reference and the deterministic replay checksums, so they must never change
// silently. The expected values below are the frozen output of the current
// FNV-1a scheme; any refactor of the ID types must keep this probe passing
// byte-for-byte.

namespace
{
using namespace dillen::kernel;

int failures = 0;

void Check(const char* label, std::uint64_t actual, std::uint64_t expected)
{
    if (actual != expected)
    {
        ++failures;
        std::cerr << "  MISMATCH " << label << " actual=0x" << std::hex
                  << actual << " expected=0x" << expected << std::dec << "\n";
    }
}

void Expect(const char* label, bool condition)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "  FAILED  " << label << "\n";
    }
}

// Fixed inputs, chained so definition/instance ids exercise the composite
// hashes as well as the leaf ones.
const MechanismTypeId kMechType =
    StableMechanismTypeId("dillen.test.alpha");
const MechanismDefinitionId kMechDef =
    StableMechanismDefinitionId(kMechType, "dillen.test.alpha.default");
const MechanismInstanceId kMechInst =
    StableMechanismInstanceId(kMechDef, 7);
const MechanismSpawnDefinitionId kMechSpawn =
    StableMechanismSpawnDefinitionId(kMechDef, "dillen.test.alpha.initial");
const AlgorithmId kAlgo = StableAlgorithmId("dillen.test.alpha.algorithm");
const PackageId kPackage = StablePackageId("dillen.test.package");
const RulesetId kRuleset = StableRulesetId("dillen.test.root");
const CapabilityId kCapability =
    StableCapabilityId("dillen.test.capability");
const AlgorithmEventTypeId kEventType =
    StableAlgorithmEventTypeId("dillen.test.event");
const RngStreamId kRngStream = StableRngStreamId("dillen.test.rng");
const EntityTypeId kEntityType = StableEntityTypeId("dillen.test.entity");
const EntityDefinitionId kEntityDef =
    StableEntityDefinitionId(kEntityType, "dillen.test.entity.default");
const EntityId kEntity = StableEntityId(kEntityDef, 3);
const ComponentTypeId kComponentType =
    StableComponentTypeId("dillen.test.component");
const RelationTypeId kRelationType =
    StableRelationTypeId("dillen.test.relation");
const RelationDefinitionId kRelationDef =
    StableRelationDefinitionId(kRelationType, "dillen.test.relation.default");
const RelationId kRelation =
    StableRelationId(kRelationType, kEntity, StableEntityId(kEntityDef, 4));

}

int main()
{
    // ---- Golden hash values (frozen) -------------------------------------
    Check("MechanismTypeId", kMechType.value, 0xb16e6f06ff3b87f5ULL);
    Check("MechanismDefinitionId", kMechDef.value, 0xee4ea7f844dc427bULL);
    Check("MechanismInstanceId", kMechInst.value, 0xd9cdbd313cec86e8ULL);
    Check(
        "MechanismSpawnDefinitionId",
        kMechSpawn.value,
        0x56b58edb8deb265eULL
    );
    Check("AlgorithmId", kAlgo.value, 0xdcea4ef7f71a04e1ULL);
    Check("PackageId", kPackage.value, 0x9d6aaa3617092060ULL);
    Check("RulesetId", kRuleset.value, 0xf02d173cbad88aa6ULL);
    Check("CapabilityId", kCapability.value, 0x5129a899bd26bf2aULL);
    Check("AlgorithmEventTypeId", kEventType.value, 0xc4cb806e596ca0d8ULL);
    Check("RngStreamId", kRngStream.value, 0x1f24b1044c5100a4ULL);
    Check("EntityTypeId", kEntityType.value, 0x269aef86e21c5b78ULL);
    Check("EntityDefinitionId", kEntityDef.value, 0x7ce432a983215c9bULL);
    Check("EntityId", kEntity.value, 0xb273888f35b821dcULL);
    Check("ComponentTypeId", kComponentType.value, 0xd2e17dab68652be0ULL);
    Check("RelationTypeId", kRelationType.value, 0xfcddb42a38f25648ULL);
    Check("RelationDefinitionId", kRelationDef.value, 0x0f4310322705bcdaULL);
    Check("RelationId", kRelation.value, 0xa9864641582663e1ULL);

    // ---- Normalisation equivalence -------------------------------------
    Expect(
        "case folding",
        StableMechanismTypeId("Dillen.Test.Alpha") == kMechType
    );
    Expect(
        "backslash to slash",
        StableMechanismTypeId("dillen\\test\\alpha")
            == StableMechanismTypeId("dillen/test/alpha")
    );
    Expect(
        "distinct domains",
        StableAlgorithmId("dillen.test.alpha").value
            != StableMechanismTypeId("dillen.test.alpha").value
    );

    // ---- operator bool / comparisons ---------------------------------
    Expect("uint64 empty is false", !static_cast<bool>(MechanismTypeId{}));
    Expect("uint64 set is true", static_cast<bool>(MechanismTypeId{1}));
    Expect("slot empty is false", !static_cast<bool>(MechanismFieldSlotId{}));
    Expect("slot zero is true", static_cast<bool>(MechanismFieldSlotId{0}));
    Expect(
        "slot max is false",
        !static_cast<bool>(MechanismFieldSlotId{UINT32_MAX})
    );
    Expect("equality", MechanismTypeId{5} == MechanismTypeId{5});
    Expect("inequality", MechanismTypeId{5} != MechanismTypeId{6});
    Expect("ordering", MechanismTypeId{5} < MechanismTypeId{6});

    // ---- std::hash specialisation ----------------------------------
    std::unordered_map<MechanismTypeId, int> byId;
    byId[kMechType] = 11;
    byId[StableMechanismTypeId("dillen.test.beta")] = 22;
    Expect("hashed lookup", byId.at(kMechType) == 11);
    Expect(
        "hashed lookup folds case",
        byId.at(StableMechanismTypeId("Dillen.Test.Alpha")) == 11
    );
    Expect("hashed size", byId.size() == 2);

    // ---- symbol validation ------------------------------------------
    Expect("valid symbol", IsValidMechanismSymbol("dillen.demo.counter"));
    Expect("empty invalid", !IsValidMechanismSymbol(""));
    Expect("leading dot invalid", !IsValidMechanismSymbol(".counter"));
    Expect("space invalid", !IsValidMechanismSymbol("a b"));

    if (failures != 0)
    {
        std::cerr << "mechanism ids probe: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "mechanism ids probe: passed\n";
    return 0;
}

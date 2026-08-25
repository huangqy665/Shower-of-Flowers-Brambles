#include <iostream>
#include <string>

#include "hoi3_native_object_keys.h"

int main()
{
    uint64_t stableId = 0;
    std::string tag;
    if (!core::PackHoi3CountryTag("chi", stableId)
        || !core::UnpackHoi3CountryTag(stableId, tag)
        || tag != "CHI"
        || core::PackHoi3CountryTag("CH", stableId)
        || core::PackHoi3CountryTag("C?I", stableId)
        || core::UnpackHoi3CountryTag(uint64_t{1} << 32, tag))
    {
        std::cerr << "HOI3 native object key probe failed\n";
        return 1;
    }

    std::string source;
    std::string target;
    if (!core::PackHoi3RelationKey("chi", "jap", stableId)
        || !core::UnpackHoi3RelationKey(stableId, source, target)
        || source != "CHI"
        || target != "JAP"
        || core::PackHoi3RelationKey("CHI", "CHI", stableId))
    {
        std::cerr << "HOI3 relation key probe failed\n";
        return 2;
    }

    uint32_t id0 = 0;
    uint32_t id1 = 0;
    if (!core::PackHoi3UnitKey(0x12345678u, 0x90ABCDEFu, stableId)
        || !core::UnpackHoi3UnitKey(stableId, id0, id1)
        || id0 != 0x12345678u
        || id1 != 0x90ABCDEFu
        || core::PackHoi3UnitKey(0, 0, stableId)
        || core::NormalizeHoi3DefinitionName("  Infantry_ Tech \t")
            != "infantry_tech")
    {
        std::cerr << "HOI3 unit/definition key probe failed\n";
        return 3;
    }
    if (!core::PackHoi3LeaderKey(0x1268u, 0x10203040u, stableId)
        || !core::UnpackHoi3LeaderKey(stableId, id0, id1)
        || id0 != 0x1268u
        || id1 != 0x10203040u
        || core::PackHoi3LeaderKey(0, 0, stableId))
    {
        std::cerr << "HOI3 leader key probe failed\n";
        return 4;
    }
    std::cout << "HOI3 native object key probe passed\n";
    return 0;
}

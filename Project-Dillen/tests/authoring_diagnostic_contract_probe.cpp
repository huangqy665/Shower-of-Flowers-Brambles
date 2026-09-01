#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "authoring_parser.hpp"
#include "diagnostic.hpp"

// The Diagnostic face of the frozen Authoring surface.
//
// A diagnostic code is a contract, not a log line. Authors key on them,
// editors and future tooling key on them, and CI configurations key on them.
// The codebase currently emits 96 distinct `dillen.authoring.*` codes. This
// probe freezes the full set and triggers the most author-facing failures end
// to end so an unreachable string literal cannot masquerade as coverage.
//
// Two halves, because either alone is weak:
//
//   1. The registry. Scanned out of the parser sources, the same way
//      architecture_guard_probe reads source rather than linking against it.
//      A code that is added, renamed or removed moves this set. This catches
//      silent drift across the whole surface.
//
//   2. Behaviour. A subset is triggered end to end against real malformed
//      sources and matched by exact code. The registry alone would happily
//      pass if a code still existed as a string literal but was no longer
//      reachable; these prove the ones authors hit most are still emitted for
//      the reason they say they are.
//
// Scanning source means this probe must be run from the repository root, which
// is what CTest's WORKING_DIRECTORY gives it -- again as architecture_guard
// already does.

namespace
{

// FROZEN. Sorted, deduplicated. Adding a code is additive and requires adding
// a line here. Renaming or removing one is a breaking change to the authoring
// surface and needs a migration note, not a quiet edit to this list.
const std::vector<std::string>& FrozenCodes()
{
    static const std::vector<std::string> codes = {
    "dillen.authoring.algorithm_backend_unknown",
    "dillen.authoring.algorithm_condition_unknown",
    "dillen.authoring.algorithm_entry_unknown",
    "dillen.authoring.algorithm_execution_policy_invalid",
    "dillen.authoring.algorithm_failure_policy_unknown",
    "dillen.authoring.algorithm_instruction_unknown",
    "dillen.authoring.algorithm_name_missing",
    "dillen.authoring.algorithm_program_backend_invalid",
    "dillen.authoring.algorithm_program_block_required",
    "dillen.authoring.algorithm_program_entry_mismatch",
    "dillen.authoring.algorithm_rejected",
    "dillen.authoring.algorithm_root_expected",
    "dillen.authoring.algorithm_stage_block_required",
    "dillen.authoring.algorithm_stage_duplicate",
    "dillen.authoring.algorithm_wall_clock_policy_conflict",
    "dillen.authoring.artifact_type_mismatch",
    "dillen.authoring.assignment_required",
    "dillen.authoring.binary_operator_unknown",
    "dillen.authoring.boolean_required",
    "dillen.authoring.capability_contract_rejected",
    "dillen.authoring.capability_requirement_expected",
    "dillen.authoring.compare_operator_unknown",
    "dillen.authoring.complex_value_not_scalar",
    "dillen.authoring.component_owner_ambiguous",
    "dillen.authoring.component_schema_rejected",
    "dillen.authoring.controlled_script_backend_invalid",
    "dillen.authoring.controlled_script_block_required",
    "dillen.authoring.controlled_script_entry_mismatch",
    "dillen.authoring.controlled_script_instruction_unknown",
    "dillen.authoring.controlled_script_stage_block_required",
    "dillen.authoring.controlled_script_stage_duplicate",
    "dillen.authoring.controlled_script_state_duplicate",
    "dillen.authoring.controlled_script_terminator_invalid",
    "dillen.authoring.definition_rejected",
    "dillen.authoring.definition_requirement_expected",
    "dillen.authoring.definition_root_expected",
    "dillen.authoring.duplicate_field",
    "dillen.authoring.duplicate_property",
    "dillen.authoring.duplicate_requirement",
    "dillen.authoring.entity_definition_rejected",
    "dillen.authoring.entry_point_list_required",
    "dillen.authoring.extension_ruleset_missing",
    "dillen.authoring.extension_selection_invalid",
    "dillen.authoring.field_entry_expected",
    "dillen.authoring.field_instruction_operand_conflict",
    "dillen.authoring.field_instruction_payload_conflict",
    "dillen.authoring.field_map_required",
    "dillen.authoring.integrity_failed",
    "dillen.authoring.invoke_capability_payload_invalid",
    "dillen.authoring.invoke_capability_version_invalid",
    "dillen.authoring.lifecycle_state_unknown",
    "dillen.authoring.mechanism_root_expected",
    "dillen.authoring.mechanism_schema_rejected",
    "dillen.authoring.number_required",
    "dillen.authoring.package_content_digest_mismatch",
    "dillen.authoring.package_dependency_role_violation",
    "dillen.authoring.package_entity_reference_violation",
    "dillen.authoring.package_lock_failed",
    "dillen.authoring.package_manifest_rejected",
    "dillen.authoring.package_role_invalid",
    "dillen.authoring.package_role_required",
    "dillen.authoring.package_role_violation",
    "dillen.authoring.package_source_ambiguous",
    "dillen.authoring.package_source_missing",
    "dillen.authoring.package_source_not_selected",
    "dillen.authoring.provides_capabilities_invalid",
    "dillen.authoring.query_kind_unknown",
    "dillen.authoring.read_path_direction_unknown",
    "dillen.authoring.read_path_reduce_unknown",
    "dillen.authoring.read_path_root_invalid",
    "dillen.authoring.read_path_terminal_missing",
    "dillen.authoring.reference_kind_unknown",
    "dillen.authoring.reference_type_invalid",
    "dillen.authoring.relation_definition_rejected",
    "dillen.authoring.relation_schema_rejected",
    "dillen.authoring.required_property_missing",
    "dillen.authoring.role_binding_duplicate",
    "dillen.authoring.role_entry_expected",
    "dillen.authoring.root_assignment_required",
    "dillen.authoring.root_ruleset_missing",
    "dillen.authoring.ruleset_composition_failed",
    "dillen.authoring.ruleset_contract_kind_unknown",
    "dillen.authoring.ruleset_not_composed",
    "dillen.authoring.ruleset_registry_rejected",
    "dillen.authoring.ruleset_root_expected",
    "dillen.authoring.runtime_compile_failed",
    "dillen.authoring.scalar_field_required",
    "dillen.authoring.scalar_required",
    "dillen.authoring.signed_integer_required",
    "dillen.authoring.source_lock_failed",
    "dillen.authoring.spawn_rejected",
    "dillen.authoring.spawn_requirement_expected",
    "dillen.authoring.spawn_root_expected",
    "dillen.authoring.typed_value_invalid",
    "dillen.authoring.unexpected_bare_value",
    "dillen.authoring.unknown_property",
    "dillen.authoring.unsigned_integer_required",
    "dillen.authoring.unterminated_block",
    "dillen.authoring.value_kind_unknown",
    "dillen.authoring.versioned_map_required",
    };
    return codes;
}

// Pulls every "dillen.authoring.*" string literal out of a source file. Crude
// on purpose: a smarter scan would need the parser it is checking.
void ScanCodes(
    const std::filesystem::path& path,
    std::set<std::string>& out,
    bool& ok
)
{
    std::ifstream file(path);
    if (!file)
    {
        std::cerr << "diagnostic contract: cannot open " << path.string()
                  << "\n  (run from the repository root)\n";
        ok = false;
        return;
    }
    const std::string prefix = "\"dillen.authoring.";
    std::string line;
    while (std::getline(file, line))
    {
        std::size_t at = line.find(prefix);
        while (at != std::string::npos)
        {
            const std::size_t start = at + 1;
            const std::size_t end = line.find('"', start);
            if (end == std::string::npos) break;
            out.insert(line.substr(start, end - start));
            at = line.find(prefix, end);
        }
    }
}

}

int main()
{
    using namespace dillen;

    const std::filesystem::path parserRoot =
        "Project-Dillen/src/parser/parsers/dillen";
    std::set<std::string> found;
    bool ok = true;
    ScanCodes(parserRoot / "authoring_parser.cpp", found, ok);
    ScanCodes(parserRoot / "authoring_pipeline.cpp", found, ok);
    if (!ok)
    {
        return 1;
    }

    const std::vector<std::string>& frozen = FrozenCodes();
    const std::set<std::string> expected(frozen.begin(), frozen.end());

    int failures = 0;
    std::vector<std::string> added;
    std::vector<std::string> removed;
    std::set_difference(
        found.begin(), found.end(),
        expected.begin(), expected.end(),
        std::back_inserter(added)
    );
    std::set_difference(
        expected.begin(), expected.end(),
        found.begin(), found.end(),
        std::back_inserter(removed)
    );
    for (const std::string& code : added)
    {
        std::cerr << "diagnostic contract: NEW code not in the frozen list: "
                  << code << '\n';
        ++failures;
    }
    for (const std::string& code : removed)
    {
        std::cerr << "diagnostic contract: code disappeared from the parser: "
                  << code
                  << "\n  Renaming or removing a diagnostic code is a breaking"
                     " change to the\n  authoring surface. Authors and tooling"
                     " key on these strings.\n";
        ++failures;
    }
    if (frozen.size() != expected.size())
    {
        std::cerr << "diagnostic contract: the frozen list has duplicates\n";
        ++failures;
    }
    if (!std::is_sorted(frozen.begin(), frozen.end()))
    {
        std::cerr << "diagnostic contract: keep the frozen list sorted\n";
        ++failures;
    }

    // Behavioural half: feed real malformed sources and require the exact
    // code. These are the ones an author is most likely to meet.
    struct Case
    {
        const char* code;
        const char* source;
    };
    const Case cases[] = {
        {"dillen.authoring.unknown_property",
         "algorithm_descriptor = { name = a.b  version = 1 "
         " backend = declarative  entry_points = { tick }  wat = 3 "
         " program = { tick = { } } }"},
        {"dillen.authoring.algorithm_instruction_unknown",
         "algorithm_descriptor = { name = a.b  version = 1 "
         " backend = declarative  entry_points = { tick } "
         " program = { tick = { no_such_instruction = { field = f } } } }"},
        {"dillen.authoring.read_path_root_invalid",
         "algorithm_descriptor = { name = a.b  version = 1 "
         " backend = declarative  entry_points = { tick } "
         " program = { tick = { set_field = { field = f "
         "   left = { self_field = x  constant = 1 } } } } }"},
        {"dillen.authoring.read_path_reduce_unknown",
         "algorithm_descriptor = { name = a.b  version = 1 "
         " backend = declarative  entry_points = { tick } "
         " program = { tick = { set_field = { field = f "
         "   left = { self_field = x  reduce = average } } } } }"},
        {"dillen.authoring.compare_operator_unknown",
         "algorithm_descriptor = { name = a.b  version = 1 "
         " backend = declarative  entry_points = { tick } "
         " program = { tick = { set_field = { field = f  value = 1 "
         "   when = { compare = { left = { self_field = a }  op = approx "
         "     right = { self_field = b } } } } } } }"},
        {"dillen.authoring.binary_operator_unknown",
         "algorithm_descriptor = { name = a.b  version = 1 "
         " backend = declarative  entry_points = { tick } "
         " program = { tick = { set_field = { field = f  op = pow "
         "   left = { self_field = a }  right = { self_field = b } } } } }"},
        {"dillen.authoring.field_instruction_operand_conflict",
         "algorithm_descriptor = { name = a.b  version = 1 "
         " backend = declarative  entry_points = { tick } "
         " program = { tick = { set_field = { field = f  value = 1 "
         "   left = { self_field = a } } } } }"},
        {"dillen.authoring.invoke_capability_payload_invalid",
         "algorithm_descriptor = { name = a.b  version = 1 "
         " backend = declarative  entry_points = { tick } "
         " program = { tick = { invoke_capability = { "
         " capability = a.signal  delay = 1  payload = 1 "
         " payload_from = { self_field = counter } } } } }"},
        {"dillen.authoring.algorithm_root_expected",
         "mechanism_template = { name = a.b  version = 1 }"},
        // Neither addressing mode given: the instruction says which Component
        // and field but never which Entity.
        {"dillen.authoring.component_owner_ambiguous",
         "algorithm_descriptor = { name = a.b  version = 1 "
         " backend = declarative  entry_points = { tick } "
         " program = { tick = { set_component_field = { "
         "   component = a.stock  field = ore  value = 1 } } } }"},
        // Both given: a role AND a named owner.
        {"dillen.authoring.component_owner_ambiguous",
         "algorithm_descriptor = { name = a.b  version = 1 "
         " backend = declarative  entry_points = { tick } "
         " program = { tick = { set_component_field = { role = home "
         "   owner_entity_type = a.place  owner_definition = a.site "
         "   component = a.stock  field = ore  value = 1 } } } }"},
        // The same role slot bound twice. std::map::emplace keeps the first
        // and drops the second, so this used to be silently accepted with the
        // author's second binding thrown away.
        {"dillen.authoring.role_binding_duplicate",
         "mechanism_spawn = { name = a.spawn  mechanism = a.mech "
         " definition = a.def  count = 1  roles = { "
         "   home = { entity = { entity_type = a.place "
         "     definition = a.first } } "
         "   home = { entity = { entity_type = a.place "
         "     definition = a.second } } } }"},
    };

    for (const Case& probe : cases)
    {
        // Cases are not all algorithms any more, and a Spawn parsed by the
        // algorithm parser would fail on its root keyword and never reach the
        // diagnostic under test.
        const bool isSpawn =
            std::string_view(probe.source).substr(0, 15) == "mechanism_spawn";
        parser::SourceBuffer source(
            1,
            isSpawn
                ? "diagnostics/case.dspawn"
                : "diagnostics/case.dalgorithm",
            {},
            probe.source,
            parser::SourceEncoding::Utf8
        );
        parser::DiagnosticBag diagnostics;
        parser::ParserCursor cursor(source, diagnostics);
        parser::ParseArtifact artifact;
        const bool parsed = isSpawn
            ? authoring::ParseMechanismSpawn(cursor, artifact)
            : authoring::ParseAlgorithmDescriptor(cursor, artifact);
        bool sawCode = false;
        for (const parser::Diagnostic& diagnostic : diagnostics.All())
        {
            if (diagnostic.code == probe.code)
            {
                sawCode = true;
            }
        }
        if (parsed && !sawCode)
        {
            std::cerr << "diagnostic contract: source parsed cleanly but "
                      << probe.code << " was expected\n";
            ++failures;
        }
        else if (!sawCode)
        {
            std::cerr << "diagnostic contract: expected " << probe.code
                      << " but got:";
            for (const parser::Diagnostic& diagnostic : diagnostics.All())
            {
                std::cerr << ' ' << diagnostic.code;
            }
            std::cerr << '\n';
            ++failures;
        }
    }

    if (failures != 0)
    {
        std::cerr << "diagnostic contract: " << failures << " failure(s)\n";
        return 2;
    }

    std::cout << "Authoring diagnostic contract: passed ("
              << frozen.size() << " frozen codes, "
              << (sizeof(cases) / sizeof(cases[0]))
              << " triggered end to end)\n";
    return 0;
}

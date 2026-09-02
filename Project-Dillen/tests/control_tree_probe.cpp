#include <cstdint>
#include <filesystem>
#include <system_error>
#include <algorithm>
#include <iterator>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "control_tree.hpp"
#include "package_content_digest.hpp"
#include "map_command.hpp"
#include "map_entity_index.hpp"
#include "mechanism_panel.hpp"
#include "presentation_compiler.hpp"
#include "presentation_schema.hpp"
#include "presentation_view.hpp"
#include "standalone_session.hpp"

// Demo 0.8 R3 -- the Presentation Schema, the Compiler, and the layout that
// runs on what they produce. All of it headless.
//
// This probe used to test an INTERPRETER: it loaded a tree of strings out of an
// asset and asserted the layout came out right. A review pointed out that
// running on the authoring form at runtime is exactly what this project refuses
// everywhere else -- Mechanisms, Components, Relations and Algorithms all go
// Source -> Registry -> Compile -> Frozen Catalog and the Runtime never sees a
// name -- and the pipeline now has the same shape:
//
//     Presentation Source -> Schema Registry -> Compiler
//                         -> Frozen Presentation Catalog -> ControlTree
//
// The assertions divide the same way. The compiler section proves every name is
// spent at load; the layout section proves the arithmetic on what is left.

namespace
{
namespace fs = std::filesystem;
using namespace dillen;

const fs::path kGameRoot = "Dillen-Game";
const fs::path kMapContractsRoot = kGameRoot / "map/contracts";
const fs::path kMapMechanismRoot = kGameRoot / "production/map_world";
const fs::path kMapWorldRoot = kGameRoot / "map/world";
const fs::path kMapPresentationRoot = kGameRoot / "presentation/map_world";

int failures = 0;

void Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "control tree: " << what << '\n';
        ++failures;
    }
}

// Every leaf rect must sit inside its parent, and siblings must not overlap.
// A layout that violates either is one where a click can land in a control the
// player is not looking at.
void CheckContainment(
    const presentation::ControlTree& tree,
    std::uint32_t index
)
{
    const presentation::CompiledControl& parent = tree.View().controls[index];
    const presentation::ControlRect box = tree.Rect(index);
    for (std::uint32_t step = 0; step < parent.childCount; ++step)
    {
        const std::uint32_t child = parent.firstChild + step;
        const presentation::ControlRect rect = tree.Rect(child);
        Check(rect.x >= box.x
                && rect.y >= box.y
                && rect.x + rect.width <= box.x + box.width
                && rect.y + rect.height <= box.y + box.height,
            "control " + std::to_string(child) + " escapes its parent");
        for (std::uint32_t other = step + 1;
            other < parent.childCount;
            ++other)
        {
            const presentation::ControlRect against =
                tree.Rect(parent.firstChild + other);
            const bool disjoint =
                rect.x + rect.width <= against.x
                || against.x + against.width <= rect.x
                || rect.y + rect.height <= against.y
                || against.y + against.height <= rect.y;
            Check(disjoint,
                "controls " + std::to_string(child) + " and "
                    + std::to_string(parent.firstChild + other)
                    + " overlap");
        }
        CheckContainment(tree, child);
    }
}

// Finds the first node with `key` anywhere in an asset's content tree. Used
// only to injure a copy before recompiling it: addressing by index would make
// these refusal cases break whenever the Package's layout is edited.
kernel::PresentationAssetNode* FindNode(
    std::vector<kernel::PresentationAssetNode>& nodes,
    const std::string& key
)
{
    for (kernel::PresentationAssetNode& node : nodes)
    {
        if (node.key == key)
        {
            return &node;
        }
        if (kernel::PresentationAssetNode* found = FindNode(node.children, key))
        {
            return found;
        }
    }
    return nullptr;
}

host::StandaloneSessionConfig WorldConfig()
{
    host::StandaloneSessionConfig config;
    config.sources.push_back({
        "world_map_contracts", kMapContractsRoot, 0, {}, {}, {}
    });
    config.sources.push_back({
        "world_map_mechanisms", kMapMechanismRoot, 50, {}, {}, {}
    });
    config.sources.push_back({
        "world_map_content", kMapWorldRoot, 100, {}, {}, {}
    });
    config.sources.push_back({
        "world_map_presentation", kMapPresentationRoot, 200, {}, {}, {}
    });
    config.rulesets.root = {
        kernel::StableRulesetId("dillen.map.world_root"),
        "dillen.map.world_root",
        1
    };
    config.rulesets.requireExplicitPackageRoles = true;
    return config;
}

}

int main()
{
    host::StandaloneSession session;
    host::StandaloneSessionReport report;
    if (!session.Start(WorldConfig(), report))
    {
        for (const std::string& diagnostic : report.diagnostics)
        {
            std::cerr << "  " << diagnostic << '\n';
        }
        std::cerr << "control tree: the world did not load\n";
        return 1;
    }

    // --- the Schema Registry ----------------------------------------------
    //
    // A vocabulary, with types, defaults and freeze semantics. It is code
    // rather than a DSL source, and the reason is in presentation_schema.hpp:
    // every control kind needs layout code, so a data schema could only ever
    // declare a subset of what the engine implements -- a second copy of one
    // semantics.
    presentation::PresentationSchemaRegistry schema;
    Check(presentation::RegisterBuiltinControls(schema),
        "the built-in control vocabulary does not match its slot constants");
    Check(schema.IsFrozen() && schema.Size() == 4,
        "the schema registry is not four frozen kinds");
    {
        std::string message;
        presentation::ControlKindSchema extra;
        extra.name = "panel";
        Check(schema.Add(extra, message)
                == presentation::PresentationSchemaStatus::Frozen,
            "a frozen registry accepted another kind");
    }
    {
        presentation::PresentationSchemaRegistry open;
        std::string message;
        presentation::ControlKindSchema kind;
        kind.name = "widget";
        kind.properties.push_back({"a", presentation::ControlPropertyKind::Text,
            false, {}});
        kind.properties.push_back({"a", presentation::ControlPropertyKind::Text,
            false, {}});
        Check(open.Add(kind, message)
                == presentation::PresentationSchemaStatus::DuplicateProperty,
            "a kind with a property declared twice was accepted");
        presentation::ControlKindSchema first;
        first.name = "widget";
        Check(open.Add(first, message)
                == presentation::PresentationSchemaStatus::Ok,
            "a well-formed kind was refused");
        presentation::ControlKindSchema again;
        again.name = "widget";
        Check(open.Add(again, message)
                == presentation::PresentationSchemaStatus::DuplicateKind,
            "the same kind was accepted twice");
    }

    // --- the Compiler ------------------------------------------------------
    std::string message;
    presentation::PresentationCompiler compiler;
    presentation::FrozenPresentationCatalog catalog;
    if (compiler.Compile(
            schema,
            session.PresentationAssets(),
            session.Catalog(),
            catalog,
            message)
        != presentation::PresentationCompileStatus::Ok)
    {
        std::cerr << "control tree: compile failed: " << message << '\n';
        return 2;
    }
    Check(catalog.IsFrozen() && catalog.ViewCount() == 1,
        "the Package should compile to exactly one view");
    Check(catalog.RulesetFingerprint() == session.Catalog().Fingerprint(),
        "the catalog did not record the Ruleset it compiled against");
    Check(static_cast<bool>(catalog.SourceFingerprint()),
        "the catalog did not record a source fingerprint");
    Check(catalog.FindAsset("map_index_raster") != nullptr
            && catalog.FindAsset("font") != nullptr,
        "payload assets are not addressable through the catalog");

    const presentation::CompiledPresentationView* view =
        catalog.FindView("dillen.map.world_raster_panel");
    Check(view != nullptr, "the panel view is not in the catalog");
    if (view == nullptr)
    {
        std::cerr << "control tree: no view to test\n";
        return 3;
    }
    Check(view->controls.size() == 8,
        "the Package declares a different number of controls than expected");

    // Every name is spent. The fields the layout binds are resolved slots, and
    // they are the SAME slots the Ruleset gives -- which is the whole claim of
    // compiling: after this nothing looks a field up by name at runtime.
    Check(view->boundFieldNames.size() == 2
            && view->boundFieldNames[0] == "level"
            && view->boundFieldNames[1] == "output",
        "the view binds different fields than the Package declares");
    Check(static_cast<bool>(view->definition),
        "the view did not resolve a Definition");
    for (std::size_t index = 0; index < view->boundFieldSlots.size(); ++index)
    {
        const auto slot = session.Catalog().ResolveDefinitionFieldSlot(
            view->definition,
            view->boundFieldNames[index]
        );
        Check(slot && *slot == view->boundFieldSlots[index],
            "field '" + view->boundFieldNames[index]
                + "' compiled to a slot the Ruleset does not agree with");
    }
    // The tree is flat and depth-first: a parent's children are contiguous and
    // strictly after it. A walk over the array is then a walk over the tree.
    for (std::uint32_t index = 0; index < view->controls.size(); ++index)
    {
        const presentation::CompiledControl& control = view->controls[index];
        Check(control.childCount == 0 || control.firstChild > index,
            "control " + std::to_string(index)
                + " has children that are not after it");
        Check(control.firstChild + control.childCount
                <= view->controls.size(),
            "control " + std::to_string(index)
                + " names children outside the array");
        Check(control.firstValue + control.valueCount <= view->values.size(),
            "control " + std::to_string(index)
                + " names values outside the array");
    }

    // --- what the Compiler refuses -----------------------------------------
    //
    // Each of these used to be either a runtime `if` in the interpreter or,
    // worse, nothing at all. They are now load-time refusals with the asset,
    // the control and the property named.
    const auto recompile = [&](
        void (*injure)(kernel::PresentationAsset&),
        presentation::PresentationCompileStatus expected,
        const std::string& what)
    {
        std::vector<kernel::PresentationAsset> assets =
            session.PresentationAssets();
        for (kernel::PresentationAsset& asset : assets)
        {
            if (asset.kind == "ui_binding")
            {
                injure(asset);
            }
        }
        presentation::FrozenPresentationCatalog refused;
        std::string reason;
        const presentation::PresentationCompileStatus status =
            compiler.Compile(
                schema, assets, session.Catalog(), refused, reason);
        Check(status == expected, what);
        Check(!refused.IsFrozen() || status
                == presentation::PresentationCompileStatus::Ok,
            "a refused compile still produced a frozen catalog");
    };

    recompile(
        [](kernel::PresentationAsset& asset)
        {
            FindNode(asset.content, "label")->key = "labell";
        },
        presentation::PresentationCompileStatus::ControlKindUnknown,
        "a misspelled control kind was accepted");
    recompile(
        [](kernel::PresentationAsset& asset)
        {
            asset.content.front().key = "pannel";
        },
        presentation::PresentationCompileStatus::ControlKindUnknown,
        "a view whose root is not a control kind was accepted");
    recompile(
        [](kernel::PresentationAsset& asset)
        {
            FindNode(asset.content, "padding")->key = "padding_";
        },
        presentation::PresentationCompileStatus::ControlPropertyInvalid,
        "a misspelled property was accepted");
    recompile(
        [](kernel::PresentationAsset& asset)
        {
            FindNode(asset.content, "padding")->value = "eight";
        },
        presentation::PresentationCompileStatus::ControlPropertyInvalid,
        "a padding that is not a number was accepted");
    recompile(
        [](kernel::PresentationAsset& asset)
        {
            FindNode(asset.content, "size")->value = "huge";
        },
        presentation::PresentationCompileStatus::ControlPropertyInvalid,
        "a size that is neither pixels nor fill was accepted");
    recompile(
        [](kernel::PresentationAsset& asset)
        {
            FindNode(asset.content, "axis")->value = "sideways";
        },
        presentation::PresentationCompileStatus::ControlPropertyInvalid,
        "an axis that is not an axis was accepted");
    recompile(
        [](kernel::PresentationAsset& asset)
        {
            FindNode(asset.content, "background")->value = "maybe";
        },
        presentation::PresentationCompileStatus::ControlPropertyInvalid,
        "a boolean that is neither yes nor no was accepted");
    recompile(
        [](kernel::PresentationAsset& asset)
        {
            // A `value` control with no field at all.
            FindNode(asset.content, "field")->key = "fields";
        },
        presentation::PresentationCompileStatus::ControlPropertyInvalid,
        "a value control with no field was accepted");
    recompile(
        [](kernel::PresentationAsset& asset)
        {
            FindNode(asset.content, "field")->value = "throughput";
        },
        presentation::PresentationCompileStatus::FieldUnresolved,
        "a field no one has heard of was accepted");
    recompile(
        [](kernel::PresentationAsset& asset)
        {
            // The check that makes the requires block LOAD-BEARING, and the
            // only one that does. `output` is a field the Ruleset really has;
            // what is removed is the Package's declaration that it reads it.
            //
            // The first version of this case bound a field that did not exist
            // in the Ruleset either, so it passed on the Ruleset lookup and
            // said nothing about the requires block at all -- an injection
            // that loosened the requires match left it green.
            for (std::size_t index = 0; index < asset.requirements.size();
                ++index)
            {
                if (asset.requirements[index].fieldName == "output")
                {
                    asset.requirements.erase(
                        asset.requirements.begin()
                            + static_cast<std::ptrdiff_t>(index));
                    break;
                }
            }
        },
        presentation::PresentationCompileStatus::FieldUnresolved,
        "a field the Package never declared it reads was accepted");
    recompile(
        [](kernel::PresentationAsset& asset)
        {
            // A field the Package declares but the Ruleset does not have.
            for (kernel::PresentationAssetRequirement& requirement
                : asset.requirements)
            {
                if (requirement.fieldName == "level")
                {
                    requirement.fieldName = "throughput";
                }
            }
            FindNode(asset.content, "field")->value = "throughput";
        },
        presentation::PresentationCompileStatus::FieldUnresolved,
        "a field the Ruleset does not provide was accepted");
    // --- the action is a contract, not a name -------------------------
    recompile(
        [](kernel::PresentationAsset& asset)
        {
            // An operation the contract does not declare. Without this check
            // a Package could name any verb it liked and the contract would
            // be decoration.
            FindNode(asset.content, "operation")->value = "demolish";
        },
        presentation::PresentationCompileStatus::CapabilityUnresolved,
        "an operation the contract does not declare was accepted");
    recompile(
        [](kernel::PresentationAsset& asset)
        {
            // The check that makes the requires block load-bearing for
            // capabilities. The contract the buttons name stays exactly as it
            // is -- the Ruleset really does publish it -- and what is removed
            // is the Package's declaration that it uses it. Naming a contract
            // nobody has heard of would fail on the Ruleset lookup instead and
            // say nothing about `requires` at all.
            for (std::size_t index = 0; index < asset.requirements.size();
                ++index)
            {
                if (asset.requirements[index].kind
                    == kernel::PresentationAssetRequirement::Kind::Capability)
                {
                    asset.requirements.erase(
                        asset.requirements.begin()
                            + static_cast<std::ptrdiff_t>(index));
                    break;
                }
            }
        },
        presentation::PresentationCompileStatus::CapabilityUnresolved,
        "a capability the Package never declared it uses was accepted");
    recompile(
        [](kernel::PresentationAsset& asset)
        {
            FindNode(asset.content, "capability")->value =
                "dillen.map.undeclared";
        },
        presentation::PresentationCompileStatus::CapabilityUnresolved,
        "a capability no one has heard of was accepted");
    recompile(
        [](kernel::PresentationAsset& asset)
        {
            // Declared by the Package, but the Ruleset publishes no such
            // contract. Both halves have to hold.
            for (kernel::PresentationAssetRequirement& requirement
                : asset.requirements)
            {
                if (requirement.kind
                    == kernel::PresentationAssetRequirement::Kind::Capability)
                {
                    requirement.primaryName = "dillen.map.unpublished";
                }
            }
            FindNode(asset.content, "capability")->value =
                "dillen.map.unpublished";
        },
        presentation::PresentationCompileStatus::CapabilityUnresolved,
        "a capability the Ruleset does not publish was accepted");
    recompile(
        [](kernel::PresentationAsset& asset)
        {
            kernel::PresentationAssetNode child;
            child.key = "label";
            child.block = true;
            FindNode(asset.content, "label")->children.push_back(child);
        },
        presentation::PresentationCompileStatus::ChildrenNotAllowed,
        "a label with children was accepted");
    recompile(
        [](kernel::PresentationAsset& asset)
        {
            asset.content.push_back(asset.content.front());
        },
        presentation::PresentationCompileStatus::ViewRootInvalid,
        "a view with two roots was accepted");
    {
        presentation::PresentationSchemaRegistry open;
        presentation::FrozenPresentationCatalog refused;
        std::string reason;
        Check(compiler.Compile(
                  open,
                  session.PresentationAssets(),
                  session.Catalog(),
                  refused,
                  reason)
                == presentation::PresentationCompileStatus::SchemaNotFrozen,
            "compiling against an unfrozen schema was allowed");
    }

    // --- a compiled catalog belongs to one Ruleset -------------------------
    //
    // Field slots are indices into a specific Ruleset's layouts. Bound to
    // another world they would read a real number belonging to something else,
    // which is worse than reading nothing -- so the fingerprint is checked
    // rather than assumed. The second world here is the Demo 1.0 fixture,
    // which is a genuinely different Ruleset rather than a doctored copy.
    presentation::ControlTree tree;
    {
        kernel::FrozenRuntimeCatalog empty;
        Check(tree.Bind(catalog, view->id, empty, message)
                == presentation::ControlTreeStatus::RulesetMismatch,
            "binding before a world was loaded was allowed");

        host::StandaloneSessionConfig other;
        other.sources.push_back({
            "demo1_contracts",
            "Project-Dillen/demo/dillen_demo_1_0/packages/contracts",
            0, {}, {}, {}
        });
        other.sources.push_back({
            "demo1_settlement",
            "Project-Dillen/demo/dillen_demo_1_0/packages/settlement_growth",
            10, {}, {}, {}
        });
        other.sources.push_back({
            "demo1_trade",
            "Project-Dillen/demo/dillen_demo_1_0/packages/trade_cycle",
            20, {}, {}, {}
        });
        other.sources.push_back({
            "demo1_ruleset",
            "Project-Dillen/demo/dillen_demo_1_0/rulesets/balanced",
            100, {}, {}, {}
        });
        other.rulesets.root = {
            kernel::StableRulesetId("dillen.demo1.balanced_root"),
            "dillen.demo1.balanced_root",
            1
        };
        host::StandaloneSession stranger;
        host::StandaloneSessionReport strangerReport;
        if (stranger.Start(other, strangerReport))
        {
            Check(stranger.Catalog().Fingerprint()
                    != session.Catalog().Fingerprint(),
                "the second world is not actually a different Ruleset");
            Check(tree.Bind(catalog, view->id, stranger.Catalog(), message)
                    == presentation::ControlTreeStatus::RulesetMismatch,
                "a catalog compiled elsewhere bound to this world");
        }
        else
        {
            Check(false, "the Demo 1.0 fixture world did not load");
        }
    }

    Check(tree.Bind(catalog, view->id, session.Catalog(), message)
            == presentation::ControlTreeStatus::Ok,
        "the view did not bind: " + message);
    Check(tree.Bind(
              catalog,
              presentation::StablePresentationViewId("nothing.here"),
              session.Catalog(),
              message)
            == presentation::ControlTreeStatus::ViewMissing,
        "a view the catalog does not have was bound");
    if (tree.Bind(catalog, view->id, session.Catalog(), message)
        != presentation::ControlTreeStatus::Ok)
    {
        return 4;
    }

    // --- the layout is exact -----------------------------------------------
    //
    // Four viewport sizes, and the odd ones are the point. An even viewport
    // divides evenly among two fill children and would pass whether or not the
    // remainder rule exists; an odd one is where a proportional split loses a
    // pixel and the panel stops being exactly covered.
    const std::uint32_t spacer = tree.Find("spacer");
    const std::uint32_t actions = tree.Find("actions");
    const std::uint32_t raise = tree.Find("raise");
    const std::uint32_t lower = tree.Find("lower");
    const std::uint32_t title = tree.Find("title");
    Check(spacer != UINT32_MAX && actions != UINT32_MAX
            && raise != UINT32_MAX && lower != UINT32_MAX
            && title != UINT32_MAX,
        "the layout no longer has the controls this probe addresses");
    Check(tree.Find("no_such_control") == UINT32_MAX,
        "a control that does not exist was found");

    for (const std::int32_t width : {200, 201})
    for (const std::int32_t height : {300, 301})
    {
        tree.Layout(width, height);
        CheckContainment(tree, 0);
        Check(tree.Rect(spacer).y + tree.Rect(spacer).height == height - 8,
            "the fill child does not reach the bottom padding at "
                + std::to_string(width) + "x" + std::to_string(height));
        Check(tree.Rect(raise).x == tree.Rect(actions).x
                && tree.Rect(lower).x + tree.Rect(lower).width
                    == tree.Rect(actions).x + tree.Rect(actions).width,
            "the buttons do not cover their row at "
                + std::to_string(width) + "x" + std::to_string(height));
        Check(tree.Rect(lower).x
                == tree.Rect(raise).x + tree.Rect(raise).width + 4,
            "the gap between the buttons is not the declared 4");
    }

    // --- a click lands where it looks like it lands ------------------------
    tree.Layout(200, 300);
    Check(tree.HitTest(
              tree.Rect(raise).x + tree.Rect(raise).width / 2,
              tree.Rect(raise).y + tree.Rect(raise).height / 2) == raise,
        "the centre of the raise button did not hit it");
    {
        const std::uint32_t edge = tree.HitTest(
            tree.Rect(raise).x + tree.Rect(raise).width,
            tree.Rect(raise).y + tree.Rect(raise).height / 2
        );
        Check(edge != raise && edge != lower,
            "a pixel in the gap was claimed by a button");
    }
    Check(tree.HitTest(-1, 10) == UINT32_MAX
            && tree.HitTest(10, 100000) == UINT32_MAX,
        "a point outside the viewport hit a control");

    // --- what paints is declared, not inferred -----------------------------
    //
    // The overlay used to paint whichever panel happened to be called
    // "province_panel", so renaming the root in a Package silently lost the
    // background. It is now the `background` property, and this is the gate:
    // rename every id in the layout and the surfaces must survive.
    {
        presentation::MechanismPanelReadout blank;
        std::size_t surfaces = 0;
        for (const presentation::ControlDraw& draw : tree.Draw(blank))
        {
            if (draw.background)
            {
                ++surfaces;
            }
        }
        Check(surfaces == 3,
            "expected three declared surfaces, got "
                + std::to_string(surfaces));

        std::vector<kernel::PresentationAsset> renamed =
            session.PresentationAssets();
        for (kernel::PresentationAsset& asset : renamed)
        {
            if (asset.kind != "ui_binding")
            {
                continue;
            }
            // Every id in the layout, changed.
            std::vector<kernel::PresentationAssetNode*> pending;
            for (kernel::PresentationAssetNode& node : asset.content)
            {
                pending.push_back(&node);
            }
            while (!pending.empty())
            {
                kernel::PresentationAssetNode* node = pending.back();
                pending.pop_back();
                if (node->key == "id")
                {
                    node->value = "renamed_" + node->value;
                }
                for (kernel::PresentationAssetNode& child : node->children)
                {
                    pending.push_back(&child);
                }
            }
        }
        presentation::FrozenPresentationCatalog other;
        std::string reason;
        Check(compiler.Compile(
                  schema, renamed, session.Catalog(), other, reason)
                == presentation::PresentationCompileStatus::Ok,
            "the renamed layout did not compile: " + reason);
        presentation::ControlTree renamedTree;
        if (renamedTree.Bind(
                other,
                view->id,
                session.Catalog(),
                reason)
            == presentation::ControlTreeStatus::Ok)
        {
            renamedTree.Layout(200, 300);
            std::size_t renamedSurfaces = 0;
            for (const presentation::ControlDraw& draw
                : renamedTree.Draw(presentation::MechanismPanelReadout{}))
            {
                if (draw.background)
                {
                    ++renamedSurfaces;
                }
            }
            Check(renamedSurfaces == surfaces,
                "renaming every control changed which ones paint");
        }
        else
        {
            Check(false, "the renamed layout did not bind");
        }
    }

    // --- the panel binds to what the view compiled -------------------------
    // The spec is now two fields, and both come from the compiled view. The
    // mechanism, the field names, the entity type, the province count and the
    // naming convention have all gone: every one of them was the host
    // asserting something about a Package it should have been reading.
    presentation::MapCommandSpec commandSpec;
    commandSpec.definition = tree.Definition();
    commandSpec.roleName = "province";

    presentation::MapCommandTranslator translator;
    if (translator.Bind(session.Catalog(), commandSpec, message)
        != presentation::MapCommandStatus::Ok)
    {
        std::cerr << "control tree: translator bind failed: " << message
                  << '\n';
        return 5;
    }
    presentation::PresentationView reader;
    reader.Advance(
        std::make_shared<const runtime::WorldQuerySnapshot>(
            session.Runtime().Query()
        )
    );
    translator.Resolve(reader);

    // Raster index -> Entity, assembled from data: the id table that ships
    // with the raster, and the source_id the world carries.
    presentation::MapEntityIndex entities;
    const kernel::PresentationAsset* idAsset =
        catalog.FindAsset("map_province_ids");
    Check(idAsset != nullptr, "the Package ships no province id table");
    if (idAsset == nullptr
        || entities.Bind(session.Catalog(), *idAsset, message)
            != presentation::MapEntityIndexStatus::Ok
        || entities.Resolve(reader)
            != presentation::MapEntityIndexStatus::Ok)
    {
        std::cerr << "control tree: the entity index failed: " << message
                  << '\n';
        return 5;
    }
    Check(entities.Resolved() == entities.Count(),
        "only " + std::to_string(entities.Resolved()) + " of "
            + std::to_string(entities.Count())
            + " raster indices resolved to an Entity");

    presentation::MechanismPanel panel;
    Check(panel.Bind(
              session.Catalog(),
              tree.Definition(),
              tree.BoundFields(),
              view->boundFieldSlots,
              message)
            == presentation::MechanismPanelStatus::Ok,
        "the panel did not bind to what the view compiled: " + message);

    // --- what a button produces --------------------------------------------
    const std::uint32_t province = 7;
    const kernel::EntityId entity = entities.EntityFor(province);
    Check(static_cast<bool>(entity),
        "raster index 7 resolved to no Entity");
    // The index is a position in a picture and the Entity is an identity.
    // Round-tripping one through the other is the property that used to be a
    // naming convention.
    Check(entities.IndexFor(entity) == province,
        "the entity index does not round-trip");

    // --- the correspondence really is data ---------------------------
    //
    // This world names its regions `dillen.map.region_<index>`, so an
    // implementation that reconstructed the Entity from the index would agree
    // with the data on every province -- and every assertion above would pass
    // while the naming convention was still doing the work. The only way to
    // tell them apart is to make the convention FALSE and see which answer
    // follows: two entries of the id table are swapped, and the Entities the
    // two indices resolve to must swap with them.
    {
        const kernel::PresentationAsset* source =
            catalog.FindAsset("map_province_ids");
        const fs::path payload =
            fs::path(source->source.physicalDirectory) / source->assetPath;
        std::string bytes;
        {
            std::ifstream stream(payload, std::ios::binary);
            bytes.assign(
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>()
            );
        }
        const std::uint32_t left = 11;
        const std::uint32_t right = 4096;
        const kernel::EntityId beforeLeft = entities.EntityFor(left);
        const kernel::EntityId beforeRight = entities.EntityFor(right);
        for (int byte = 0; byte < 4; ++byte)
        {
            std::swap(
                bytes[static_cast<std::size_t>(left) * 4u + byte],
                bytes[static_cast<std::size_t>(right) * 4u + byte]
            );
        }
        const fs::path swapped =
            fs::temp_directory_path() / "dillen_swapped_ids.bin";
        {
            std::ofstream stream(swapped, std::ios::binary | std::ios::trunc);
            stream.write(
                bytes.data(),
                static_cast<std::streamsize>(bytes.size())
            );
        }
        kernel::PresentationAsset altered = *source;
        altered.source.physicalDirectory = swapped.parent_path().string();
        altered.assetPath = swapped.filename().string();
        altered.assetDigest = kernel::ComputeContentDigest(bytes);

        presentation::MapEntityIndex shuffled;
        Check(shuffled.Bind(session.Catalog(), altered, message)
                == presentation::MapEntityIndexStatus::Ok
            && shuffled.Resolve(reader)
                == presentation::MapEntityIndexStatus::Ok,
            "the swapped id table did not load: " + message);
        Check(shuffled.EntityFor(left) == beforeRight
                && shuffled.EntityFor(right) == beforeLeft,
            "swapping two entries of the id table did not swap the Entities "
            "-- the index is still being turned into a name");
        std::error_code removing;
        fs::remove(swapped, removing);
    }

    presentation::MapIntent intent;
    Check(tree.IntentFor(raise, entity, intent)
            && intent.entity == entity
            && intent.delta == 1,
        "the raise button's intent is not what the Package declared");
    // The action is a public contract, not a verb this layer knows.
    Check(static_cast<bool>(intent.capability)
            && intent.capability
                == kernel::StableCapabilityId("dillen.map.site_development")
            && intent.capabilityVersion == 1
            && static_cast<bool>(intent.field),
        "the button's intent does not name a resolved Capability Contract");
    {
        presentation::MapIntent down;
        Check(tree.IntentFor(lower, entity, down) && down.delta == -1,
            "the lower button's intent is not what the Package declared");
    }
    {
        presentation::MapIntent none;
        Check(!tree.IntentFor(title, entity, none),
            "a label produced an intent");
    }
    {
        presentation::MapIntent orphan;
        Check(!tree.IntentFor(raise, kernel::EntityId{}, orphan),
            "a button with nothing selected produced an intent");
    }

    // The contract check, from the other side: an intent naming a capability
    // the Definition does not provide must be refused even though every name
    // in it resolves.
    {
        presentation::MapIntent forged = intent;
        forged.capability =
            kernel::StableCapabilityId("dillen.map.not_provided");
        kernel::WorldTransaction refused;
        Check(translator.Translate(forged, refused)
                == presentation::MapCommandStatus::CapabilityNotProvided,
            "a capability the Definition does not provide was commanded");
        forged = intent;
        forged.capabilityVersion = 2;
        Check(translator.Translate(forged, refused)
                == presentation::MapCommandStatus::CapabilityNotProvided,
            "a capability version the Definition does not provide was "
            "commanded");
    }

    const presentation::MechanismPanelReadout opening =
        panel.Read(translator, reader, entity);
    if (!opening.valid || opening.fields.size() != 2)
    {
        std::cerr << "control tree: province " << province
                  << " has no panel to read" << '\n';
        return 6;
    }
    const std::int64_t levelBefore = opening.fields[0].value;

    kernel::WorldTransaction transaction;
    Check(translator.Translate(intent, transaction)
            == presentation::MapCommandStatus::Ok,
        "the button's intent did not translate");
    session.Runtime().Enqueue(std::move(transaction), 1, 0);
    Check(static_cast<bool>(session.Runtime().RunTick(1)),
        "the tick after the button failed");
    reader.Advance(
        std::make_shared<const runtime::WorldQuerySnapshot>(
            session.Runtime().Query()
        )
    );

    const presentation::MechanismPanelReadout readout =
        panel.Read(translator, reader, entity);
    Check(readout.valid && readout.fields.size() == 2
            && readout.fields[0].value == levelBefore + 1,
        "the button did not move the authoritative level");

    // The value rows match their field by SLOT, not by label. Two resolved
    // slots cannot collide the way two strings that look alike can.
    const std::vector<presentation::ControlDraw> draws = tree.Draw(readout);
    std::size_t texts = 0;
    std::string levelRow;
    std::string outputRow;
    for (const presentation::ControlDraw& draw : draws)
    {
        if (draw.text.empty())
        {
            continue;
        }
        ++texts;
        if (draw.text.rfind("Level: ", 0) == 0)
        {
            levelRow = draw.text;
        }
        if (draw.text.rfind("Output: ", 0) == 0)
        {
            outputRow = draw.text;
        }
    }
    Check(texts == 5, "a different number of controls draw text than expected");
    Check(levelRow == "Level: " + std::to_string(levelBefore + 1),
        "the level row does not show the authoritative level");
    Check(outputRow.find('.') != std::string::npos
            && outputRow.size() - outputRow.find('.') == 5,
        "the output row is not formatted at four decimal places");

    // An unselected province draws the same controls with no numbers: an empty
    // panel, not a missing one.
    std::size_t blankTexts = 0;
    for (const presentation::ControlDraw& draw
        : tree.Draw(presentation::MechanismPanelReadout{}))
    {
        if (!draw.text.empty())
        {
            ++blankTexts;
        }
    }
    Check(blankTexts == texts,
        "an invalid readout changed how many controls draw text");

    if (failures != 0)
    {
        std::cerr << "control tree: " << failures << " failure(s)\n";
        return 7;
    }
    std::cout << "Control tree: passed (" << schema.Size()
              << " schema kinds, " << view->controls.size()
              << " controls compiled to slots, 17 compile refusals, entity-addressed, "
              << "exact layout at four sizes, a click through to the "
              << "authoritative world)\n";
    return 0;
}

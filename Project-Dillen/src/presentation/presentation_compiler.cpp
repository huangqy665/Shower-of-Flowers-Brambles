#include "presentation_compiler.hpp"

#include <charconv>
#include <string_view>

namespace dillen::presentation {

namespace {

struct Failure
{
    PresentationCompileStatus status = PresentationCompileStatus::Ok;
    std::string message;
};

bool ReadInt64(std::string_view text, std::int64_t& output)
{
    if (text.empty())
    {
        return false;
    }
    const char* begin = text.data();
    const char* end = begin + text.size();
    std::int64_t value = 0;
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc() || result.ptr != end)
    {
        return false;
    }
    output = value;
    return true;
}

const kernel::PresentationAssetNode* FindChild(
    const kernel::PresentationAssetNode& node,
    std::string_view key
)
{
    for (const kernel::PresentationAssetNode& child : node.children)
    {
        if (child.key == key)
        {
            return &child;
        }
    }
    return nullptr;
}

// Turns one authored scalar into a typed value. The default goes through the
// same reader, so a default the schema itself would reject cannot slip in.
bool ReadValue(
    const ControlPropertySchema& property,
    std::string_view text,
    CompiledControlValue& output,
    std::string& message
)
{
    output.kind = property.kind;
    switch (property.kind)
    {
    case ControlPropertyKind::Text:
    case ControlPropertyKind::MechanismField:
    case ControlPropertyKind::Capability:
    case ControlPropertyKind::Operation:
        output.text = std::string(text);
        return true;
    case ControlPropertyKind::Integer:
        if (!ReadInt64(text, output.number))
        {
            message = "'" + std::string(text) + "' is not a whole number";
            return false;
        }
        return true;
    case ControlPropertyKind::Boolean:
        if (text == "yes")
        {
            output.number = 1;
            return true;
        }
        if (text == "no")
        {
            output.number = 0;
            return true;
        }
        message = "'" + std::string(text) + "' is not yes or no";
        return false;
    case ControlPropertyKind::Axis:
        if (text == "vertical")
        {
            output.number = 0;
            return true;
        }
        if (text == "horizontal")
        {
            output.number = 1;
            return true;
        }
        message = "'" + std::string(text) + "' is not an axis";
        return false;
    case ControlPropertyKind::Extent:
    {
        constexpr std::string_view kFill = "fill";
        if (text.rfind(kFill, 0) == 0)
        {
            output.fill = true;
            output.number = 1;
            if (text.size() == kFill.size())
            {
                return true;
            }
            if (text[kFill.size()] != ':'
                || !ReadInt64(text.substr(kFill.size() + 1), output.number)
                || output.number <= 0)
            {
                message = "'" + std::string(text)
                    + "' is not fill or fill:<weight>";
                return false;
            }
            return true;
        }
        if (!ReadInt64(text, output.number) || output.number < 0)
        {
            message = "'" + std::string(text)
                + "' is not a pixel count or fill";
            return false;
        }
        output.fill = false;
        return true;
    }
    }
    message = "unhandled property type";
    return false;
}

// The `requires` entry that backs a bound field.
//
// Looking the field up here rather than trusting the layout is what makes the
// requires block load-bearing: a `value` control may only bind a field the
// Package declared it would read, and the declaration is what names the
// Mechanism and Definition -- so the host never has to.
const kernel::PresentationAssetRequirement* RequirementFor(
    const kernel::PresentationAsset& asset,
    const std::string& fieldName
)
{
    for (const kernel::PresentationAssetRequirement& requirement
        : asset.requirements)
    {
        if (requirement.kind
                == kernel::PresentationAssetRequirement::Kind::MechanismField
            && requirement.fieldName == fieldName)
        {
            return &requirement;
        }
    }
    return nullptr;
}

class ViewCompiler
{
public:
    ViewCompiler(
        const PresentationSchemaRegistry& schema,
        const kernel::PresentationAsset& asset,
        const kernel::FrozenRuntimeCatalog& runtime
    )
        : schema_(schema), asset_(asset), runtime_(runtime)
    {
    }

    bool Compile(
        const kernel::PresentationAssetNode& root,
        CompiledPresentationView& view
    )
    {
        view_ = &view;
        return Emit(root) != UINT32_MAX;
    }

    const Failure& Error() const noexcept { return failure_; }

private:
    // Parent before children, and every sibling block contiguous.
    //
    // The obvious walk -- append a control, then recurse into each child --
    // does NOT produce that: a child's own children land between it and its
    // next sibling, so `firstChild + childCount` addresses a grandchild. The
    // first version of this did exactly that and the layout came out with a
    // control escaping its parent.
    //
    // So the whole sibling block is reserved first and filled afterwards. Each
    // recursion then appends its own block after everything already present,
    // and every range stays contiguous.
    std::uint32_t Emit(const kernel::PresentationAssetNode& node)
    {
        const std::uint32_t self =
            static_cast<std::uint32_t>(view_->controls.size());
        view_->controls.push_back({});
        return Fill(self, node) ? self : UINT32_MAX;
    }

    bool Fill(std::uint32_t self, const kernel::PresentationAssetNode& node)
    {
        const ControlKindSchema* kind = schema_.Find(node.key);
        if (kind == nullptr)
        {
            Fail(
                PresentationCompileStatus::ControlKindUnknown,
                "'" + node.key + "' is not a control kind"
            );
            return false;
        }
        if (!node.block)
        {
            Fail(
                PresentationCompileStatus::ControlPropertyInvalid,
                node.key + " must be a block"
            );
            return false;
        }

        // Every key is either one of this kind's properties or a nested
        // control. Anything else is refused rather than ignored: a misspelled
        // property on a control nobody clicks is a control that quietly does
        // the wrong thing in a Package that loaded cleanly.
        for (const kernel::PresentationAssetNode& child : node.children)
        {
            const bool isProperty = kind->Slot(child.key).operator bool();
            const bool isControl = schema_.Find(child.key) != nullptr;
            if (!isProperty && !isControl)
            {
                // A block is a misspelled control; a scalar is a misspelled
                // property. Reporting them the same way sends an author
                // looking in the wrong place, and the two are distinguishable
                // by shape alone.
                if (child.block)
                {
                    Fail(
                        PresentationCompileStatus::ControlKindUnknown,
                        "'" + child.key + "' is not a control kind"
                    );
                }
                else
                {
                    Fail(
                        PresentationCompileStatus::ControlPropertyInvalid,
                        "'" + child.key + "' is not a property of "
                            + kind->name
                    );
                }
                return false;
            }
            if (isControl && !kind->allowsChildren)
            {
                Fail(
                    PresentationCompileStatus::ChildrenNotAllowed,
                    kind->name + " cannot contain controls"
                );
                return false;
            }
            if (isProperty && child.block)
            {
                Fail(
                    PresentationCompileStatus::ControlPropertyInvalid,
                    "property '" + child.key + "' is a block"
                );
                return false;
            }
        }

        CompiledControl control;
        control.kind = kind->id;
        control.kindIndex = schema_.IndexOf(kind->id);
        control.firstValue =
            static_cast<std::uint32_t>(view_->values.size());
        control.valueCount =
            static_cast<std::uint32_t>(kind->properties.size());

        // Dense and in schema order, defaults applied. After this nothing
        // asks whether a property was authored.
        for (const ControlPropertySchema& property : kind->properties)
        {
            const kernel::PresentationAssetNode* authored =
                FindChild(node, property.name);
            if (authored == nullptr && property.required)
            {
                Fail(
                    PresentationCompileStatus::ControlPropertyInvalid,
                    kind->name + " needs a " + property.name
                );
                return false;
            }
            const std::string& text = authored != nullptr
                ? authored->value
                : property.defaultValue;
            CompiledControlValue value;
            std::string reason;
            if (!ReadValue(property, text, value, reason))
            {
                Fail(
                    PresentationCompileStatus::ControlPropertyInvalid,
                    kind->name + "." + property.name + ": " + reason
                );
                return false;
            }
            if (property.kind == ControlPropertyKind::MechanismField
                && !ResolveField(value))
            {
                return false;
            }
            if (property.kind == ControlPropertyKind::Capability
                && !ResolveCapability(value, node))
            {
                return false;
            }
            if (property.kind == ControlPropertyKind::Operation
                && !ResolveOperation(value.text))
            {
                return false;
            }
            view_->values.push_back(std::move(value));
        }

        std::vector<const kernel::PresentationAssetNode*> children;
        for (const kernel::PresentationAssetNode& child : node.children)
        {
            if (schema_.Find(child.key) != nullptr)
            {
                children.push_back(&child);
            }
        }
        control.firstChild =
            static_cast<std::uint32_t>(view_->controls.size());
        control.childCount = static_cast<std::uint32_t>(children.size());
        view_->controls.resize(
            view_->controls.size() + children.size()
        );
        // Written before recursing: the recursion reallocates the array, so a
        // reference taken here would dangle.
        view_->controls[self] = control;

        for (std::size_t index = 0; index < children.size(); ++index)
        {
            if (!Fill(control.firstChild
                    + static_cast<std::uint32_t>(index), *children[index]))
            {
                return false;
            }
        }
        return true;
    }

    // A capability a control acts through.
    //
    // Two checks, and they are different questions. The Package must have
    // DECLARED that it uses this contract -- otherwise `requires` is a list
    // nothing consults -- and the Ruleset must actually PUBLISH it. A control
    // that could name any contract at all would be back to naming a verb.
    bool ResolveCapability(
        CompiledControlValue& value,
        const kernel::PresentationAssetNode& node
    )
    {
        const kernel::CapabilityId capability =
            kernel::StableCapabilityId(value.text);
        // The version the CONTROL declares, not the one the requires block
        // happens to carry.
        //
        // Taking it from the requirement made the two checks below collapse
        // into one: with the requirement gone the version was zero, the
        // Ruleset lookup failed on that, and a compiler that never consulted
        // `requires` at all still produced the same refusal. Reading it here
        // keeps them independent -- the Ruleset question is answered for the
        // version the control asked for, and `requires` is asked its own
        // question.
        std::uint32_t version = 1;
        if (const kernel::PresentationAssetNode* authored =
                FindChild(node, "capability_version"))
        {
            std::int64_t parsed = 0;
            if (!ReadInt64(authored->value, parsed) || parsed <= 0
                || parsed > 0xFFFFFFFF)
            {
                Fail(
                    PresentationCompileStatus::ControlPropertyInvalid,
                    "capability_version is not a version"
                );
                return false;
            }
            version = static_cast<std::uint32_t>(parsed);
        }
        bool declared = false;
        for (const kernel::PresentationAssetRequirement& requirement
            : asset_.requirements)
        {
            if (requirement.kind
                    == kernel::PresentationAssetRequirement::Kind::Capability
                && requirement.primaryName == value.text
                && requirement.version == version)
            {
                declared = true;
                break;
            }
        }
        if (!declared)
        {
            Fail(
                PresentationCompileStatus::CapabilityUnresolved,
                "capability '" + value.text + "' at version "
                    + std::to_string(version)
                    + " is used by the layout but not declared in requires"
            );
            return false;
        }
        if (runtime_.FindCapability(capability, version) == nullptr)
        {
            Fail(
                PresentationCompileStatus::CapabilityUnresolved,
                "the Ruleset does not publish capability '" + value.text
                    + "' at version " + std::to_string(version)
            );
            return false;
        }
        value.capability = capability;
        value.number = version;
        pendingCapability_ = capability;
        pendingCapabilityVersion_ = version;
        return true;
    }

    // The operation, checked against the operations the CONTRACT declares.
    // Without this a Package could name any verb it liked and the contract
    // would be decoration.
    bool ResolveOperation(const std::string& operation)
    {
        const kernel::RuntimeCapabilityContract* contract =
            runtime_.FindCapability(
                pendingCapability_,
                pendingCapabilityVersion_
            );
        if (contract == nullptr)
        {
            Fail(
                PresentationCompileStatus::CapabilityUnresolved,
                "an operation was declared before its capability"
            );
            return false;
        }
        for (const std::string& declared : contract->operations)
        {
            if (declared == operation)
            {
                return true;
            }
        }
        Fail(
            PresentationCompileStatus::CapabilityUnresolved,
            "capability '" + contract->canonicalName
                + "' declares no operation '" + operation + "'"
        );
        return false;
    }

    bool ResolveField(CompiledControlValue& value)
    {
        const kernel::PresentationAssetRequirement* requirement =
            RequirementFor(asset_, value.text);
        if (requirement == nullptr)
        {
            Fail(
                PresentationCompileStatus::FieldUnresolved,
                "field '" + value.text
                    + "' is bound by the layout but not declared in requires"
            );
            return false;
        }
        const kernel::MechanismDefinitionId definition =
            kernel::StableMechanismDefinitionId(
                kernel::StableMechanismTypeId(requirement->primaryName),
                requirement->secondaryName
            );
        // One view reads one Definition. Two would need the panel to hold two
        // instances at once, and nothing downstream is built for that; saying
        // so here beats producing a view whose halves disagree about whose
        // numbers they are showing.
        if (view_->definition && view_->definition != definition)
        {
            Fail(
                PresentationCompileStatus::FieldUnresolved,
                "view binds fields from more than one Definition"
            );
            return false;
        }
        view_->definition = definition;

        const auto slot = runtime_.ResolveDefinitionFieldSlot(
            definition,
            value.text
        );
        if (!slot)
        {
            Fail(
                PresentationCompileStatus::FieldUnresolved,
                "the Ruleset has no field '" + value.text + "' on "
                    + requirement->secondaryName
            );
            return false;
        }
        value.fieldSlot = *slot;

        for (const std::string& seen : view_->boundFieldNames)
        {
            if (seen == value.text)
            {
                return true;
            }
        }
        view_->boundFieldNames.push_back(value.text);
        view_->boundFieldSlots.push_back(*slot);
        return true;
    }

    std::uint32_t Fail(PresentationCompileStatus status, std::string message)
    {
        if (failure_.status == PresentationCompileStatus::Ok)
        {
            failure_.status = status;
            failure_.message =
                asset_.canonicalName + ": " + std::move(message);
        }
        return UINT32_MAX;
    }

    const PresentationSchemaRegistry& schema_;
    const kernel::PresentationAsset& asset_;
    const kernel::FrozenRuntimeCatalog& runtime_;
    CompiledPresentationView* view_ = nullptr;
    // The capability of the control being compiled, so its `operation`
    // property can be checked against the right contract. The schema puts
    // `capability` before `operation`, and the slot-constant check in
    // RegisterBuiltinControls is what keeps that true.
    kernel::CapabilityId pendingCapability_;
    std::uint32_t pendingCapabilityVersion_ = 0;
    Failure failure_;
};

}

PresentationCompileStatus PresentationCompiler::Compile(
    const PresentationSchemaRegistry& schema,
    const std::vector<kernel::PresentationAsset>& assets,
    const kernel::FrozenRuntimeCatalog& runtime,
    FrozenPresentationCatalog& output,
    std::string& message
) const
{
    output = FrozenPresentationCatalog{};
    if (!schema.IsFrozen())
    {
        message = "the Presentation Schema Registry is not frozen";
        return PresentationCompileStatus::SchemaNotFrozen;
    }
    if (!runtime.IsFrozen())
    {
        message = "the Runtime Catalog is not frozen";
        return PresentationCompileStatus::RuntimeCatalogNotFrozen;
    }

    for (const kernel::PresentationAsset& asset : assets)
    {
        if (asset.content.empty())
        {
            // A payload asset -- a raster, a font. It declares no view.
            continue;
        }
        if (asset.content.size() != 1)
        {
            message = asset.canonicalName
                + ": content must hold exactly one root control";
            return PresentationCompileStatus::ViewRootInvalid;
        }

        CompiledPresentationView view;
        view.canonicalName = asset.canonicalName;
        {
            // The role is a property rather than a control: it is a statement
            // about the whole view -- what a control's subject IS -- not about
            // any one control.
            const auto role = asset.properties.find("subject_role");
            if (role == asset.properties.end() || role->second.empty())
            {
                message = asset.canonicalName
                    + ": a view needs a subject_role property";
                return PresentationCompileStatus::ViewRootInvalid;
            }
            view.subjectRoleName = role->second;
        }
        view.id = StablePresentationViewId(asset.canonicalName);
        if (output.FindView(view.id) != nullptr)
        {
            message = asset.canonicalName + ": declared twice";
            return PresentationCompileStatus::DuplicateView;
        }

        ViewCompiler compiler(schema, asset, runtime);
        if (!compiler.Compile(asset.content.front(), view))
        {
            message = compiler.Error().message;
            return compiler.Error().status;
        }
        output.views_.push_back(std::move(view));
    }

    output.schema_ = schema;
    output.assets_ = assets;
    // The identity of what went in. A consumer holding a different Ruleset
    // fingerprint is holding field slots that index someone else's layouts.
    output.sourceFingerprint_ =
        kernel::ComputePresentationFingerprint(assets);
    output.rulesetFingerprint_ = runtime.Fingerprint();
    output.frozen_ = true;
    return PresentationCompileStatus::Ok;
}

}

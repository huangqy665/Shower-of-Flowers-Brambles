#include "control_tree.hpp"

#include <algorithm>

#include "fixed_point.hpp"

namespace dillen::presentation {

namespace {

const CompiledControlValue kAbsent{};

}

std::string FormatPanelValue(const MechanismPanelField& field)
{
    if (!field.isDecimal)
    {
        return std::to_string(field.value);
    }
    const std::int64_t scale = kernel::kDecimalInternalScale;
    const bool negative = field.value < 0;
    // Negated as unsigned so INT64_MIN does not overflow on the way in.
    const std::uint64_t magnitude = negative
        ? 0ULL - static_cast<std::uint64_t>(field.value)
        : static_cast<std::uint64_t>(field.value);
    const std::uint64_t whole =
        magnitude / static_cast<std::uint64_t>(scale);
    const std::uint64_t fraction =
        magnitude % static_cast<std::uint64_t>(scale);
    std::string text = negative ? "-" : "";
    text += std::to_string(whole);
    text += '.';
    // Four places, always, because the internal scale is 10^4. Trimming zeroes
    // would make the width of the number depend on its value, which is a
    // layout decision hiding inside a formatting function.
    std::string digits = std::to_string(fraction);
    text.append(4 - digits.size(), '0');
    text += digits;
    return text;
}

ControlTreeStatus ControlTree::Bind(
    const FrozenPresentationCatalog& catalog,
    PresentationViewId view,
    const kernel::FrozenRuntimeCatalog& runtime,
    std::string& message
)
{
    catalog_ = nullptr;
    view_ = nullptr;
    rects_.clear();

    if (!catalog.IsFrozen())
    {
        message = "the Presentation Catalog is not frozen";
        return ControlTreeStatus::CatalogNotFrozen;
    }
    // The one failure a compiled slot cannot survive. A field slot is an index
    // into a specific Ruleset's layouts; handed a different world it reads a
    // real number belonging to something else, which is worse than reading
    // nothing.
    if (!runtime.IsFrozen()
        || catalog.RulesetFingerprint() != runtime.Fingerprint())
    {
        message =
            "the Presentation Catalog was compiled against another Ruleset";
        return ControlTreeStatus::RulesetMismatch;
    }
    const CompiledPresentationView* compiled = catalog.FindView(view);
    if (compiled == nullptr || compiled->controls.empty())
    {
        message = "the catalog has no such view";
        return ControlTreeStatus::ViewMissing;
    }

    catalog_ = &catalog;
    view_ = compiled;
    rects_.assign(compiled->controls.size(), ControlRect{});
    const PresentationSchemaRegistry& schema = catalog.Schema();
    panelKind_ = schema.IndexOf(StableControlKindId("panel"));
    labelKind_ = schema.IndexOf(StableControlKindId("label"));
    valueKind_ = schema.IndexOf(StableControlKindId("value"));
    buttonKind_ = schema.IndexOf(StableControlKindId("button"));
    return ControlTreeStatus::Ok;
}

const CompiledControlValue& ControlTree::Value(
    std::uint32_t control,
    std::uint32_t slot
) const
{
    if (view_ == nullptr || control >= view_->controls.size())
    {
        return kAbsent;
    }
    const CompiledControl& node = view_->controls[control];
    if (slot >= node.valueCount)
    {
        return kAbsent;
    }
    return view_->values[node.firstValue + slot];
}

const ControlRect& ControlTree::Rect(std::uint32_t control) const
{
    static const ControlRect kEmpty{};
    return control < rects_.size() ? rects_[control] : kEmpty;
}

const std::vector<std::string>& ControlTree::BoundFields() const
{
    static const std::vector<std::string> kNone;
    return view_ != nullptr ? view_->boundFieldNames : kNone;
}

const std::string& ControlTree::SubjectRole() const
{
    static const std::string kNone;
    return view_ != nullptr ? view_->subjectRoleName : kNone;
}

kernel::MechanismDefinitionId ControlTree::Definition() const
{
    return view_ != nullptr ? view_->definition
                            : kernel::MechanismDefinitionId{};
}

void ControlTree::Layout(
    std::int32_t viewportWidth,
    std::int32_t viewportHeight
)
{
    if (view_ == nullptr)
    {
        return;
    }
    rects_[0] = {
        0,
        0,
        std::max(viewportWidth, 0),
        std::max(viewportHeight, 0)
    };
    LayoutPanel(0);
}

// Stacks children along the panel's axis inside its rect.
//
// Fixed children take what they ask for; what is left is divided among the
// fill children by weight, and the LAST fill child absorbs the remainder. That
// last clause is the whole reason this is integer arithmetic: three children
// filling a 100 pixel panel get 33, 33 and 34, they exactly cover the panel,
// and no pixel belongs to two of them or to none.
void ControlTree::LayoutPanel(std::uint32_t index)
{
    const CompiledControl& panel = view_->controls[index];
    if (panel.childCount == 0)
    {
        return;
    }
    const ControlRect rect = rects_[index];
    const bool isPanel = panel.kindIndex == panelKind_;
    const bool vertical = !isPanel
        || Value(index, builtin::kPanelAxis).number == 0;
    const std::int32_t pad = isPanel
        ? static_cast<std::int32_t>(Value(index, builtin::kPanelPadding).number)
        : 0;
    const std::int32_t gap = isPanel
        ? static_cast<std::int32_t>(Value(index, builtin::kPanelGap).number)
        : 0;

    const std::int32_t count = static_cast<std::int32_t>(panel.childCount);
    std::int32_t inner = (vertical ? rect.height : rect.width) - 2 * pad
        - gap * (count - 1);
    if (inner < 0)
    {
        inner = 0;
    }
    const std::int32_t cross = (vertical ? rect.width : rect.height) - 2 * pad;

    std::int32_t fixedTotal = 0;
    std::int32_t weightTotal = 0;
    std::uint32_t lastFill = UINT32_MAX;
    for (std::uint32_t step = 0; step < panel.childCount; ++step)
    {
        const CompiledControlValue& size =
            Value(panel.firstChild + step, builtin::kSize);
        if (size.fill)
        {
            weightTotal += static_cast<std::int32_t>(size.number);
            lastFill = step;
        }
        else
        {
            fixedTotal += static_cast<std::int32_t>(size.number);
        }
    }
    std::int32_t free = inner - fixedTotal;
    if (free < 0)
    {
        free = 0;
    }

    std::int32_t cursor = (vertical ? rect.y : rect.x) + pad;
    const std::int32_t crossOrigin = (vertical ? rect.x : rect.y) + pad;
    std::int32_t used = 0;
    for (std::uint32_t step = 0; step < panel.childCount; ++step)
    {
        const std::uint32_t child = panel.firstChild + step;
        const CompiledControlValue& size = Value(child, builtin::kSize);
        std::int32_t extent = static_cast<std::int32_t>(size.number);
        if (size.fill)
        {
            extent = step == lastFill
                ? free - used
                : (weightTotal > 0
                    ? free * static_cast<std::int32_t>(size.number)
                        / weightTotal
                    : 0);
            used += extent;
        }
        if (extent < 0)
        {
            extent = 0;
        }
        rects_[child] = vertical
            ? ControlRect{crossOrigin, cursor, std::max(cross, 0), extent}
            : ControlRect{cursor, crossOrigin, extent, std::max(cross, 0)};
        cursor += extent + gap;
        LayoutPanel(child);
    }
}

std::uint32_t ControlTree::HitTestIn(
    std::uint32_t index,
    std::int32_t x,
    std::int32_t y
) const
{
    if (!rects_[index].Contains(x, y))
    {
        return UINT32_MAX;
    }
    // Reverse order: later siblings are drawn on top, so they are hit first.
    const CompiledControl& control = view_->controls[index];
    for (std::uint32_t step = control.childCount; step > 0; --step)
    {
        const std::uint32_t hit =
            HitTestIn(control.firstChild + step - 1, x, y);
        if (hit != UINT32_MAX)
        {
            return hit;
        }
    }
    return index;
}

std::uint32_t ControlTree::HitTest(std::int32_t x, std::int32_t y) const
{
    if (view_ == nullptr)
    {
        return UINT32_MAX;
    }
    return HitTestIn(0, x, y);
}

std::uint32_t ControlTree::Find(std::string_view id) const
{
    if (view_ == nullptr)
    {
        return UINT32_MAX;
    }
    for (std::uint32_t index = 0;
        index < view_->controls.size();
        ++index)
    {
        if (Value(index, builtin::kId).text == id)
        {
            return index;
        }
    }
    return UINT32_MAX;
}

void ControlTree::CollectDraw(
    std::uint32_t index,
    const MechanismPanelReadout& readout,
    std::vector<ControlDraw>& output
) const
{
    const CompiledControl& control = view_->controls[index];
    const bool background = Value(index, builtin::kBackground).number != 0;
    const bool isPanel = control.kindIndex == panelKind_;
    if (!isPanel || background)
    {
        ControlDraw draw;
        draw.control = index;
        draw.rect = rects_[index];
        draw.background = background;
        draw.actionable = control.kindIndex == buttonKind_;
        draw.text = Value(index, builtin::kText).text;
        if (control.kindIndex == valueKind_ && readout.valid)
        {
            const kernel::MechanismFieldSlotId slot =
                Value(index, builtin::kValueField).fieldSlot;
            for (const MechanismPanelField& field : readout.fields)
            {
                if (field.slot == slot)
                {
                    draw.text += FormatPanelValue(field);
                    break;
                }
            }
        }
        output.push_back(std::move(draw));
    }
    for (std::uint32_t step = 0; step < control.childCount; ++step)
    {
        CollectDraw(control.firstChild + step, readout, output);
    }
}

std::vector<ControlDraw> ControlTree::Draw(
    const MechanismPanelReadout& readout
) const
{
    std::vector<ControlDraw> output;
    if (view_ == nullptr)
    {
        return output;
    }
    output.reserve(view_->controls.size());
    CollectDraw(0, readout, output);
    return output;
}

bool ControlTree::IntentFor(
    std::uint32_t control,
    kernel::EntityId entity,
    MapIntent& intent
) const
{
    if (view_ == nullptr
        || control >= view_->controls.size()
        || view_->controls[control].kindIndex != buttonKind_
        || !entity)
    {
        return false;
    }
    // Everything below was resolved by the Compiler against the Ruleset. This
    // function does no lookup, no comparison against a verb it knows, and no
    // parsing -- it copies four compiled values into an intent.
    intent.entity = entity;
    intent.capability = Value(control, builtin::kButtonCapability).capability;
    intent.capabilityVersion = static_cast<std::uint32_t>(
        Value(control, builtin::kButtonCapabilityVersion).number
    );
    intent.field = Value(control, builtin::kButtonField).fieldSlot;
    intent.delta = Value(control, builtin::kButtonAmount).number;
    return static_cast<bool>(intent);
}

}

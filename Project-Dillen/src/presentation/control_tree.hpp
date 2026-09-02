#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "frozen_presentation_catalog.hpp"
#include "map_command.hpp"
#include "mechanism_panel.hpp"

namespace dillen::presentation {

// Layout and hit testing over a compiled view.
//
// This used to be the whole of the control system: it read a tree of strings
// out of an asset, decided what a `panel` was, checked property names against
// hand-written `if` chains, and did it again on every load. A review called
// that what it was -- Presentation running on its authoring form -- and it is
// now split in two:
//
//   PresentationCompiler   names -> types, slots and ids, once, at load
//   ControlTree (here)     arithmetic on the result, per viewport
//
// So there is not a single string comparison below. A control's kind is an
// index, its properties are values at known slots, and its bound fields are
// MechanismFieldSlotIds. What is left is genuinely layout: integer arithmetic
// over a flat array.
//
// The compiled view is immutable and shared; the rects are per-instance. Two
// viewers at different window sizes hold two ControlTrees over one catalog,
// which is the arrangement that makes a catalog worth freezing.

enum class ControlAxis
{
    Vertical,
    Horizontal
};

struct ControlRect
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;

    bool Contains(std::int32_t px, std::int32_t py) const noexcept
    {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
};

// What a control shows for one province. Text is produced here rather than in
// a backend so that the number a player sees and the number a probe asserts
// come from the same code.
struct ControlDraw
{
    // Index into the compiled view, so a caller can ask the tree anything
    // else about the control without carrying a pointer.
    std::uint32_t control = 0;
    ControlRect rect;
    std::string text;
    bool background = false;
    // Whether this control can produce an intent. Carried on the draw record
    // so a backend does not have to ask the tree a second question -- and so
    // "what looks like a button" and "what acts like one" are the same bit.
    bool actionable = false;
};

enum class ControlTreeStatus
{
    Ok,
    NotBound,
    CatalogNotFrozen,
    ViewMissing,
    // The catalog was compiled against a different Ruleset than the one being
    // read. Its field slots index someone else's layouts.
    RulesetMismatch
};

class ControlTree
{
public:
    // Binds to one compiled view. `runtime` is checked against the
    // fingerprint the catalog was compiled with -- a catalog and a world that
    // do not match is the one failure a resolved slot cannot survive.
    ControlTreeStatus Bind(
        const FrozenPresentationCatalog& catalog,
        PresentationViewId view,
        const kernel::FrozenRuntimeCatalog& runtime,
        std::string& message
    );

    bool IsBound() const noexcept { return view_ != nullptr; }
    std::size_t Count() const noexcept
    {
        return view_ != nullptr ? view_->controls.size() : 0;
    }
    const CompiledPresentationView& View() const noexcept { return *view_; }

    // Assigns every control a rect. Integer arithmetic throughout: a hit test
    // has to agree with what was drawn, and float rects that round differently
    // in two places are how that stops being true.
    void Layout(std::int32_t viewportWidth, std::int32_t viewportHeight);

    const ControlRect& Rect(std::uint32_t control) const;

    // The deepest control containing the point, or UINT32_MAX. Children win
    // over their parent and later siblings over earlier ones, which is the
    // order they are drawn in.
    std::uint32_t HitTest(std::int32_t x, std::int32_t y) const;

    // The control with this authored id, or UINT32_MAX. For a host that wants
    // to address a control it knows about; the layout itself never needs it.
    std::uint32_t Find(std::string_view id) const;

    // One entry per control that shows text or paints, in draw order.
    std::vector<ControlDraw> Draw(const MechanismPanelReadout& readout) const;

    // A button's intent for the province the panel is showing. False for a
    // control with no action.
    bool IntentFor(
        std::uint32_t control,
        kernel::EntityId entity,
        MapIntent& intent
    ) const;

    // The field names this view binds, in tree order. A panel binds with
    // exactly these, so the layout and the reader cannot drift apart.
    const std::vector<std::string>& BoundFields() const;
    kernel::MechanismDefinitionId Definition() const;
    // The role a control's subject binds through, as the Package declared it.
    const std::string& SubjectRole() const;

    // Typed property access at a schema slot. No string ever reaches these.
    const CompiledControlValue& Value(
        std::uint32_t control,
        std::uint32_t slot
    ) const;

private:
    void LayoutPanel(std::uint32_t control);
    std::uint32_t HitTestIn(
        std::uint32_t control,
        std::int32_t x,
        std::int32_t y
    ) const;
    void CollectDraw(
        std::uint32_t control,
        const MechanismPanelReadout& readout,
        std::vector<ControlDraw>& output
    ) const;

    const FrozenPresentationCatalog* catalog_ = nullptr;
    const CompiledPresentationView* view_ = nullptr;
    std::vector<ControlRect> rects_;
    // The kind indices this layout engine acts on, resolved once at Bind so
    // the walk compares integers rather than ids.
    std::uint32_t panelKind_ = UINT32_MAX;
    std::uint32_t labelKind_ = UINT32_MAX;
    std::uint32_t valueKind_ = UINT32_MAX;
    std::uint32_t buttonKind_ = UINT32_MAX;
};

// Renders a mechanism panel value the way the UI shows it. Integers as
// themselves; decimals divided out of the fixed-point internal scale with a
// fixed number of places, so the same value is the same string everywhere.
std::string FormatPanelValue(const MechanismPanelField& field);

}

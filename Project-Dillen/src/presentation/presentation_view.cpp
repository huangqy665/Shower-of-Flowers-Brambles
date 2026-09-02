#include "presentation_view.hpp"

namespace dillen::presentation {

PresentationView::PresentationView(
    runtime::WorldQuerySnapshotHandle snapshot
)
{
    Advance(std::move(snapshot));
}

bool PresentationView::IsBound() const noexcept
{
    return snapshot_ != nullptr;
}

const runtime::WorldQuerySnapshot& PresentationView::World() const noexcept
{
    return *snapshot_;
}

runtime::WorldQueryStamp PresentationView::Stamp() const noexcept
{
    return snapshot_ == nullptr
        ? runtime::WorldQueryStamp{}
        : snapshot_->Stamp();
}

bool PresentationView::Advance(runtime::WorldQuerySnapshotHandle snapshot)
{
    if (snapshot == nullptr || !snapshot->IsPublished())
    {
        return false;
    }
    // Publication counts monotonically per Query Service, so it is the one
    // field that orders two snapshots without knowing anything else about
    // them. Refusing to go backwards matters because presentation is the only
    // reader that keeps a snapshot alive across ticks: an out-of-order swap
    // would show the viewer a world that un-happened.
    if (snapshot_ != nullptr
        && snapshot->Stamp().publication <= snapshot_->Stamp().publication)
    {
        return false;
    }
    snapshot_ = std::move(snapshot);
    return true;
}

void PresentationView::Reset() noexcept
{
    snapshot_.reset();
}

}

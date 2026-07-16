#include "session/dialog_cancellation.h"

namespace interview {
namespace session {

void DialogCancellationToken::RequestStop() {
    stop_requested_.store(true, std::memory_order_release);
}

bool DialogCancellationToken::IsStopRequested() const {
    return stop_requested_.load(std::memory_order_acquire);
}

} // namespace session
} // namespace interview

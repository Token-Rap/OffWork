#include "reminder.h"

namespace offwork {
namespace {

constexpr int kSecondsPerDay = 24 * 60 * 60;

}  // namespace

int SecondsSinceShiftEnd(int nowSeconds, int endMinutes) {
    const int delta = (nowSeconds - endMinutes * 60) % kSecondsPerDay;
    return delta < 0 ? delta + kSecondsPerDay : delta;
}

ReminderState ResetReminder(WorkdayPhase currentPhase, int remainingSeconds) {
    ReminderState state;
    state.initialized = true;
    state.lastPhase = currentPhase;
    state.lastRemainingSeconds = remainingSeconds;
    state.fiveMinutesHandled = remainingSeconds >= 0 && remainingSeconds <= kFiveMinuteReminderSeconds;
    return state;
}

ReminderDecision TickReminder(
    const ReminderState& state, WorkdayPhase phase, int secondsSinceEnd, int remainingSeconds) {
    ReminderDecision decision;
    decision.next = ResetReminder(phase, remainingSeconds);
    decision.next.lastSecondsSinceEnd = secondsSinceEnd;
    if (!state.initialized) {
        return decision;
    }
    if (state.lastPhase == WorkdayPhase::Working && phase == WorkdayPhase::Working) {
        decision.next.fiveMinutesHandled = state.fiveMinutesHandled;
        if (remainingSeconds >= 0 && remainingSeconds <= kFiveMinuteReminderSeconds) {
            const int gap = state.lastSecondsSinceEnd < 0 ? 0 :
                (secondsSinceEnd - state.lastSecondsSinceEnd + kSecondsPerDay) % kSecondsPerDay;
            decision.notifyFiveMinutes = !state.fiveMinutesHandled &&
                state.lastRemainingSeconds > kFiveMinuteReminderSeconds && gap <= kReminderGraceSeconds;
            // Consume even a stale crossing, so later ticks cannot catch it up.
            decision.next.fiveMinutesHandled = true;
        }
    }
    // Rearming is implicit: any tick that sees Working makes the next
    // AfterWork a transition again. Overnight shifts never report BeforeWork,
    // so rearming must not depend on it.
    decision.notify = state.lastPhase == WorkdayPhase::Working &&
                      phase == WorkdayPhase::AfterWork &&
                      secondsSinceEnd <= kReminderGraceSeconds;
    return decision;
}

}  // namespace offwork

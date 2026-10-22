#pragma once

// Off-work reminder decision. Pure: no Windows headers, no clock, no I/O, so
// every transition can be tested deterministically. The window feeds it one
// snapshot per clock tick and shows balloons when it says so.
//
// Every operation returns a new ReminderState instead of mutating its input.

#include "calculator.h"

namespace offwork {

// A transition first observed later than this after the end time is treated
// as stale and stays silent (machine asleep or locked across the end time,
// clock jumps). User decision, 2026-09-29. Boundary inclusive.
inline constexpr int kReminderGraceSeconds = 10 * 60;
inline constexpr int kFiveMinuteReminderSeconds = 5 * 60;

struct ReminderState {
    bool initialized = false;
    WorkdayPhase lastPhase = WorkdayPhase::AfterWork;
    int lastRemainingSeconds = -1;
    int lastSecondsSinceEnd = -1;
    bool fiveMinutesHandled = false;
};

struct ReminderDecision {
    ReminderState next;
    bool notify = false;
    bool notifyFiveMinutes = false;
};

// Seconds since the most recent end-of-shift moment, in [0, 86400).
int SecondsSinceShiftEnd(int nowSeconds, int endMinutes);

// Baseline without notifying: at startup, and whenever the schedule changes.
ReminderState ResetReminder(WorkdayPhase currentPhase, int remainingSeconds = -1);

// One clock tick. End notification requires a fresh Working -> AfterWork
// transition. Five-minute notification requires crossing the REAL remaining-time
// threshold while Working, including during lunch; startup, reset
// and stale observations stay silent.
// remainingSeconds = -1 keeps the legacy end-only API available.
ReminderDecision TickReminder(
    const ReminderState& state, WorkdayPhase phase, int secondsSinceEnd, int remainingSeconds = -1);

}  // namespace offwork

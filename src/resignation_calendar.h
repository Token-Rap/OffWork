#pragma once

#include <windows.h>

namespace offwork {

// Owned, modeless calendar; closing without choosing leaves settings intact.
void ShowResignationCalendar();
// Screen-space layout for the date field and its monitor work area.
void ShowResignationCalendarAt(const RECT& anchor, const RECT& work);
void CloseResignationCalendar();

}  // namespace offwork

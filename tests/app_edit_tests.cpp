#include "app_edit.h"
#include "app_view.h"
#include "check.h"
#include <iostream>

int main() {
    using namespace offwork;
    g.settings = Settings{};
    PopulateFields();
    CHECK(g.fields[3].text.empty() && g.fields[4].text.empty());
    Settings parsed;
    CHECK(ReadFieldSettings(parsed));
    CHECK(!parsed.deductLunch);

    g.fields[3] = MakeField(L"12:00");
    CHECK(!ReadFieldSettings(parsed)); // Partial lunch must not be silently lost.
    g.fields[4] = MakeField(L"13:30");
    g.editDeductLunch = true;
    CHECK(ReadFieldSettings(parsed));
    CHECK(parsed.lunchStartMinutes == 720 && parsed.lunchEndMinutes == 810);
    CHECK(parsed.deductLunch);
    SetEndInputMode(EndInputMode::WorkDuration);
    CHECK(g.fields[1].text == L"7.5"); // 09:00–18:00 minus 90 minutes.
    g.fields[1] = MakeField(L"8");
    CHECK(ReadFieldSettings(parsed));
    CHECK(parsed.endMinutes == 1110); // 18:30
    SetEndInputMode(EndInputMode::EndTime);
    CHECK(g.fields[1].text == L"18:30");
    g.settings = parsed;
    PopulateFields();
    CHECK(g.fields[1].text == L"8");
    CHECK(g.fields[3].text == L"12:00" && g.fields[4].text == L"13:30");
    CHECK(g.editDeductLunch);
    g.editDeductLunch = false;
    CHECK(ReadFieldSettings(parsed));
    CHECK(parsed.endMinutes == 1020); // 17:00 without lunch deduction.
    g.fields[3] = MakeField(L"");
    g.fields[4] = MakeField(L"");
    CHECK(ReadFieldSettings(parsed));
    CHECK(parsed.lunchStartMinutes == -1 && !parsed.deductLunch);
    g.fields[3] = MakeField(L"12:00");
    g.fields[4] = MakeField(L"12:00");
    CHECK(!ReadFieldSettings(parsed));
    g.fields[4] = MakeField(L"11:59");
    g.editDeductLunch = true;
    CHECK(!ReadFieldSettings(parsed)); // Cannot represent an >=24h duration.
    std::cout << "All lunch editor tests passed.\n";
}

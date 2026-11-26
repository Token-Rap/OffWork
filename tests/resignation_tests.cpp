#include "resignation.h"
#include "settings.h"
#include "check.h"

#include <filesystem>
#include <iostream>
#include <windows.h>

int main() {
    using namespace offwork;
    using namespace std::chrono;
    for (const auto text : {L"", L"2026-2-01", L"2026-02-29", L"1900-02-29",
            L"0000-01-01", L"10000-01-01", L"2026-13-01", L"2026-04-31",
            L"2026-01-00", L"2026-11-30junk", L" 2026-11-30", L"2026/11/30"})
        CHECK(!ParseResignationDate(text));
    CHECK(ParseResignationDate(L"2000-02-29").has_value());
    CHECK(ParseResignationDate(L"0001-01-01").has_value());
    CHECK(ParseResignationDate(L"9999-12-31").has_value());
    const auto count = [](const wchar_t* today, const wchar_t* target, bool shown = true) {
        return RemainingResignationWorkdays(*ParseResignationDate(today), target, shown);
    };
    CHECK(count(L"2026-11-26", L"2026-11-30") == 2); // Thu -> Fri, Mon.
    CHECK(count(L"2026-11-27", L"2026-11-30") == 1);
    CHECK(count(L"2026-11-28", L"2026-11-30") == 1);
    CHECK(count(L"2026-11-29", L"2026-11-30") == 1);
    CHECK(count(L"2026-11-30", L"2026-11-30") == 0);
    CHECK(!count(L"2026-12-01", L"2026-11-30"));
    CHECK(count(L"2026-11-27", L"2026-11-29") == 0); // Weekend target.
    CHECK(count(L"2026-11-29", L"2026-11-29") == 0);
    CHECK(count(L"2026-11-23", L"2026-11-30") == 5);
    CHECK(count(L"2026-12-31", L"2027-01-01") == 1); // No holiday dataset.
    CHECK(count(L"2024-02-28", L"2024-03-01") == 2);
    CHECK(count(L"2026-03-06", L"2026-03-09") == 1); // Civil dates, no DST hours.
    CHECK(!count(L"2026-11-26", L"2026-11-30", false));
    CHECK(!count(L"2026-11-26", L""));
    CHECK(!count(L"2026-11-26", L"2026-02-29"));
    CHECK(count(L"9999-12-31", L"9999-12-31") == 0);
    CHECK(count(L"0001-01-01", L"9999-12-31").value() > 2000000);

    const auto path = std::filesystem::temp_directory_path() /
        (L"offwork-resignation-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::filesystem::remove(path);
    auto settings = LoadSettings(path.wstring());
    CHECK(!settings.showResignation && settings.resignationDate.empty());
    // An existing settings file keeps all old values when the new keys are absent.
    settings.startMinutes = 480;
    settings.endMinutes = 1020;
    settings.hideEarned = true;
    SaveSettings(path.wstring(), settings);
    WritePrivateProfileStringW(L"resignation", L"date", nullptr, path.c_str());
    WritePrivateProfileStringW(L"resignation", L"show", nullptr, path.c_str());
    settings = LoadSettings(path.wstring());
    CHECK(!settings.showResignation && settings.resignationDate.empty());
    CHECK(settings.startMinutes == 480 && settings.endMinutes == 1020 && settings.hideEarned);
    settings.resignationDate = L"2026-11-30";
    settings.showResignation = true;
    SaveSettings(path.wstring(), settings);
    settings = LoadSettings(path.wstring());
    CHECK(settings.showResignation && settings.resignationDate == L"2026-11-30");
    settings.showResignation = false;
    SaveSettings(path.wstring(), settings);
    settings = LoadSettings(path.wstring());
    CHECK(!settings.showResignation && settings.resignationDate == L"2026-11-30");
    settings.showResignation = true;
    SaveSettings(path.wstring(), settings);
    CHECK(LoadSettings(path.wstring()).showResignation);
    for (const auto text : {L"2026-02-29", L"2026-11-30junk", L"2026-11-30000000000000000000000000000000"}) {
        WritePrivateProfileStringW(L"resignation", L"date", text, path.c_str());
        CHECK(LoadSettings(path.wstring()).resignationDate.empty());
    }
    settings.resignationDate.clear();
    SaveSettings(path.wstring(), settings);
    CHECK(LoadSettings(path.wstring()).resignationDate.empty());
    std::filesystem::remove(path);
    std::cout << "All resignation date boundary and persistence tests passed.\n";
}

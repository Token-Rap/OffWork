#include "money_display.h"
#include "check.h"

#include <iostream>
#include <string_view>

int main() {
    using namespace offwork;
    // Currency, zero, cents, salary limits and rate suffixes are all part of
    // the amount: none may escape masking. Labels belong outside the helper.
    for (const std::wstring_view amount : {
             L"¥0.00", L"¥0.01", L"¥500.00", L"¥1000000.00",
             L"¥12.34/h", L"¥12345.67/month"}) {
        CHECK(MoneyDisplayText(amount, true) == L"****");
        CHECK(MoneyDisplayText(amount, false) == amount);
        CHECK(MoneyDisplayText(amount, true) == L"****");
    }
    CHECK(MoneyDisplayText(L"", true) == L"****");
    CHECK(MoneyDisplayText(L"", false).empty());
    std::cout << "All money display tests passed.\n";
}

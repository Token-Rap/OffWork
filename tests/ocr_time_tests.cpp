#include "ocr_time.h"
#include "check.h"
#include <iostream>
int main() {
    using offwork::ParseClockInTime;
    CHECK(ParseClockInTime(L"上班打卡 09:12:34") == 552);
    CHECK(ParseClockInTime(L"07:01\n上班 09:12\n下班 18:00") == 552);
    CHECK(ParseClockInTime(L"上班 09:12\n下班打卡 06:00") == 552);
    CHECK(ParseClockInTime(L"打卡 08:30\n18:00") == 510);
    CHECK(ParseClockInTime(L"20:00 00:03 08:30") == 3);
    CHECK(ParseClockInTime(L"上 班 ０９ ： ０５ ： ４５") == 545);
    CHECK(ParseClockInTime(L"上班\n08:15") == 495);
    CHECK(!ParseClockInTime(L"24:00 12:60 09:30:99 123:45 09:123 09:30:2"));
    CHECK(!ParseClockInTime(L"没有时间"));
    CHECK(!ParseClockInTime(L""));
    CHECK(ParseClockInTime(L"23:59:59") == 1439);
    CHECK(ParseClockInTime(L"打卡结果\n08:55 上班打卡•成功\n班次时间 10月21日 08:30上班\n打卡方式 Wi-Fi打卡\n2026年10月21日") == 535);
    CHECK(ParseClockInTime(L"班次时间 10月21日 08:30上班\n打卡结果\n08:55 上班打卡•成功") == 535);
    CHECK(ParseClockInTime(L"班次 08:30\n应上班 08:00\n打卡成功 08 ： 55") == 535);
    CHECK(ParseClockInTime(L"打卡•成功 08：55") == 535);
    CHECK(!ParseClockInTime(L"班次时间 08:30上班\n应上班 09:00"));
    CHECK(!ParseClockInTime(L"班 次 时 间\n10 月 21 日 08 ： 30 上 班"));
    CHECK(ParseClockInTime(L"班次时间\n10月21日 08:30上班\n09:01") == 541);
    CHECK(ParseClockInTime(L"班次时间：\n08:30上班\n打卡成功\n08:55") == 535);
    CHECK(ParseClockInTime(L"班次时间（计划）\n08:30上班\n打卡•成功\n08:55") == 535);
    CHECK(ParseClockInTime(L"下班打卡成功 18:00\n上班打卡 08:55") == 535);
    CHECK(ParseClockInTime(L"下班打卡•成功 06:00\n上班 08:55") == 535);
    CHECK(ParseClockInTime(L"上班打卡 ０ ９ ： ０ ５") == 545);
    CHECK(ParseClockInTime(L"班次时间10月21日\n08:30上班\n08:55") == 535);
    std::cout << "All OCR time parser tests passed.\n";
}

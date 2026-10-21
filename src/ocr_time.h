#pragma once

#include <algorithm>
#include <cwctype>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace offwork {

// Pure parser: normalize OCR spacing/full-width punctuation, but preserve lines
// so a scheduled shift cannot lend its time to a clock-in label on another row.
inline std::optional<int> ParseClockInTime(const std::wstring& input) {
    std::wstring text;
    for (std::size_t i = 0; i < input.size(); ++i) {
        wchar_t c = input[i];
        if (c >= L'０' && c <= L'９') c = L'0' + (c - L'０');
        if (c == L'：') c = L':';
        if (c == L'\n') text += c;
        else if (!std::iswspace(c)) text += c;
        else if (!text.empty() && text.back() >= L'0' && text.back() <= L'9') {
            auto next = i + 1;
            while (next < input.size() && input[next] != L'\n' && std::iswspace(input[next])) ++next;
            if (next < input.size() && ((input[next] >= L'0' && input[next] <= L'9') ||
                (input[next] >= L'０' && input[next] <= L'９'))) text += L' ';
        }
    }
    // Join split OCR digits only inside a two-digit clock component, never
    // across independent times such as "20:00 00:03".
    text = std::regex_replace(text, std::wregex(LR"((^|[^0-9:])([0-9]) ([0-9])(?=:))"), L"$1$2$3");
    text = std::regex_replace(text, std::wregex(LR"(:([0-9]) ([0-9])($|[^0-9:]))"), L":$1$2$3");
    struct Line { std::wstring text; bool excluded; };
    std::vector<Line> lines;
    for (std::size_t begin = 0; begin <= text.size();) {
        const auto end = text.find(L'\n', begin);
        auto line = text.substr(begin, end == std::wstring::npos ? end : end - begin);
        const bool excluded = line.find(L"班次") != std::wstring::npos ||
                              line.find(L"应上班") != std::wstring::npos;
        lines.push_back({std::move(line), excluded});
        if (end == std::wstring::npos) break;
        begin = end + 1;
    }
    for (std::size_t row = 1; row < lines.size(); ++row) {
        const auto& previous = lines[row - 1].text;
        // OCR commonly puts the shift label and its value on separate rows.
        const bool scheduleLabel = (previous.find(L"班次") != std::wstring::npos ||
            previous.find(L"应上班") != std::wstring::npos) &&
            !std::regex_search(previous, std::wregex(LR"([0-9]+:[0-9]+)"));
        const bool clockIn = lines[row].text.find(L"上班打卡") != std::wstring::npos ||
            lines[row].text.find(L"打卡成功") != std::wstring::npos ||
            lines[row].text.find(L"打卡•成功") != std::wstring::npos;
        if (scheduleLabel && !clockIn) lines[row].excluded = true;
    }
    struct Time { std::size_t line, begin, end; int minutes; };
    std::vector<Time> times;
    // Match the entire numeric token first, then validate, avoiding valid-looking
    // suffixes of malformed values such as 123:45 or 09:30:99.
    const std::wregex pattern(LR"([0-9]+(?::[0-9]+)+)");
    const std::wregex valid(LR"(([0-9]{2}):([0-9]{2})(?::([0-9]{2}))?)");
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].excluded) continue;
        const auto& line = lines[i].text;
        for (auto it = std::wsregex_iterator(line.begin(), line.end(), pattern);
             it != std::wsregex_iterator(); ++it) {
            std::wsmatch match;
            const auto token = it->str();
            if (!std::regex_match(token, match, valid)) continue;
            const int hour = std::stoi(match[1]), minute = std::stoi(match[2]);
            if (hour > 23 || minute > 59 ||
                (match[3].matched && std::stoi(match[3]) > 59)) continue;
            times.push_back({i, static_cast<std::size_t>(it->position()),
                             static_cast<std::size_t>(it->position() + it->length()),
                             hour * 60 + minute});
        }
    }
    std::optional<int> best;
    std::size_t bestScore = std::wstring::npos;
    const std::vector<std::wstring> labels{L"上班打卡", L"打卡成功", L"打卡•成功", L"打卡·成功", L"上班", L"打卡"};
    for (std::size_t row = 0; row < lines.size(); ++row) {
        if (lines[row].excluded) continue;
        for (std::size_t rank = 0; rank < labels.size(); ++rank) {
            const auto& label = labels[rank];
            for (auto pos = lines[row].text.find(label); pos != std::wstring::npos;
                 pos = lines[row].text.find(label, pos + 1)) {
                if (pos >= 2 && lines[row].text.substr(pos - 2, 2) == L"下班") continue;
                const bool ownTime = std::any_of(times.begin(), times.end(),
                    [row](const Time& time) { return time.line == row; });
                for (const auto& time : times) {
                    const auto rowDistance = time.line > row ? time.line - row : row - time.line;
                    if (rowDistance > 1 || (rowDistance && ownTime)) continue;
                    const auto distance = rowDistance ? 40 :
                        (time.begin >= pos + label.size() ? time.begin - pos - label.size() :
                         pos >= time.end ? pos - time.end : 0);
                    if (distance > 40) continue;
                    const auto score = (rank == 0 ? 0 : rank < 4 ? 1 : rank - 2) * 1000 + distance;
                    if (score < bestScore) { bestScore = score; best = time.minutes; }
                }
            }
        }
    }
    if (best) return best;
    for (const auto& time : times) {
        if (!best || time.minutes < *best) best = time.minutes;
    }
    return best;
}

}  // namespace offwork

#include "check.h"
#include "update_info.h"

#include <iostream>
#include <limits>
#include <string>

namespace {
void ReleaseLocations() {
    using namespace offwork;
    for (const auto location : {
            "https://github.com/Token-Rap/OffWork/releases/tag/v0.4.1",
            "/Token-Rap/OffWork/releases/tag/v0.4.1",
            "https://github.com/alertform/OffWork/releases/tag/v0.4.1",
            "/alertform/OffWork/releases/tag/v0.4.1"}) {
        const auto release = ParseReleaseLocation(location);
        CHECK(release && release->tag == "v0.4.1");
        CHECK(release->installerUrl ==
            "https://github.com/Token-Rap/OffWork/releases/download/v0.4.1/OffWork-Setup.exe");
    }
    CHECK(ParseReleaseLocation("/alertform/OffWork/releases/tag/v10.20.30").has_value());
    for (const auto location : {"", "http://github.com/alertform/OffWork/releases/tag/v0.4.1",
            "https://github.com.evil.test/alertform/OffWork/releases/tag/v0.4.1",
            "https://evil.test/alertform/OffWork/releases/tag/v0.4.1",
            "https://github.com@evil.test/alertform/OffWork/releases/tag/v0.4.1",
            "//github.com/alertform/OffWork/releases/tag/v0.4.1",
            "/other/OffWork/releases/tag/v0.4.1", "/alertform/Other/releases/tag/v0.4.1",
            "/alertform/OffWork/releases/download/v0.4.1"}) {
        CHECK(!ParseReleaseLocation(location));
    }
    for (const auto tag : {"", "0.4.1", "V0.4.1", "v0.4", "v0.4.1.2", "v00.4.1",
            "v0.04.1", "v0.4.01", "v0.4.-1", "v0.4.1-rc.1", "v0.4.1+build.1",
            "v0.4.1/", "v0.4.1?x=1", "v0.4.1#fragment", "v0.4.1%2f..", "../v0.4.1",
            " v0.4.1", "v0.4.1 ", "v0.4.1\r\n", "v0.4.1\\evil"}) {
        CHECK(!ParseReleaseLocation(std::string("/alertform/OffWork/releases/tag/") + tag));
    }
    auto embeddedNull = std::string("/alertform/OffWork/releases/tag/v0.4.1");
    embeddedNull.push_back('\0');
    embeddedNull += "evil";
    CHECK(!ParseReleaseLocation(embeddedNull));
}

void FailedChecksBackOffAndSuccessResets() {
    using namespace offwork;
    constexpr std::uint64_t minute = 60000;
    UpdateCheckSchedule schedule;
    std::uint64_t now = 12345;
    CHECK(schedule.Due(now));
    for (const unsigned minutes : {10, 20, 40, 80, 160, 320, 320, 320}) {
        schedule.Complete(now, false);
        CHECK(!schedule.Due(now));
        CHECK(!schedule.Due(now + minutes * minute - 1));
        CHECK(schedule.Due(now + minutes * minute));
        now += minutes * minute;
    }
    // A long outage must stay capped without overflowing the failure counter.
    for (int i = 0; i < 1000; ++i) schedule.Complete(now, false);
    CHECK(!schedule.Due(now + 320 * minute - 1));
    CHECK(schedule.Due(now + 320 * minute));
    // Success includes a valid check reporting no newer release.
    schedule.Complete(now, true);
    CHECK(!schedule.Due(now + 360 * minute - 1));
    CHECK(schedule.Due(now + 360 * minute));
    now += 360 * minute;
    schedule.Complete(now, false);
    CHECK(!schedule.Due(now + 10 * minute - 1));
    CHECK(schedule.Due(now + 10 * minute));
}
} // namespace

int main() {
    using namespace offwork;
    ReleaseLocations();
    FailedChecksBackOffAndSuccessResets();
    CHECK(InstallerDownloadPercent(0, std::nullopt) == -1);
    CHECK(InstallerDownloadPercent(500, std::nullopt) == -1);
    CHECK(InstallerDownloadPercent(0, 0) == -1);
    CHECK(InstallerDownloadPercent(0, 1000) == 0);
    CHECK(InstallerDownloadPercent(1, 1000) == 0);
    CHECK(InstallerDownloadPercent(500, 1000) == 50);
    CHECK(InstallerDownloadPercent(999, 1000) == 99);
    CHECK(InstallerDownloadPercent(1000, 1000) == 99);
    CHECK(InstallerDownloadPercent(2000, 1000) == 99);
    constexpr auto maxBytes = std::numeric_limits<std::size_t>::max();
    CHECK(InstallerDownloadPercent((maxBytes - 1) / 2, maxBytes - 1) == 50);
    CHECK(InstallerDownloadPercent(maxBytes, maxBytes) == 99);
    CHECK(IsNewerVersion("v0.4.0", "0.3.0"));
    CHECK(IsNewerVersion("v0.10.0", "0.9.99"));
    CHECK(IsNewerVersion("1.0.0", "0.99.99"));
    CHECK(!IsNewerVersion("v0.4.0", "0.4.0"));
    CHECK(!IsNewerVersion("v0.3.9", "0.4.0"));
    CHECK(IsNewerVersion("1.0.0", "1.0.0-rc.1"));
    CHECK(IsNewerVersion("1.0.0-rc.10", "1.0.0-rc.2"));
    CHECK(!IsNewerVersion("1.0.0+build.2", "1.0.0+build.1"));
    for (const auto bad : {"", "v1.2", "1.2.3.4", "01.2.3", "1.2.-3",
                           "1.2.3-01", "1.2.3+", "1.2.3-", "1.2.3 garbage"}) {
        CHECK(!IsNewerVersion(bad, "0.1.0"));
        CHECK(!IsNewerVersion("2.0.0", bad));
    }
    CHECK(IsNewerVersion("999999999999999999999999.0.0", "2.0.0"));
    const std::string url = "https://github.com/Token-Rap/OffWork/releases/download/v0.4.0/OffWork-Setup.exe";
    const std::string json = R"({"body":"tag_name: fake, \"assets\": [] \u4e2d", "tag_name":"v0.4.0",
      "draft":false,"prerelease":false,"assets":[
      {"name":"OffWork.exe","browser_download_url":"ignored"},
      {"browser_download_url":")" + url + R"(","name":"OffWork-Setup.exe"}]})";
    auto release = ParseReleaseInfo(json);
    CHECK(release && release->tag == "v0.4.0" && release->installerUrl == url);
    auto legacyJson = json;
    legacyJson.replace(legacyJson.find("Token-Rap"), std::string("Token-Rap").size(), "alertform");
    const auto legacyRelease = ParseReleaseInfo(legacyJson);
    CHECK(legacyRelease && legacyRelease->installerUrl ==
        "https://github.com/alertform/OffWork/releases/download/v0.4.0/OffWork-Setup.exe");
    auto evilJson = json;
    evilJson.replace(evilJson.find("github.com/"), std::string("github.com/").size(), "github.com.evil.test/");
    CHECK(!ParseReleaseInfo(evilJson));
    CHECK(!ParseReleaseInfo("{\"tag_name\":\"v0.4.0\",\"assets\":[]}"));
    CHECK(!ParseReleaseInfo("{\"tag_name\":\"v0.4.0\"}"));
    CHECK(!ParseReleaseInfo(json + "garbage"));
    CHECK(!ParseReleaseInfo(json.substr(0, json.size() - 1)));
    for (const auto from : {std::string("https://github.com/Token-Rap/OffWork/"),
                            std::string("OffWork-Setup.exe"), std::string("v0.4.0")}) {
        auto changed = json;
        changed.replace(changed.find(from), from.size(), "untrusted");
        CHECK(!ParseReleaseInfo(changed));
    }
    for (const auto flag : {"draft", "prerelease"}) {
        auto changed = json;
        const auto pos = changed.find(std::string("\"") + flag + "\":false");
        changed.replace(pos, std::string(flag).size() + 8, std::string("\"") + flag + "\":true");
        CHECK(!ParseReleaseInfo(changed));
    }
    CHECK(!ParseReleaseInfo(R"({"tag_name":"v0.4.0","tag_name":"v9.0.0","assets":[]})"));
    CHECK(!ParseReleaseInfo(std::string(80, '[') + std::string(80, ']')));
    std::cout << "Semver, release JSON/Location and check backoff tests passed.\n";
}

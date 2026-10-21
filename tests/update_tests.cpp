#include "check.h"
#include "update_info.h"

#include <iostream>
#include <limits>
#include <string>

int main() {
    using namespace offwork;
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
    const std::string url = "https://github.com/alertform/OffWork/releases/download/v0.4.0/OffWork-Setup.exe";
    const std::string json = R"({"body":"tag_name: fake, \"assets\": [] \u4e2d", "tag_name":"v0.4.0",
      "draft":false,"prerelease":false,"assets":[
      {"name":"OffWork.exe","browser_download_url":"ignored"},
      {"browser_download_url":")" + url + R"(","name":"OffWork-Setup.exe"}]})";
    auto release = ParseReleaseInfo(json);
    CHECK(release && release->tag == "v0.4.0" && release->installerUrl == url);
    CHECK(!ParseReleaseInfo("{\"tag_name\":\"v0.4.0\",\"assets\":[]}"));
    CHECK(!ParseReleaseInfo("{\"tag_name\":\"v0.4.0\"}"));
    CHECK(!ParseReleaseInfo(json + "garbage"));
    CHECK(!ParseReleaseInfo(json.substr(0, json.size() - 1)));
    for (const auto from : {std::string("https://github.com/alertform/OffWork/"),
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
    std::cout << "Semver and release JSON tests passed.\n";
}

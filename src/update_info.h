#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace offwork {

struct ReleaseInfo {
    std::string tag;
    std::string installerUrl;
};

// Invalid versions compare as no update. Accepts an optional leading 'v'.
bool IsNewerVersion(std::string_view candidate, std::string_view current);
// Only a stable release with the exact setup asset and official HTTPS URL.
std::optional<ReleaseInfo> ParseReleaseInfo(std::string_view json);

// -1 means an unknown/zero content length. Reserve 100 for a fully validated,
// flushed installer, even if all advertised bytes have already arrived.
int InstallerDownloadPercent(std::size_t received, std::optional<std::size_t> total);

}  // namespace offwork

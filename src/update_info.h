#pragma once

#include <cstddef>
#include <cstdint>
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
// Exact official repository Location, absolute HTTPS or root-relative, and a
// stable vX.Y.Z tag. No URL decoding, suffixes, queries or fragments.
std::optional<ReleaseInfo> ParseReleaseLocation(std::string_view location);

class UpdateCheckSchedule {
public:
    bool Due(std::uint64_t now) const { return now >= nextCheck_; }
    void Complete(std::uint64_t now, bool succeeded);
private:
    std::uint64_t nextCheck_ = 0;
    unsigned failures_ = 0;
};

// -1 means an unknown/zero content length. Reserve 100 for a fully validated,
// flushed installer, even if all advertised bytes have already arrived.
int InstallerDownloadPercent(std::size_t received, std::optional<std::size_t> total);

}  // namespace offwork

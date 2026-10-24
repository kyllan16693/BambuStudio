#ifndef slic3r_AppImageUpdater_hpp_
#define slic3r_AppImageUpdater_hpp_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace Slic3r::AppImageUpdater {

struct Release {
    std::string version;
    std::string notes;
    std::string url;
    std::string sha256;
    std::uint64_t size = 0;
};

// Repository and asset are build-time choices, never taken from release content.
std::optional<Release> select_release(const std::string &json, const std::string &repository,
                                     const std::string &asset, const std::string &current_version,
                                     bool include_prereleases);
std::string current_appimage();
// Invoked as /bin/sh -c <script> name <old-pid> <absolute-AppImage-path>.
const char *restart_script();

// Stages beside the installed image, then verifies and atomically replaces it.
// Until commit succeeds the installed image is untouched. Destruction removes
// incomplete downloads. The previous image is kept at <image>.previous.
class StagedUpdate {
public:
    StagedUpdate(const std::string &image, const Release &release);
    ~StagedUpdate();
    StagedUpdate(const StagedUpdate &) = delete;
    StagedUpdate &operator=(const StagedUpdate &) = delete;
    void append(const char *data, std::size_t size);
    void commit();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

void download_and_install(const std::string &image, const Release &release,
                          const std::atomic<bool> &cancel, std::atomic<int> &percent);

} // namespace Slic3r::AppImageUpdater
#endif

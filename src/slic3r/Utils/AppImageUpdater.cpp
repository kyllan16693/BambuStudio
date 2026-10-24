#include "AppImageUpdater.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <vector>
#include <regex>

#include <curl/curl.h>
#include <openssl/evp.h>
#include <nlohmann/json.hpp>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Slic3r::AppImageUpdater {
namespace {
constexpr std::uint64_t max_image_size = 2ULL * 1024 * 1024 * 1024;

[[noreturn]] void system_error(const char *operation)
{
    throw std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
}

using Version = std::array<unsigned long, 4>;
Version parse_version(const std::string &value)
{
    static const std::regex pattern(R"(^v?([0-9]{1,5})\.([0-9]{1,5})\.([0-9]{1,5})\.([0-9]{1,5})$)");
    std::smatch match;
    if (!std::regex_match(value, match, pattern))
        throw std::runtime_error("Invalid update version");
    return {std::stoul(match[1]), std::stoul(match[2]), std::stoul(match[3]), std::stoul(match[4])};
}

void validate_release(const Release &release)
{
    if (release.size < 64 || release.size > max_image_size ||
        !std::regex_match(release.sha256, std::regex("[0-9a-f]{64}")))
        throw std::runtime_error("The release has no valid SHA-256 digest or size");
}

bool same_file(const struct stat &a, const struct stat &b)
{
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size &&
           a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec;
}

void validate_image(int fd)
{
    unsigned char header[64] = {};
    if (pread(fd, header, sizeof(header), 0) != sizeof(header) ||
        std::memcmp(header, "\177ELF", 4) != 0 || header[4] != 2 || header[5] != 1 ||
        header[8] != 'A' || header[9] != 'I' || header[10] != 2 ||
        header[18] != 62 || header[19] != 0)
        throw std::runtime_error("The update is not an x86-64 type-2 AppImage");
}
} // namespace

std::optional<Release> select_release(const std::string &body, const std::string &repository,
                                     const std::string &asset_name, const std::string &current_version,
                                     bool include_prereleases)
{
    if (!std::regex_match(repository, std::regex(R"([A-Za-z0-9_-]+/[A-Za-z0-9_.-]+)")) ||
        !std::regex_match(asset_name, std::regex(R"([A-Za-z0-9_.-]+\.AppImage)")))
        throw std::runtime_error("Invalid updater build configuration");
    const auto releases = nlohmann::json::parse(body);
    if (!releases.is_array())
        throw std::runtime_error("Unexpected release service response");
    auto best_version = parse_version(current_version);
    std::optional<Release> best;
    for (const auto &entry : releases) {
        if (entry.value("draft", false) || (!include_prereleases && entry.value("prerelease", false)))
            continue;
        const std::string tag = entry.value("tag_name", "");
        Version version;
        try { version = parse_version(tag); } catch (const std::exception &) { continue; }
        if (version <= best_version || !entry.contains("assets") || !entry["assets"].is_array())
            continue;
        for (const auto &asset : entry["assets"]) {
            if (asset.value("name", "") != asset_name)
                continue;
            Release release;
            release.version = tag[0] == 'v' ? tag.substr(1) : tag;
            release.notes = entry.contains("body") && entry["body"].is_string() ? entry["body"].get<std::string>() : "";
            release.url = asset.at("browser_download_url").get<std::string>();
            // Prevent selecting arbitrary URLs, even if metadata is malformed.
            const std::string expected = "https://github.com/" + repository + "/releases/download/" + tag + "/" + asset_name;
            if (release.url != expected)
                throw std::runtime_error("The update URL does not belong to the configured repository");
            const std::string digest = asset.value("digest", "");
            if (digest.compare(0, 7, "sha256:") != 0)
                throw std::runtime_error("The release has no SHA-256 digest");
            release.sha256 = digest.substr(7);
            if (!asset.at("size").is_number_unsigned())
                throw std::runtime_error("Invalid update size");
            release.size = asset["size"].get<std::uint64_t>();
            validate_release(release);
            best = std::move(release);
            best_version = version;
            break;
        }
    }
    return best;
}

const char *restart_script()
{
    return R"(i=0; while kill -0 "$1" 2>/dev/null; do
    i=$((i+1)); [ "$i" -lt 120 ] || exit 1; sleep 1
    done
    unset APPIMAGE APPDIR ARGV0 OWD LD_LIBRARY_PATH LD_PRELOAD
    exec "$2")";
}

std::string current_appimage()
{
    const char *value = std::getenv("APPIMAGE");
    if (!value || !*value || std::getenv("FLATPAK_ID"))
        return {};
    char *resolved = realpath(value, nullptr);
    if (!resolved)
        return {};
    std::string image(resolved);
    std::free(resolved);
    return image;
}

struct StagedUpdate::Impl {
    std::string image, directory, staged, backup;
    Release release;
    int original_fd = -1, fd = -1;
    struct stat original = {};
    std::uint64_t received = 0;
    EVP_MD_CTX *hash = nullptr;
    bool committed = false;

    ~Impl()
    {
        if (fd >= 0) close(fd);
        if (original_fd >= 0) close(original_fd);
        if (hash) EVP_MD_CTX_free(hash);
        if (!staged.empty()) unlink(staged.c_str());
        if (!backup.empty()) unlink(backup.c_str());
        if (!directory.empty()) rmdir(directory.c_str());
    }
};

StagedUpdate::StagedUpdate(const std::string &image, const Release &release) : impl(new Impl)
{
    validate_release(release);
    impl->image = image;
    impl->release = release;
    impl->original_fd = open(image.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (impl->original_fd < 0) system_error("Open installed AppImage");
    if (fstat(impl->original_fd, &impl->original) != 0) system_error("Inspect installed AppImage");
    if (!S_ISREG(impl->original.st_mode) || impl->original.st_uid != geteuid() ||
        (impl->original.st_mode & (S_ISUID | S_ISGID)) || !(impl->original.st_mode & S_IWUSR) ||
        access(image.c_str(), W_OK) != 0)
        throw std::runtime_error("The installed AppImage must be a writable file owned by you");
    validate_image(impl->original_fd);
    if (flock(impl->original_fd, LOCK_EX | LOCK_NB) != 0)
        throw std::runtime_error("Another update is already in progress");
    std::string pattern = image + ".update-XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    if (!mkdtemp(name.data())) system_error("Create update directory beside AppImage");
    impl->directory = name.data();
    impl->staged = impl->directory + "/new.AppImage";
    impl->backup = impl->directory + "/previous.AppImage";
    impl->fd = open(impl->staged.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (impl->fd < 0) system_error("Create update file");
    impl->hash = EVP_MD_CTX_new();
    if (!impl->hash || EVP_DigestInit_ex(impl->hash, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("Could not initialize SHA-256 verification");
}

StagedUpdate::~StagedUpdate() = default;

void StagedUpdate::append(const char *data, std::size_t size)
{
    if (impl->committed || size > impl->release.size - impl->received)
        throw std::runtime_error("The download exceeds the release size");
    const auto total = size;
    const char *cursor = data;
    while (size) {
        const auto written = write(impl->fd, cursor, size);
        if (written < 0) {
            if (errno == EINTR) continue;
            system_error("Write update file");
        }
        if (written == 0) throw std::runtime_error("Could not write update file");
        cursor += written;
        size -= written;
    }
    if (EVP_DigestUpdate(impl->hash, data, total) != 1)
        throw std::runtime_error("Could not verify update data");
    impl->received += total;
}

void StagedUpdate::commit()
{
    if (impl->committed || impl->received != impl->release.size)
        throw std::runtime_error("The update download is incomplete");
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned length = 0;
    if (EVP_DigestFinal_ex(impl->hash, digest, &length) != 1 || length != 32)
        throw std::runtime_error("Could not finish SHA-256 verification");
    std::string hex;
    for (unsigned i = 0; i < length; ++i) {
        hex += "0123456789abcdef"[digest[i] >> 4];
        hex += "0123456789abcdef"[digest[i] & 15];
    }
    if (hex != impl->release.sha256)
        throw std::runtime_error("The update failed SHA-256 verification");
    validate_image(impl->fd);
    if (fchmod(impl->fd, (impl->original.st_mode & 0777) | S_IXUSR) != 0 || fsync(impl->fd) != 0)
        system_error("Save verified update");
    struct stat current = {};
    if (lstat(impl->image.c_str(), &current) != 0 || !same_file(impl->original, current))
        throw std::runtime_error("The installed AppImage changed during the download; retry the update");
    const std::string previous = impl->image + ".previous";
    struct stat backup_stat = {};
    if (lstat(previous.c_str(), &backup_stat) == 0) {
        if (!S_ISREG(backup_stat.st_mode) || backup_stat.st_uid != geteuid())
            throw std::runtime_error("The previous-version backup path is not a file owned by you");
    } else if (errno != ENOENT) system_error("Inspect previous-version backup");
    if (link(impl->image.c_str(), impl->backup.c_str()) != 0 ||
        rename(impl->backup.c_str(), previous.c_str()) != 0)
        system_error("Keep previous AppImage");
    if (rename(impl->staged.c_str(), impl->image.c_str()) != 0)
        system_error("Install verified AppImage");
    impl->committed = true;
    // A successful rename is the commit point. Do not report a failed install
    // after it: the new image is already in place even if directory fsync fails.
    const auto slash = impl->image.find_last_of('/');
    const auto parent = slash == std::string::npos ? "." : impl->image.substr(0, slash);
    const int parent_fd = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (parent_fd >= 0) { fsync(parent_fd); close(parent_fd); }
}

void download_and_install(const std::string &image, const Release &release,
                          const std::atomic<bool> &cancel, std::atomic<int> &percent)
{
    StagedUpdate staged(image, release);
    struct Context {
        StagedUpdate &staged;
        const Release &release;
        const std::atomic<bool> &cancel;
        std::atomic<int> &percent;
        std::exception_ptr error;
    } context{staged, release, cancel, percent, {}};
    using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
    CurlHandle curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("Could not initialize update download");
    curl_easy_setopt(curl.get(), CURLOPT_URL, release.url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS));
    curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS));
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 1800L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "BambuStudio-AppImage-Updater");
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &context);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, +[](char *data, size_t size, size_t count, void *opaque) -> size_t {
        auto &ctx = *static_cast<Context *>(opaque);
        if (ctx.cancel.load()) return 0;
        try { ctx.staged.append(data, size * count); }
        catch (...) { ctx.error = std::current_exception(); return 0; }
        return size * count;
    });
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &context);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, +[](void *opaque, curl_off_t, curl_off_t now, curl_off_t, curl_off_t) -> int {
        auto &ctx = *static_cast<Context *>(opaque);
        ctx.percent = static_cast<int>(std::min<std::uint64_t>(99, std::max<curl_off_t>(0, now) * 100ULL / ctx.release.size));
        return ctx.cancel.load() ? 1 : 0;
    });
    const auto result = curl_easy_perform(curl.get());
    if (cancel.load()) throw std::runtime_error("Update cancelled");
    if (context.error) std::rethrow_exception(context.error);
    if (result != CURLE_OK) throw std::runtime_error(std::string("Download failed: ") + curl_easy_strerror(result));
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status != 200) throw std::runtime_error("Unexpected update download response");
    if (cancel.load()) throw std::runtime_error("Update cancelled");
    staged.commit();
    percent = 100;
}

} // namespace Slic3r::AppImageUpdater

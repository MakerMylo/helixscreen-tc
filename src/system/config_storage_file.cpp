// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_error_reporting.h"

#include "config_storage.h"
#include "helix_fs.h"
#include "system/helix_paths.h"
#include "text_io.h"

#if !defined(HELIX_SPLASH_ONLY) && !defined(HELIX_WATCHDOG)
#include "system/telemetry_manager.h"
#define CONFIG_RECORD_ERROR(...) TelemetryManager::instance().record_error(__VA_ARGS__)
#else
#define CONFIG_RECORD_ERROR(...) ((void)0)
#endif

#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace hfs = helix::fs;

namespace helix {

namespace {

std::string errno_reason(int err) {
    switch (err) {
    case ENOSPC:
        return "disk full";
    case EROFS:
        return "read-only filesystem";
    case EACCES:
        return "permission denied";
    default:
        return strerror(err);
    }
}

class FileConfigStorage : public ConfigStorage {
  public:
    explicit FileConfigStorage(std::string path) : path_(std::move(path)) {}

    std::optional<std::string> load(std::string& read_error) override {
        struct stat st;
        if (stat(path_.c_str(), &st) != 0) {
            return std::nullopt; // absent — first boot
        }
        std::optional<std::string> text = helix::text_io::read_file(path_);
        if (!text) {
            // Present but unreadable (e.g. permission denied) — distinct
            // from "absent" so Config::init() can route this into
            // corrupt-preserve + backup-restore instead of silently
            // treating a locked-down existing config as first-boot.
            int err = errno;
            read_error = fmt::format("failed to open {} for reading: {}", path_, errno_reason(err));
            return std::nullopt;
        }
        return text;
    }

    bool store(const std::string& bytes) override {
        // Atomic save: symlink-resolve, write .tmp, fsync, rename, fsync
        // parent dir. Moved verbatim from Config::save() (see #943: without
        // the fsyncs a power cycle can leave settings.json empty on
        // flash-backed filesystems).
        std::string target_path = helix::paths::write_target(path_);

        std::string tmp_path = target_path + ".tmp";
        {
            helix::text_io::File o = helix::text_io::open_file(tmp_path, "wb");
            if (!o) {
                std::string reason = errno_reason(errno);
                NOTIFY_ERROR("Could not save settings: {}", reason);
                LOG_ERROR_INTERNAL("Failed to open temp file for writing: {} ({})", tmp_path,
                                   reason);
                CONFIG_RECORD_ERROR("file_io", "config_write_failed",
                                    fmt::format("open failed: {}", reason));
                return false;
            }

            const bool wrote = helix::text_io::write_all(o.get(), bytes);
            if (!helix::text_io::close(o) || !wrote) {
                std::string reason = errno_reason(errno);
                NOTIFY_ERROR("Failed to save settings: {}", reason);
                LOG_ERROR_INTERNAL("Failed to write config to {}: {}", tmp_path, reason);
                CONFIG_RECORD_ERROR("file_io", "config_write_failed",
                                    fmt::format("write error: {}", reason));
                std::remove(tmp_path.c_str());
                return false;
            }
        }

        {
            int fd = ::open(tmp_path.c_str(), O_RDONLY);
            if (fd >= 0) {
                (void)::fsync(fd);
                ::close(fd);
            }
        }

        if (std::rename(tmp_path.c_str(), target_path.c_str()) != 0) {
            NOTIFY_ERROR("Failed to save configuration file");
            LOG_ERROR_INTERNAL("Failed to rename temp file '{}' to '{}': {}", tmp_path, target_path,
                               strerror(errno));
            CONFIG_RECORD_ERROR("file_io", "config_write_failed",
                                fmt::format("rename failed: {}", strerror(errno)));
            std::remove(tmp_path.c_str());
            return false;
        }

        {
            std::string dir(hfs::parent_path(target_path));
            if (!dir.empty()) {
                int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
                if (dfd >= 0) {
                    (void)::fsync(dfd);
                    ::close(dfd);
                }
            }
        }

        // The rolling backup is Config::save()'s job, not the backend's —
        // whether a document is worth preserving is policy, not byte
        // movement.
        return true;
    }

    void preserve_corrupt() override {
        std::string corrupt_path = path_ + ".corrupt";
        std::rename(path_.c_str(), corrupt_path.c_str());
        spdlog::info("[ConfigStorage] Corrupt config saved to {}", corrupt_path);
    }

    bool read_only() override {
        // Write-probe, moved verbatim from Config::init() (lines 1340-1359).
        std::string probe_path = hfs::join_path(hfs::parent_path(path_), ".helix-write-probe");
        helix::text_io::File probe = helix::text_io::open_file(probe_path, "wb");
        if (!probe) {
            int err = errno;
            if (err == EROFS || err == EACCES) {
                spdlog::warn("[ConfigStorage] Read-only filesystem detected ({})", strerror(err));
                return true;
            }
            return false;
        }
        probe.reset();
        std::remove(probe_path.c_str());
        return false;
    }

    std::string describe() const override {
        return path_;
    }

  private:
    std::string path_;
};

} // namespace

std::unique_ptr<ConfigStorage> make_file_config_storage(const std::string& path) {
    return std::make_unique<FileConfigStorage>(path);
}

} // namespace helix

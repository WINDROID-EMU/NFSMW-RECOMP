#include "android_storage.h"
#include <android/log.h>
#include <vector>
#include <algorithm>

#define TAG "NFS-AndroidStorage"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

namespace rex::vfs::android {

std::filesystem::path AndroidStorage::internal_path_{};
std::filesystem::path AndroidStorage::external_path_{};

void AndroidStorage::Initialize(const std::string& internal_data_path,
                                const std::string& external_data_path) {
  internal_path_ = internal_data_path;
  external_path_ = external_data_path;
  LOGI("AndroidStorage initialized: internal=%s, external=%s",
       internal_path_.c_str(), external_path_.c_str());
}

std::filesystem::path AndroidStorage::GetInternalPath() {
  return internal_path_;
}

std::filesystem::path AndroidStorage::GetExternalPath() {
  return external_path_;
}

std::filesystem::path AndroidStorage::FindGameDataRoot() {
  std::error_code ec;
  std::vector<std::filesystem::path> search_dirs = {
      external_path_,
      internal_path_,
      "/sdcard/NFSMW",
      "/sdcard/Download/NFSMW",
      "/storage/emulated/0/NFSMW"
  };

  for (const auto& dir : search_dirs) {
    if (dir.empty() || !std::filesystem::is_directory(dir, ec)) {
      continue;
    }

    // 1. Look for extracted game_root folder
    auto game_root = dir / "game_root";
    if (std::filesystem::is_directory(game_root, ec)) {
      LOGI("Found extracted game_root at %s", game_root.c_str());
      return game_root;
    }

    // 2. Look for ISO files
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
      if (ec) break;
      if (!entry.is_regular_file(ec)) continue;

      std::string ext = entry.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
      if (ext == ".iso") {
        LOGI("Found game ISO at %s", entry.path().c_str());
        return entry.path();
      }
    }
  }

  LOGW("No game data found in searched directories. Defaulting to external path.");
  return external_path_ / "game_root";
}

void AndroidStorage::ConfigureAppPaths(rex::PathConfig& paths) {
  std::error_code ec;

  if (paths.game_data_root.empty()) {
    paths.game_data_root = FindGameDataRoot();
  }

  // Create user, cache, and save folders
  std::filesystem::path base_user = external_path_.empty() ? internal_path_ : external_path_;

  paths.user_data_root = base_user / "user";
  paths.cache_root = base_user / "cache";
  paths.metadata_root = base_user / "metadata";
  paths.update_data_root = base_user / "updates";
  paths.config_path = base_user / "nfsmw.toml";

  std::filesystem::create_directories(paths.user_data_root, ec);
  std::filesystem::create_directories(paths.cache_root, ec);
  std::filesystem::create_directories(paths.metadata_root, ec);
  std::filesystem::create_directories(paths.update_data_root, ec);

  LOGI("Paths configured:\n  Game: %s\n  User: %s\n  Cache: %s",
       paths.game_data_root.c_str(),
       paths.user_data_root.c_str(),
       paths.cache_root.c_str());
}

}  // namespace rex::vfs::android

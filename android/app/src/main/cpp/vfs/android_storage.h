#pragma once

#include <filesystem>
#include <string>
#include <rex/filesystem.h>
#include <rex/rex_app.h>

namespace rex::vfs::android {

class AndroidStorage {
 public:
  static void Initialize(const std::string& internal_data_path,
                         const std::string& external_data_path);

  static std::filesystem::path GetInternalPath();
  static std::filesystem::path GetExternalPath();
  static std::filesystem::path FindGameDataRoot();

  static void ConfigureAppPaths(rex::PathConfig& paths);

 private:
  static std::filesystem::path internal_path_;
  static std::filesystem::path external_path_;
};

}  // namespace rex::vfs::android

/**
 * @file        vulkan_driver_loader.h
 * @brief       Custom Vulkan driver loader via libadrenotools (Turnip/Mesa support)
 *
 * Attempts to replace the system libvulkan.so with the Mesa Turnip open-source
 * Adreno driver before the ReXGlue SDK initialises Vulkan. This gives us:
 *
 *   - Vulkan 1.3/1.4 on devices whose stock driver only reports 1.1
 *   - Full shaderInt64 / VK_KHR_buffer_device_address support
 *   - A single validated driver path instead of per-OEM driver quirks
 *
 * Usage:
 *   Call TryLoadCustomVulkanDriver() once, as early as possible in
 *   android_main(), before rex::cvar::Init() and before any Vulkan call.
 *
 * Driver installation:
 *   Place the Turnip driver SO (e.g. libvulkan_freedreno.so) inside the app's
 *   internal data directory at:
 *       <internalDataPath>/turnip/libvulkan_freedreno.so
 *
 *   The TitleActivity already writes the nfsmw.toml there; the same path is
 *   used here so the user only needs one accessible directory.
 *
 * Fallback:
 *   If libadrenotools.so is not found in nativeLibraryDir, or if the custom
 *   driver directory/file is absent, the function returns false and the stock
 *   system driver is used normally. No crash, no error — just a log line.
 *
 * @copyright   BSD-2-Clause (libadrenotools itself is BSD-2-Clause, Billy Laws)
 *              Project-specific glue: same licence as the rest of NFSMW-RECOMP.
 */

#pragma once

#include <android/log.h>
#include <android_native_app_glue.h>
#include <vulkan/vulkan.h>

#include <dlfcn.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <string>

#define ATAG "NFS-VkLoader"
#define ATLOG(...) __android_log_print(ANDROID_LOG_INFO,  ATAG, __VA_ARGS__)
#define ATERR(...) __android_log_print(ANDROID_LOG_ERROR, ATAG, __VA_ARGS__)
#define ATWRN(...) __android_log_print(ANDROID_LOG_WARN,  ATAG, __VA_ARGS__)

// ──────────────────────────────────────────────────────────────────────────────
// libadrenotools C API surface we actually use
// Declared here so we don't need the full adrenotools headers at build time
// when the lib is absent (header-only stub path).
// ──────────────────────────────────────────────────────────────────────────────
extern "C" {
// adrenotools feature flags (from include/adrenotools/priv.h)
enum {
    ADRENOTOOLS_DRIVER_CUSTOM           = 1 << 0,
    ADRENOTOOLS_DRIVER_FILE_REDIRECT    = 1 << 1,
    ADRENOTOOLS_DRIVER_GPU_MAPPING_IMPORT = 1 << 2,
};

// adrenotools_open_libvulkan signature
using PFN_adrenotools_open_libvulkan = void* (*)(
    int       dlopenMode,
    int       featureFlags,
    const char* tmpLibDir,
    const char* hookLibDir,
    const char* customDriverDir,
    const char* customDriverName,
    const char* fileRedirectDir);
}  // extern "C"

namespace nfsmw::android {

// Subdirectory inside internalDataPath where the user drops the Turnip SO.
static constexpr const char* kTurnipSubdir      = "turnip";
static constexpr const char* kTurnipDriverName  = "libvulkan_freedreno.so";

/**
 * @brief Checks whether a file exists and is readable.
 */
static inline bool FileExists(const std::string& path) {
    struct stat st{};
    return (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode));
}

/**
 * @brief Retrieves the native library directory of the running process.
 *
 * libadrenotools requires hookLibDir == getApplicationInfo().nativeLibraryDir.
 * We derive it from the path of libadrenotools.so itself once it is located,
 * so we never need to cross the JNI boundary just for a path.
 */
static std::string GetNativeLibraryDir(void* adrenotools_handle) {
    // dl_iterate_phdr is available in API 21+ but is heavyweight.
    // Simpler: query the SO's own path via dlinfo (available in API 21+).
    // Falls back to parsing /proc/self/maps if dlinfo is unavailable.
#if __ANDROID_API__ >= 21
    Dl_info info{};
    if (dladdr(adrenotools_handle, &info) && info.dli_fname) {
        std::string so_path = info.dli_fname;
        auto slash = so_path.rfind('/');
        if (slash != std::string::npos) {
            return so_path.substr(0, slash);
        }
    }
#endif
    return {};
}

/**
 * @brief Tries to load the custom Turnip Vulkan driver via libadrenotools.
 *
 * @param state  android_app* from android_main — used for internalDataPath.
 * @return true  if Turnip was successfully loaded (libvulkan.so is now Turnip).
 * @return false if adrenotools is absent, driver files are missing, or the
 *               load failed — the stock system driver will be used instead.
 */
inline bool TryLoadCustomVulkanDriver(struct android_app* state) {
    if (!state || !state->activity) {
        ATERR("TryLoadCustomVulkanDriver: null android_app, skipping");
        return false;
    }

    const char* internal_path =
        state->activity->internalDataPath ? state->activity->internalDataPath : "";

    if (internal_path[0] == '\0') {
        ATWRN("TryLoadCustomVulkanDriver: internalDataPath is empty, skipping");
        return false;
    }

    // ── 1. Locate the custom driver SO ────────────────────────────────────────
    std::string driver_dir  = std::string(internal_path) + "/" + kTurnipSubdir;
    std::string driver_path = driver_dir + "/" + kTurnipDriverName;

    if (!FileExists(driver_path)) {
        ATLOG("Turnip driver not found at '%s' — using stock Vulkan driver",
              driver_path.c_str());
        return false;
    }

    ATLOG("Turnip driver found: %s", driver_path.c_str());

    // ── 2. Load libadrenotools.so ─────────────────────────────────────────────
    // libadrenotools.so is packaged in the APK alongside libnfsmw.so.
    // We load it by name so the dynamic linker finds it in nativeLibraryDir.
    void* adrenotools = dlopen("libadrenotools.so", RTLD_NOW | RTLD_LOCAL);
    if (!adrenotools) {
        ATWRN("libadrenotools.so not found (%s) — using stock Vulkan driver",
              dlerror());
        return false;
    }

    auto open_libvulkan = reinterpret_cast<PFN_adrenotools_open_libvulkan>(
        dlsym(adrenotools, "adrenotools_open_libvulkan"));
    if (!open_libvulkan) {
        ATERR("adrenotools_open_libvulkan symbol missing (%s)", dlerror());
        dlclose(adrenotools);
        return false;
    }

    // ── 3. Resolve hookLibDir ─────────────────────────────────────────────────
    // MUST be nativeLibraryDir (where libnfsmw.so and libadrenotools.so live).
    std::string hook_lib_dir = GetNativeLibraryDir(adrenotools);
    if (hook_lib_dir.empty()) {
        ATERR("Could not determine nativeLibraryDir — skipping Turnip load");
        dlclose(adrenotools);
        return false;
    }

    ATLOG("hookLibDir resolved: %s", hook_lib_dir.c_str());
    ATLOG("customDriverDir:     %s", driver_dir.c_str());
    ATLOG("customDriverName:    %s", kTurnipDriverName);

    // ── 4. Call adrenotools_open_libvulkan ────────────────────────────────────
    // We request ADRENOTOOLS_DRIVER_CUSTOM only.
    // tmpLibDir is nullptr — we target API 29+ so memfd is always available.
    void* custom_vulkan = open_libvulkan(
        RTLD_NOW | RTLD_LOCAL,              // dlopenMode
        ADRENOTOOLS_DRIVER_CUSTOM,          // featureFlags
        nullptr,                            // tmpLibDir  (not needed on API ≥29)
        hook_lib_dir.c_str(),               // hookLibDir = nativeLibraryDir
        driver_dir.c_str(),                 // customDriverDir
        kTurnipDriverName,                  // customDriverName
        nullptr                             // fileRedirectDir (unused)
    );

    if (!custom_vulkan) {
        ATERR("adrenotools_open_libvulkan returned null — falling back to stock driver");
        dlclose(adrenotools);
        return false;
    }

    // ── 5. Verify we can resolve vkGetInstanceProcAddr in the new driver ──────
    auto vkGIPA = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(custom_vulkan, "vkGetInstanceProcAddr"));
    if (!vkGIPA) {
        ATERR("Turnip SO is missing vkGetInstanceProcAddr — not a valid Vulkan driver");
        dlclose(custom_vulkan);
        dlclose(adrenotools);
        return false;
    }

    ATLOG("Turnip/Mesa driver loaded successfully. vkGetInstanceProcAddr @ %p", vkGIPA);
    ATLOG("Stock libvulkan.so is now intercepted by Turnip.");

    // adrenotools_handle and custom_vulkan are intentionally kept open for the
    // lifetime of the process — the hook must remain active.
    // (No dlclose — this is expected and documented by libadrenotools.)
    return true;
}

}  // namespace nfsmw::android

#undef ATAG
#undef ATLOG
#undef ATERR
#undef ATWRN

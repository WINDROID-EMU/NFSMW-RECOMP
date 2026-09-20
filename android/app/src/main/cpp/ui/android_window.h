#pragma once

#include <rex/ui/window.h>
#include <rex/ui/surface_android.h>
#include <android/native_window.h>

namespace rex::ui {

class AndroidWindowedAppContext;

class AndroidWindow final : public Window {
 public:
  AndroidWindow(WindowedAppContext& app_context,
                const std::string_view title,
                uint32_t desired_logical_width,
                uint32_t desired_logical_height);
  ~AndroidWindow() override;

  void AttachNativeWindow(ANativeWindow* window);
  void DetachNativeWindow();
  void UpdateDimensions(uint32_t width, uint32_t height);
  void PaintFrame();

  static AndroidWindow* GetActiveWindow();

  ANativeWindow* native_window() const { return native_window_; }

 protected:
  bool OpenImpl() override;
  void RequestCloseImpl() override;
  std::unique_ptr<Surface> CreateSurfaceImpl(Surface::TypeFlags allowed_types) override;
  void RequestPaintImpl() override;

  void ApplyNewFullscreen() override;
  void ApplyNewTitle() override;

 private:
  ANativeWindow* native_window_ = nullptr;
  bool is_attached_ = false;
};

}  // namespace rex::ui

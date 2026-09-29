// NFSMW Recompiled - sonda de Vulkan
//
// Hace, sin el juego, lo primero que haria el juego con la GPU: cargar el
// libvulkan (el del sistema o Turnip, segun los ajustes android_gpu_*), crear
// la instancia y crear el dispositivo CON EMULACION DE GPU, que es donde el SDK
// comprueba que el driver da lo que la emulacion de Xenos necesita.
//
// Escribe un informe de texto en --nfsmw_informe_sonda, que la pantalla de
// inicio de la app lee y ensena. Sirve para responder, antes de tener el juego:
//
//   - se carga Turnip de verdad, o se queda el driver de Qualcomm?
//   - el SDK acepta este movil para emular la GPU?
//   - hay texturas BC (las del juego) o habra que descomprimirlas en CPU?

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/vulkan/device.h>
#include <rex/ui/vulkan/instance.h>

REXCVAR_DEFINE_STRING(nfsmw_informe_sonda, "", "Android",
                      "Where the Vulkan probe writes its report");

// Definidos por tools/parche_turnip.py en vulkan_instance.cpp, dentro de
// librexruntime.so, que esta en la linea de enlace de libmain.so.
REXCVAR_DECLARE(std::string, android_gpu_driver_name);
REXCVAR_DECLARE(std::string, android_gpu_driver_dir);

namespace {

using rex::ui::vulkan::VulkanDevice;
using rex::ui::vulkan::VulkanInstance;

std::string Version(uint32_t v) {
  return fmt::format("{}.{}.{}", VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v),
                     VK_API_VERSION_PATCH(v));
}

const char* NombreDriver(VkDriverId id) {
  switch (id) {
    case VK_DRIVER_ID_MESA_TURNIP:
      return "Mesa Turnip";
    case VK_DRIVER_ID_QUALCOMM_PROPRIETARY:
      return "Qualcomm (propietario)";
    case VK_DRIVER_ID_ARM_PROPRIETARY:
      return "ARM Mali (propietario)";
    case VK_DRIVER_ID_SAMSUNG_PROPRIETARY:
      return "Samsung Xclipse (propietario)";
    case VK_DRIVER_ID_IMAGINATION_PROPRIETARY:
      return "PowerVR (propietario)";
    default:
      return "otro";
  }
}

class Informe {
 public:
  void Linea(const std::string& texto) {
    REXLOG_INFO("[sonda] {}", texto);
    texto_ += texto;
    texto_ += '\n';
  }
  void Guardar(const std::string& ruta) const {
    if (ruta.empty()) {
      return;
    }
    std::ofstream f(ruta, std::ios::binary | std::ios::trunc);
    f << texto_;
  }

 private:
  std::string texto_;
};

void SondearDispositivo(Informe& inf, const VulkanInstance& instancia, VkPhysicalDevice fisico) {
  const auto& ifn = instancia.functions();

  VkPhysicalDeviceProperties props{};
  ifn.vkGetPhysicalDeviceProperties(fisico, &props);
  inf.Linea(fmt::format("GPU: {}  (vendor 0x{:04X}, device 0x{:08X})", props.deviceName,
                        props.vendorID, props.deviceID));
  inf.Linea(fmt::format("Vulkan del dispositivo: {}", Version(props.apiVersion)));

  // Que driver es de verdad. Es la respuesta a "se ha cargado Turnip?".
  if (props.apiVersion >= VK_MAKE_API_VERSION(0, 1, 2, 0) && ifn.vkGetPhysicalDeviceProperties2) {
    VkPhysicalDeviceDriverProperties driver{};
    driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &driver;
    ifn.vkGetPhysicalDeviceProperties2(fisico, &props2);
    inf.Linea(fmt::format("Driver: {}  [{}]  {} {}", NombreDriver(driver.driverID),
                          static_cast<int>(driver.driverID), driver.driverName,
                          driver.driverInfo));
  } else {
    inf.Linea("Driver: sin VkPhysicalDeviceDriverProperties (Vulkan < 1.2)");
  }

  // Texturas BC: las usa el juego. Sin ellas el SDK las descomprime en CPU.
  const std::pair<VkFormat, const char*> formatos[] = {
      {VK_FORMAT_BC1_RGBA_UNORM_BLOCK, "BC1"},
      {VK_FORMAT_BC2_UNORM_BLOCK, "BC2"},
      {VK_FORMAT_BC3_UNORM_BLOCK, "BC3"},
      {VK_FORMAT_BC5_UNORM_BLOCK, "BC5"},
  };
  std::string bc;
  for (const auto& [formato, nombre] : formatos) {
    VkFormatProperties fp{};
    ifn.vkGetPhysicalDeviceFormatProperties(fisico, formato, &fp);
    const bool ok = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
    bc += fmt::format("{}={} ", nombre, ok ? "si" : "NO");
  }
  inf.Linea("Texturas comprimidas: " + bc);

  // Crear el dispositivo como lo crea el juego: con emulacion de GPU. Si el SDK
  // lo rechaza, el motivo queda en el log.
  auto dispositivo = VulkanDevice::CreateIfSupported(&instancia, fisico,
                                                     /*with_gpu_emulation=*/true,
                                                     /*with_swapchain=*/true);
  if (!dispositivo) {
    inf.Linea("RESULTADO: el SDK NO acepta esta GPU para emular la Xenos (motivo en el log)");
    return;
  }
  inf.Linea("RESULTADO: el SDK acepta esta GPU para emular la Xenos");

  const auto& p = dispositivo->properties();
  const std::pair<bool, const char*> rasgos[] = {
      {p.fragmentStoresAndAtomics, "fragmentStoresAndAtomics"},
      {p.vertexPipelineStoresAndAtomics, "vertexPipelineStoresAndAtomics"},
      {p.independentBlend, "independentBlend"},
      {p.geometryShader, "geometryShader"},
      {p.tessellationShader, "tessellationShader"},
      {p.sampleRateShading, "sampleRateShading"},
      {p.depthClamp, "depthClamp"},
      {p.fillModeNonSolid, "fillModeNonSolid"},
      {p.samplerAnisotropy, "samplerAnisotropy"},
      {p.shaderClipDistance, "shaderClipDistance"},
      {p.fullDrawIndexUint32, "fullDrawIndexUint32"},
      {p.fragmentShaderPixelInterlock, "fragmentShaderPixelInterlock"},
      {p.shaderDemoteToHelperInvocation, "shaderDemoteToHelperInvocation"},
      {p.dynamicRendering, "dynamicRendering"},
  };
  for (const auto& [ok, nombre] : rasgos) {
    inf.Linea(fmt::format("  {:3} {}", ok ? "si" : "NO", nombre));
  }
}

}  // namespace

int NfsmwSondaVulkan(int argc, char** argv) {
  rex::cvar::Init(argc, argv);
  rex::cvar::ApplyEnvironment();
  rex::InitLoggingEarly();

  Informe inf;
  inf.Linea("NFSMW Recompiled - sonda de Vulkan");
  const std::string& driver = REXCVAR_GET(android_gpu_driver_name);
  inf.Linea(driver.empty() ? "Driver pedido: el del sistema"
                           : fmt::format("Driver pedido: {} (en {})", driver,
                                         REXCVAR_GET(android_gpu_driver_dir)));

  auto instancia = VulkanInstance::Create(/*with_surface=*/true, /*try_enable_validation=*/false);
  if (!instancia) {
    inf.Linea("RESULTADO: no se pudo crear la instancia de Vulkan (ver log)");
    inf.Guardar(REXCVAR_GET(nfsmw_informe_sonda));
    return 1;
  }
  inf.Linea(fmt::format("Instancia Vulkan: {}", Version(instancia->api_version())));

  std::vector<VkPhysicalDevice> fisicos;
  instancia->EnumeratePhysicalDevices(fisicos);
  if (fisicos.empty()) {
    inf.Linea("RESULTADO: el driver no devuelve ninguna GPU. Con Turnip suele ser que los hooks "
              "de adrenotools no estan en nativeLibraryDir");
  }
  for (VkPhysicalDevice fisico : fisicos) {
    SondearDispositivo(inf, *instancia, fisico);
  }

  inf.Guardar(REXCVAR_GET(nfsmw_informe_sonda));
  return 0;
}

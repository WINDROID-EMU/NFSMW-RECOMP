#pragma once

#include <cstdint>
#include <string_view>

namespace rex::graphics {

// Phase-1 PM4 -> Plume intermediate representation. It deliberately does not
// own a Plume command list yet: the legacy Xenos backend remains the execution
// authority while this IR proves packet/state coverage and provides a stable
// seam for the real Plume emitter.
class Pm4PlumeTranspiler final {
 public:
  enum class PacketKind {
    kOther,
    kSetState,
    kSetShader,
    kDraw,
    kSwap,
  };

  struct Stats {
    uint64_t packets_seen = 0;
    uint64_t state_packets = 0;
    uint64_t shader_packets = 0;
    uint64_t draw_packets = 0;
    uint64_t swap_packets = 0;
    uint64_t fallback_packets = 0;
  };

  void SetEnabled(bool enabled) { enabled_ = enabled; }
  bool enabled() const { return enabled_; }

  PacketKind ObservePacket(uint32_t opcode, uint32_t count);
  void ObserveRegisterWrite(uint32_t index, uint32_t value);
  void ObserveShaderLoad(uint32_t shader_type, uint32_t guest_address,
                         uint32_t dword_count);
  void ObserveDraw(std::string_view opcode_name, uint32_t primitive_type,
                   uint32_t index_count, bool indexed);

  const Stats& stats() const { return stats_; }
  PacketKind last_packet_kind() const { return last_packet_kind_; }
  uint32_t last_register_index() const { return last_register_index_; }
  uint32_t last_register_value() const { return last_register_value_; }
  uint32_t last_shader_address() const { return last_shader_address_; }
  uint32_t last_shader_dwords() const { return last_shader_dwords_; }
  uint32_t last_draw_indices() const { return last_draw_indices_; }

 private:
  bool enabled_ = false;
  Stats stats_;
  uint32_t last_register_index_ = 0;
  uint32_t last_register_value_ = 0;
  uint32_t last_shader_address_ = 0;
  uint32_t last_shader_dwords_ = 0;
  uint32_t last_draw_indices_ = 0;
  PacketKind last_packet_kind_ = PacketKind::kOther;
};

}  // namespace rex::graphics

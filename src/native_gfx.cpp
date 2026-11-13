// Kamui's graphics calls, replaced natively. The vertex-buffer send (0x8C045120) hands vertex lists
// straight to the renderer's parameter stream and writes textures into texture memory; the PVR
// register accessors keep the values the renderer reads. No DMA or PVR register is touched. The
// originals' guest cycles, interrupt polls and interrupts are kept so the rest of the game sees the
// same timing.
#include "native_gfx.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "dream/runtime/holly/intc.h"
#include "dream/runtime/mem/dc_memory.h"
#include "dream/runtime/pvr/core.h"
#include "dream/runtime/sh4/abi.h"
#include "dream/runtime/sh4/cost.h"
#include "dream/runtime/system.h"
#include "dreamcomp/hook.h"
#include "native_dev.h"

namespace dream::gen {
void fn_8c06f740(dream::sh4::Ctx& c, dream::Memory& m);  // Kamui: TA list init
}

namespace rez {
namespace {

dream::System* g_sys = nullptr;

using Core = dream::pvr::Core;
using Spg = dream::pvr::Spg;

// The PVR register values Kamui's register accessor writes, in the layout the renderer reads
// (Core::settings). No device is behind them: what a write did on hardware (start a render, open a
// list, reset the TA) is done here.
std::array<std::uint32_t, 0x2000 / 4> g_regs{};
// Each TA context's list words since its list init: what the next render of that context draws.
// Handed to the renderer without a copy.
std::unordered_map<std::uint32_t, dream::pvr::Stream> g_streams;
std::uint32_t g_list_ctx = 0;    // the context whose list init is running
std::uint32_t g_render_ctx = 0;  // the context Kamui's render start was called with
int g_render_event = -1;
std::uint64_t g_frame_swaps = 0;  // FB_R_SOF1 writes: pictures the game has shown

constexpr std::uint32_t kKamuiVertexBufDescPtr = 0x8C121050u;
constexpr float kUiOverlayMinDepth = 2.0f;
constexpr std::uint32_t kMainRamBytes = 0x01000000u;
constexpr std::uint32_t kMaxUiTagBytes = 0x00100000u;
constexpr std::uint32_t kTaChunkBytes = 32u;
constexpr std::uint32_t kTaDoubleChunkBytes = 64u;
constexpr std::uint32_t kColorFloating = 1u;
constexpr std::uint32_t kColorIntensity = 2u;
constexpr std::uint32_t kParaEndOfList = 0u;
constexpr std::uint32_t kParaPolygonOrModVol = 4u;
constexpr std::uint32_t kParaSprite = 5u;
constexpr std::uint32_t kParaExternal = 6u;
constexpr std::uint32_t kParaVertex = 7u;

int g_ui_tag_depth = 0;
bool g_ui_tag_valid = false;
std::array<std::uint32_t, 5> g_ui_tag_before{};

bool spg_reg(std::uint32_t off) {
    const std::uint32_t addr = Core::kRegBase + off;
    return (addr >= Spg::kRegLo && addr < Spg::kRegHi) || addr == Spg::kStatus;
}

// Renders the context Kamui's render start named; the render-done interrupts follow after the
// renderer model's render time.
void start_render() {
    const dream::pvr::RenderWords words = g_streams[g_render_ctx].share();
    if (g_sys->pvr_core->on_render)
        g_sys->pvr_core->on_render(words);
    const std::uint64_t cycles = g_sys->pvr_core->render_base_cycles +
                                 g_sys->pvr_core->render_cycles_per_word * words->size();
    g_sys->sched.request(g_render_event, cycles);
    g_render_ctx = 0;
}

void settings_write(std::uint32_t off, std::uint32_t value) {
    if (spg_reg(off)) {
        g_sys->spg.write(Core::kRegBase + off, value, 4);
        return;
    }
    switch (off) {
        case Core::kId:
        case Core::kRevision:
        case Core::kTaYuvTexCnt:
            return;
        case Core::kTaYuvTexBase:
        case Core::kTaYuvTexCtrl:
            g_regs[off >> 2] = value;
            return;
        case Core::kStartRender:
            start_render();
            return;
        case Core::kTaListInit:  // the context's lists start again; the TA's pointers reset
            g_ui_tag_depth = 0;
            g_ui_tag_valid = false;
            if (value >> 31) {
                g_streams[g_list_ctx].clear();
                g_regs[Core::kTaNextOpb >> 2] = g_regs[Core::kTaNextOpbInit >> 2];
                g_regs[Core::kTaItpCurrent >> 2] = g_regs[Core::kTaIspBase >> 2];
            }
            return;
        case Core::kTaListCont:
            return;
        case Core::kFbRCtrl: {
            const bool changed = ((g_regs[off >> 2] ^ value) >> 23) & 1u;
            g_regs[off >> 2] = value;
            if (changed)
                g_sys->spg.set_vclk_div((value >> 23) & 1u);
            return;
        }
        case Core::kFbRSof1:
            g_regs[off >> 2] = value & 0x00FFFFFCu;
            ++g_frame_swaps;
            return;
        case Core::kFbRSof2:
            g_regs[off >> 2] = value & 0x00FFFFFCu;
            return;
        case 0x060:  // FB_W_SOF1
        case 0x064:  // FB_W_SOF2
            g_regs[off >> 2] = value & 0x01FFFFFCu;
            return;
        default:
            if (off < 0x2000)
                g_regs[off >> 2] = value;
            return;
    }
}

std::uint32_t settings_read(std::uint32_t off) {
    if (spg_reg(off))
        return g_sys->spg.read(Core::kRegBase + off, 4);
    return off < 0x2000 ? g_regs[off >> 2] : 0;
}

// The register block for accesses that do not come through Kamui's accessors.
class Regs final : public dream::mem::MmioHandler {
public:
    std::uint32_t read(std::uint32_t addr, unsigned) override {
        return settings_read((addr - Core::kRegBase) & 0x1FFCu);
    }
    void write(std::uint32_t addr, std::uint32_t value, unsigned) override {
        settings_write((addr - Core::kRegBase) & 0x1FFCu, value);
    }
};
Regs g_regs_handler;

}  // namespace

std::uint64_t native_gfx_frame_swaps() {
    return g_frame_swaps;
}

dream::mem::MmioHandler* native_gfx_regs() {
    return g_sys && g_sys->pvr_core ? &g_regs_handler : nullptr;
}

void native_gfx_start(dream::System& sys) {
    g_sys = &sys;
    if (!sys.pvr_core)
        return;
    // The settings start as the PowerVR's registers stand before the game's first instruction.
    for (unsigned i = 0; i < g_regs.size(); ++i) g_regs[i] = sys.pvr_core->reg_block()[i];
    sys.pvr_core->settings = g_regs.data();
    g_render_event = sys.sched.add("native gfx render", [](std::uint64_t, std::uint64_t) {
        g_sys->holly.raise(dream::holly::Irq::RenderDone);
        g_sys->holly.raise(dream::holly::Irq::RenderDoneIsp);
        g_sys->holly.raise(dream::holly::Irq::RenderDoneVideo);
    });
}

// The side effect of a device register access, which the hooks keep: the scheduler catches up to
// the guest clock.
void device_access() {
    if (g_sys->memory.on_device_access)
        g_sys->memory.on_device_access();
}

// Kamui's vertex-buffer send. r4 points at a descriptor: +8 kind (0 sends through ch2 DMA, 1 first
// runs a TA list init for context +48), +12 length in bytes, +16 source, +20 destination, +28 wait
// mask; +36 and +40 are written on the way out. Other kinds and sources run the original.
bool km_send_buffer(dream::sh4::Ctx& c, dream::Memory& m) {
    if (!g_sys || !g_sys->pvr_core)
        return false;
    const std::uint32_t d = c.r[4];
    const std::uint32_t len = m.read32(d + 12), src = m.read32(d + 16), dst = m.read32(d + 20);
    const std::uint32_t kind = m.read32(d + 8);
    // Vertex lists to the polygon FIFO and textures to the texture path, from main RAM. `stat` is
    // the destination as C2DSTAT holds it.
    const std::uint32_t stat = (dst & 0x03FFFFE0u) | 0x10000000u;
    const bool from_ram = (kind == 0 || kind == 1) && (src & 0x1C000000u) == 0x0C000000u;
    const bool vertex_list = from_ram && (dst & 0x01800000u) == 0;
    // Textures need the native system block's LMMODE registers.
    const bool texture = from_ram && (stat & 0x01000000u) != 0 && native_dev_on();
    if (!vertex_list && !texture)
        return false;
    // The original's entry poll (every emitted function starts with one).
    if (c.cycles >= c.next_event) {
        c.pc = 0x8C045120u;
        dream::sh4::deliver_irq(c, m);
    }
    if (kind == 1) {
        // The list init runs the original, called as its jsr calls it.
        c.cycles += dream::sh4::cost(30) + dream::sh4::cost(3);
        const std::uint32_t pr = c.pr;
        c.r[4] = m.read32(d + 48);
        g_list_ctx = c.r[4];
        c.pr = 0x8C045258u;
        dream::gen::fn_8c06f740(c, m);
        c.pr = pr;
        device_access();  // DMAC and ch2 registers
        c.cycles += dream::sh4::cost(20);
    } else {
        c.cycles += dream::sh4::cost(24);
        device_access();  // DMAC registers
        c.cycles += dream::sh4::cost(16);
        device_access();  // ch2 destination and length
        c.cycles += dream::sh4::cost(6);
    }
    device_access();  // the ch2 start
    const std::uint32_t from = (src & 0x1FFFFFE0u) | 0x80000000u;
    if (vertex_list) {
        // The words join their context's stream; the lists the wait mask names (bit 0 opaque,
        // 1 opaque modifiers, 2 translucent, 3 translucent modifiers, 4 punch-through) end here.
        dream::pvr::Stream& s = g_streams[m.read32(d + 48)];
        // Whole 32-byte chunks, borrowed in place when main RAM is mapped flat (little-endian guest
        // and host; `from` is 32-byte aligned), copied otherwise. Borrowing is safe because Kamui
        // double-buffers the lists per render: a buffer is written again two renders on, after
        // kmBeginScene, where the launcher makes sure the borrowed words are read (km_begin_scene).
        const std::uint32_t chunks = len / 32;
        const std::uint32_t ram_off = from & 0x00FFFFFFu;
        if (m.fast_ram && dream::Memory::is_ram(from) && ram_off + chunks * 32u <= 0x01000000u) {
            s.borrow(reinterpret_cast<const std::uint32_t*>(m.fast_ram + ram_off), std::size_t{chunks} * 8);
        } else {
            for (std::uint32_t i = 0; i < chunks; ++i) {
                std::uint32_t chunk[8];
                for (std::uint32_t k = 0; k < 8; ++k) chunk[k] = m.read32(from + 32 * i + 4 * k);
                s.append(chunk, 8);
            }
        }
        g_regs[Core::kTaItpCurrent >> 2] += 32 * chunks;
        static constexpr dream::holly::Irq kEnd[5] = {dream::holly::Irq::OpaqueDone, dream::holly::Irq::OpaqueModDone,
                                                     dream::holly::Irq::TransDone, dream::holly::Irq::TransModDone,
                                                     dream::holly::Irq::PunchThruDone};
        const std::uint32_t lists = m.read16(d + 28) & 0x1Fu;
        for (unsigned l = 0; l < 5; ++l)
            if (lists & (1u << l))
                g_sys->holly.raise(kEnd[l]);
    } else {
        // Texture memory through the 64-bit or the 32-bit view, as LMMODE0/1 select for the two
        // texture-area mirrors.
        const std::uint32_t to = stat & 0x01FFFFE0u;
        const bool path64 = native_sb_reg((stat & 0x02000000u) ? 0x88 : 0x84) == 0;
        const std::uint32_t view = (to & 0x00FFFFFFu) | (path64 ? 0x04000000u : 0x05000000u);
        // One bulk copy where DcMemory::copy32 can; the 32-bit view interleaves the banks, so it
        // goes word by word.
        if (!g_sys->memory.copy32(view, from, len))
            for (std::uint32_t off = 0; off < len; off += 4) g_sys->memory.write32(view + off, m.read32(from + off));
    }
    g_sys->holly.raise(dream::holly::Irq::Ch2Dma);
    c.cycles += dream::sh4::cost(3);
    // The stamp is TMU0's counter (the original reads 0xFFD8000C), taken where the original's
    // epilogue reads it: after the scheduler catch-up, before that block's cycles are added.
    device_access();
    m.write32(d + 36, g_sys->tmu.tcnt(0));
    m.write32(d + 40, 3);
    c.cycles += dream::sh4::cost(14);
    c.r[0] = len;
    return true;
}

// Kamui's two PVR register accessors: 0x8C06BB20 writes `0xA05F8000 + r4 = r5` and returns 1,
// 0x8C06BB2A returns `0xA05F8000 + r4`. The hooks use the register values above instead of the
// memory map and keep the originals' timing: the entry poll, the scheduler catch-up, the write at
// the entry cycle then 5 cycles, the read after 3 (it is in the rts delay slot).
bool km_pvr_write(dream::sh4::Ctx& c, dream::Memory& m) {
    if (!g_sys || !g_sys->pvr_core)
        return false;
    if (c.cycles >= c.next_event) {
        c.pc = 0x8C06BB20u;
        dream::sh4::deliver_irq(c, m);
    }
    c.r[3] = 0xA05F8000u;
    const std::uint32_t off = c.r[4];
    c.r[4] += c.r[3];
    device_access();
    settings_write(off & 0x1FFCu, c.r[5]);
    c.cycles += dream::sh4::cost(5);
    c.r[0] = 1;
    return true;
}

bool km_pvr_read(dream::sh4::Ctx& c, dream::Memory& m) {
    if (!g_sys || !g_sys->pvr_core)
        return false;
    if (c.cycles >= c.next_event) {
        c.pc = 0x8C06BB2Au;
        dream::sh4::deliver_irq(c, m);
    }
    c.cycles += dream::sh4::cost(3);
    device_access();
    c.r[0] = settings_read(c.r[4] & 0x1FFCu);
    return true;
}

// kmBeginScene (0x8C046420) takes a vertex buffer for the scene's lists; the launcher first makes
// sure the words borrowed from it two renders ago are read (Stream::borrow). The original runs on.
void km_begin_scene() {
    if (g_sys && g_sys->pvr_core && g_sys->pvr_core->on_lists_begin)
        g_sys->pvr_core->on_lists_begin();
}

// Kamui's render starts (0x8C06F900 to the framebuffer, 0x8C06F800 to a texture), r4 the TA
// context whose lists the render draws. The originals run on; their render-start write starts it.
bool km_render_start(dream::sh4::Ctx& c) {
    if (g_sys && g_sys->pvr_core)
        g_render_ctx = c.r[4];
    return false;
}

std::uint32_t* map_ta_chunk(dream::Memory& m, std::uint32_t addr) noexcept {
    if (dream::Memory::is_sq(addr)) {
        const unsigned bank = (addr >> 5) & 1u;
        if (!m.sq_ram || !m.sq_to_ram[bank])
            return nullptr;
        const std::uint32_t ram_off = (m.sq_area[bank] | (addr & 0x03FFFFE0u)) & 0x00FFFFFFu;
        if (ram_off + kTaChunkBytes > kMainRamBytes)
            return nullptr;
        return reinterpret_cast<std::uint32_t*>(m.sq_ram + ram_off);
    }
    if (dream::Memory::is_ram(addr) && m.fast_ram) {
        const std::uint32_t ram_off = addr & 0x00FFFFE0u;
        if (ram_off + kTaChunkBytes > kMainRamBytes)
            return nullptr;
        return reinterpret_cast<std::uint32_t*>(m.fast_ram + ram_off);
    }
    return nullptr;
}

void walk_and_tag_range(dream::Memory& m, std::uint32_t before, std::uint32_t after) {
    bool in_sprite = false;
    bool header_active = false;
    std::uint32_t cur_pcw = 0;
    std::uint32_t line_words_remaining = 0;

    auto u2f = [](std::uint32_t u) noexcept -> float {
        float f;
        std::memcpy(&f, &u, sizeof(f));
        return f;
    };

    auto pcw_para_type = [](std::uint32_t p) noexcept -> std::uint32_t { return (p >> 29) & 7u; };
    auto pcw_volume = [](std::uint32_t p) noexcept -> std::uint32_t { return (p >> 6) & 1u; };
    auto pcw_col_type = [](std::uint32_t p) noexcept -> std::uint32_t { return (p >> 4) & 3u; };
    auto pcw_texture = [](std::uint32_t p) noexcept -> std::uint32_t { return (p >> 3) & 1u; };
    auto pcw_offset = [](std::uint32_t p) noexcept -> std::uint32_t { return (p >> 2) & 1u; };

    auto header_is_64 = [&](std::uint32_t pcw) noexcept -> bool {
        if (pcw_col_type(pcw) != kColorIntensity)
            return false;
        if (pcw_volume(pcw))
            return true;
        return pcw_texture(pcw) != 0 && pcw_offset(pcw) != 0;
    };

    auto vertex_is_64 = [&](std::uint32_t pcw) noexcept -> bool {
        const std::uint32_t col = pcw_col_type(pcw);
        if (pcw_volume(pcw))
            return pcw_texture(pcw) != 0;
        if (!pcw_texture(pcw))
            return false;
        return col == kColorFloating;
    };

    for (std::uint32_t addr = before; addr < after;) {
        std::uint32_t* const w = map_ta_chunk(m, addr);
        if (!w) {
            addr += kTaChunkBytes;
            continue;
        }

        if (line_words_remaining != 0) {
            line_words_remaining = (line_words_remaining <= 8u) ? 0u : (line_words_remaining - 8u);
            addr += kTaChunkBytes;
            continue;
        }

        const std::uint32_t pcw = w[0];
        const std::uint32_t para_type = pcw_para_type(pcw);

        switch (para_type) {
            case kParaSprite:
                in_sprite = true;
                header_active = true;
                cur_pcw = pcw;
                addr += kTaChunkBytes;
                break;
            case kParaPolygonOrModVol: {
                in_sprite = false;
                header_active = true;
                cur_pcw = pcw;
                const bool is_64 = header_is_64(pcw);
                addr += is_64 ? kTaDoubleChunkBytes : kTaChunkBytes;
                break;
            }
            case kParaExternal: {
                const std::uint32_t count = w[1];
                line_words_remaining = 6u * count;
                addr += kTaChunkBytes;
                break;
            }
            case kParaVertex: {
                if (in_sprite) {
                    if (addr + kTaDoubleChunkBytes <= after) {
                        std::uint32_t* const w2 = map_ta_chunk(m, addr + kTaChunkBytes);
                        if (w2) {
                            const std::uint32_t az = w[3];
                            const std::uint32_t bz = w[6];
                            const std::uint32_t cz = w2[1];
                            if (az == bz && az == cz && u2f(az) >= kUiOverlayMinDepth) {
                                w[3] |= 1u;
                                w[6] |= 1u;
                                w2[1] |= 1u;
                            }
                        }
                    }
                    addr += kTaDoubleChunkBytes;
                    break;
                }
                if (header_active) {
                    if (u2f(w[3]) >= kUiOverlayMinDepth) {
                        w[3] |= 1u;
                    }
                    const bool is_64 = vertex_is_64(cur_pcw);
                    addr += is_64 ? kTaDoubleChunkBytes : kTaChunkBytes;
                    break;
                }
                addr += kTaChunkBytes;
                break;
            }
            case kParaEndOfList:
                header_active = false;
                in_sprite = false;
                addr += kTaChunkBytes;
                break;
            default:
                addr += kTaChunkBytes;
                break;
        }
    }
}

void native_gfx_ui_tag_begin(dream::Memory& m) {
    if (g_ui_tag_depth++ == 0) {
        g_ui_tag_valid = false;
        const std::uint32_t desc = m.read32(kKamuiVertexBufDescPtr);
        if (dream::Memory::is_ram(desc)) {
            for (unsigned list = 0; list < 5; ++list) {
                g_ui_tag_before[list] = m.read32(desc + 4u * list);
            }
            g_ui_tag_valid = true;
        }
    }
}

void native_gfx_ui_tag_end(dream::Memory& m) {
    if (g_ui_tag_depth <= 0)
        return;
    if (--g_ui_tag_depth != 0)
        return;
    if (!g_ui_tag_valid)
        return;
    g_ui_tag_valid = false;
    const std::uint32_t desc = m.read32(kKamuiVertexBufDescPtr);
    if (!dream::Memory::is_ram(desc))
        return;

    for (const unsigned list : {0u, 2u, 4u}) {
        const std::uint32_t before = g_ui_tag_before[list];
        const std::uint32_t after = m.read32(desc + 4u * list);
        if (after <= before || ((after - before) & (kTaChunkBytes - 1u)) != 0 || (after - before) > kMaxUiTagBytes)
            continue;
        walk_and_tag_range(m, before, after);
    }
}

}  // namespace rez

DC_HOOK_ENTRY(km_send_buffer) {
    return rez::km_send_buffer(c, m);
}

DC_HOOK_ENTRY(km_pvr_write) {
    return rez::km_pvr_write(c, m);
}

DC_HOOK_ENTRY(km_pvr_read) {
    return rez::km_pvr_read(c, m);
}

DC_HOOK_ENTRY(km_begin_scene) {
    (void)c;
    (void)m;
    rez::km_begin_scene();
    return false;
}

DC_HOOK_ENTRY(km_render_start) {
    (void)m;
    return rez::km_render_start(c);
}

DC_HOOK_ENTRY(ui_tag_stage_pause) {
    (void)c;
    rez::native_gfx_ui_tag_begin(m);
    return false;
}
DC_HOOK_EXIT(ui_tag_stage_pause) {
    (void)c;
    rez::native_gfx_ui_tag_end(m);
}

DC_HOOK_ENTRY(ui_tag_modal_dialog) {
    (void)c;
    rez::native_gfx_ui_tag_begin(m);
    return false;
}
DC_HOOK_EXIT(ui_tag_modal_dialog) {
    (void)c;
    rez::native_gfx_ui_tag_end(m);
}

DC_HOOK_ENTRY(ui_tag_title_menu) {
    (void)c;
    rez::native_gfx_ui_tag_begin(m);
    return false;
}
DC_HOOK_EXIT(ui_tag_title_menu) {
    (void)c;
    rez::native_gfx_ui_tag_end(m);
}

DC_HOOK_ENTRY(ui_tag_options_menu) {
    (void)c;
    rez::native_gfx_ui_tag_begin(m);
    return false;
}
DC_HOOK_EXIT(ui_tag_options_menu) {
    (void)c;
    rez::native_gfx_ui_tag_end(m);
}

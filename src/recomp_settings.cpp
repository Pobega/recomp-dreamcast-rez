// "recomp settings" in Rez's main menu: a page built from Rez's own options screen offering
// aspect ratio (4:3, 3:2, 16:9, 1:1), render resolution ladders, and frame rate (30 vs 60 fps).
// The game's tables are changed only while the page runs, and restored when closed.
#include "recomp_settings.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

#include "dream/runtime/sh4/abi.h"
#include "dream/runtime/sh4/cost.h"
#include "dream/runtime/sh4/ctx.h"
#include "dreamcomp/hook.h"
#include "dreamcomp/port.h"
#include "dreamcomp/settings.h"
#include "native_sys.h"

namespace rez {

constexpr std::uint32_t kSet0 = 0x8C303B78u;
constexpr unsigned kSetWords = 20;
constexpr std::uint32_t kBoxSelect0 = 0x8C303F0Cu;
constexpr std::uint32_t kBoxSelect1 = 0x8C303F38u;
constexpr std::uint32_t kBoxSelectItems = 0x8C30460Cu;
constexpr unsigned kBoxSelectWords = 11;
constexpr unsigned kLanguageCount = 6;
constexpr std::uint32_t kRowTypeBoxOnOff = 1u;
constexpr std::uint32_t kRowTypeBoxSelect = 3u;
constexpr std::uint32_t kSet0Getter = 0x8C301464u, kSet0Setter = 0x8C30146Au;
constexpr std::uint32_t kBoxSelectInputGetReturnAddr = 0x8C301A1Au;
constexpr std::uint32_t kBoxSelectSetLeftReturnAddr = 0x8C301A44u;
constexpr std::uint32_t kBoxSelectSetRightReturnAddr = 0x8C301A70u;
constexpr std::uint32_t kBoxSelectDrawReturnAddr = 0x8C301BC0u;

struct Text {
    std::uint32_t at;
    const char* original;
    const char* page;
};

// Row 0: aspect ratio (replaces "overdrive button" at 0x8C3038E8)
constexpr Text kAspectRow{0x8C3038E8u, "overdrive button", "aspect ratio"};
constexpr Text kSelectValue{0x8C3038FCu, "overdrive-taste", ""};

// Row 1: resolution (replaces "sound mode" at 0x8C303784 / 0x8C303D1C)
constexpr Text kResRow{0x8C303784u, "sound mode", "resolution"};
constexpr Text kResHead{0x8C303D1Cu, "sound mode", "resolution"};

// Titles and headings
constexpr Text kTitle{0x8C303804u, "sound effects vol.", "recomp settings"};
constexpr Text kHead{0x8C30385Cu, "up/down", "recomp"};

// Row 2: frame rate (replaces "shot button" at 0x8C303898, subpanel heading at 0x8C303E20)
constexpr Text kFpsRow{0x8C303898u, "shot button", "frame rate"};
constexpr Text kFpsHead{0x8C303E20u, "heads up display", "frame rate"};
constexpr Text k30Fps{0x8C303DACu, "normal", "30 fps"};
constexpr Text k60Fps{0x8C303DBCu, "reverse", "60 fps"};

void write_text(dream::Memory& m, std::uint32_t a, const char* s) {
    for (std::size_t i = 0;; ++i) {
        m.write8(a + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(s[i]));
        if (!s[i])
            return;
    }
}

namespace {

constexpr std::uint32_t kMenu = 0x8C3042BCu;  // nine 32-byte entries
constexpr std::uint32_t kReturnOne = 0x8C014EA4u;  // `rts; mov #1,r0` in the main program
constexpr unsigned kOrder[9] = {1, 2, 3, 4, 0, 5, 6, 7, 8};  // shown slot -> table slot
constexpr unsigned kItem = 4;                                  // where slot 0 is shown
constexpr std::uint32_t kItemId = 2, kOptionsId = 6;

constexpr std::uint32_t kRows = 0x8C3034A4u, kRowBytes = 40;  // 17 rows; the type at +24
constexpr std::uint32_t kHeading = 0x8C303B00u;               // six title pointers

constexpr unsigned kRowCount = 17, kRowWords = kRowBytes / 4;

bool g_on = true, g_page = false, g_menu = false;
std::uint32_t g_menu_state = 0;              // the menu's state, from recomp_menu_begin to _end
std::uint32_t g_table[9][8];                 // the menu table as the game has it, while reordered
std::uint32_t g_rows[kRowCount][kRowWords];  // the options rows, while the page is open
std::uint32_t g_set0[kSetWords], g_box0[kBoxSelectWords], g_box1[kBoxSelectWords], g_box_items[kLanguageCount], g_heading[kLanguageCount];

bool text_is(dream::Memory& m, std::uint32_t a, const char* s) {
    for (std::size_t i = 0;; ++i) {
        const char ch = static_cast<char>(m.read8(a + static_cast<std::uint32_t>(i)));
        if (ch != s[i])
            return false;
        if (!ch)
            return true;
    }
}

// The title overlay as on the European and Japanese discs: its menu table's ids, the options
// screen's labels, rows and heading. Anything else and the item is not offered.
bool overlay_as_read(dream::Memory& m) {
    for (unsigned i = 0; i < 9; ++i)
        if (m.read32(kMenu + 32 * i + 24) != 2 + i)
            return false;
    for (const Text& t : {kAspectRow, kSelectValue, kResRow, kResHead,
                          kHead, kFpsRow, kFpsHead, k30Fps, k60Fps}) {
        if (!text_is(m, t.at, t.original))
            return false;
    }
    if (!text_is(m, kTitle.at, kTitle.original) && !text_is(m, kTitle.at, kTitle.page))
        return false;
    return m.read32(kRows + 24) == 1 && m.read32(kRows + 36) == 0 && m.read32(kRows + 16 * kRowBytes + 24) == 4;
}

// The page is the options screen with heading "recomp", row 0 "aspect ratio", row 1 "resolution",
// row 2 "frame rate", and row 16 "return". The remaining rows are emptied.
void page_open(dream::Memory& m) {
    for (unsigned r = 0; r < kRowCount; ++r)
        for (unsigned w = 0; w < kRowWords; ++w)
            g_rows[r][w] = m.read32(kRows + r * kRowBytes + 4 * w);
    for (unsigned i = 0; i < kLanguageCount; ++i) {
        g_heading[i] = m.read32(kHeading + 4 * i);
        g_box_items[i] = m.read32(kBoxSelectItems + 24 * i);
    }
    for (unsigned i = 0; i < kSetWords; ++i)
        g_set0[i] = m.read32(kSet0 + 4 * i);
    for (unsigned i = 0; i < kBoxSelectWords; ++i) {
        g_box0[i] = m.read32(kBoxSelect0 + 4 * i);
        g_box1[i] = m.read32(kBoxSelect1 + 4 * i);
    }

    for (const Text& t : {kAspectRow, kResRow, kResHead, kTitle, kHead, kFpsRow, kFpsHead, k30Fps, k60Fps})
        write_text(m, t.at, t.page);

    for (unsigned r = 1; r + 1 < kRowCount; ++r)
        for (unsigned word = 0; word < kRowWords; ++word)
            m.write32(kRows + r * kRowBytes + 4 * word, 0);

    for (unsigned i = 0; i < kLanguageCount; ++i) {
        m.write32(kRows + 4 * i, kAspectRow.at);
        m.write32(kRows + kRowBytes + 4 * i, kResRow.at);
        m.write32(kRows + 2 * kRowBytes + 4 * i, kFpsRow.at);

        m.write32(kHeading + 4 * i, kHead.at);

        m.write32(kBoxSelect0 + 4 * i, kAspectRow.at);
        m.write32(kBoxSelect1 + 4 * i, kResHead.at);
        m.write32(kBoxSelectItems + 24 * i, kSelectValue.at);

        m.write32(kSet0 + 4 * i, kFpsHead.at);
        m.write32(kSet0 + 24 + 4 * i, k30Fps.at);
        m.write32(kSet0 + 48 + 4 * i, k60Fps.at);
    }

    m.write32(kRows + 24, kRowTypeBoxSelect);
    m.write32(kRows + 36, 0u);
    m.write32(kRows + kRowBytes + 24, kRowTypeBoxSelect);
    m.write32(kRows + kRowBytes + 36, 1u);
    m.write32(kRows + 2 * kRowBytes + 24, kRowTypeBoxOnOff);
    m.write32(kRows + 2 * kRowBytes + 36, 0u);

    const auto fam = dreamcomp::current_aspect_family();
    const auto& res_opts = dreamcomp::resolution_options(fam);

    m.write32(kBoxSelect0 + 24, kBoxSelectItems);
    m.write32(kBoxSelect0 + 28, 0u);
    m.write32(kBoxSelect0 + 32, static_cast<std::uint32_t>(dreamcomp::AspectFamily::Count) - 1u);
    m.write32(kBoxSelect0 + 36, kSet0Getter);
    m.write32(kBoxSelect0 + 40, kSet0Setter);

    m.write32(kBoxSelect1 + 24, kBoxSelectItems);
    m.write32(kBoxSelect1 + 28, 0u);
    m.write32(kBoxSelect1 + 32, static_cast<std::uint32_t>(res_opts.size()) - 1u);
    m.write32(kBoxSelect1 + 36, kSet0Getter);
    m.write32(kBoxSelect1 + 40, kSet0Setter);

    m.write32(kSet0 + 72, kSet0Getter);
    m.write32(kSet0 + 76, kSet0Setter);

    g_page = true;

    const unsigned cur_res = dreamcomp::current_resolution_index();
    const char* label = (cur_res < res_opts.size()) ? res_opts[cur_res].label : "unknown";
    std::printf("rez: recomp settings opened (aspect %s, res %s, %u fps)\n",
                dreamcomp::aspect_family_label(fam), label, rez::native_sys_get_frame_rate());
}

void page_close(dream::Memory& m) {
    for (unsigned r = 0; r < kRowCount; ++r)
        for (unsigned w = 0; w < kRowWords; ++w)
            m.write32(kRows + r * kRowBytes + 4 * w, g_rows[r][w]);
    for (unsigned i = 0; i < kLanguageCount; ++i) {
        m.write32(kHeading + 4 * i, g_heading[i]);
        m.write32(kBoxSelectItems + 24 * i, g_box_items[i]);
    }
    for (unsigned i = 0; i < kSetWords; ++i)
        m.write32(kSet0 + 4 * i, g_set0[i]);
    for (unsigned i = 0; i < kBoxSelectWords; ++i) {
        m.write32(kBoxSelect0 + 4 * i, g_box0[i]);
        m.write32(kBoxSelect1 + 4 * i, g_box1[i]);
    }
    for (const Text& t : {kAspectRow, kSelectValue, kResRow, kResHead, kTitle, kHead, kFpsRow, kFpsHead, k30Fps, k60Fps})
        write_text(m, t.at, t.original);
    g_page = false;
}

}  // namespace

void recomp_settings_start(bool on) {
    g_on = on;
    std::printf("rez: recomp settings item %s\n", on ? "on" : "off");
}

// The main menu's table slot 0, "play" (id 2), is never shown (its availability function returns
// 0). While the menu's input and drawing run, the table is in the order travelling, play, score
// attack, options, slot 0 as "recomp settings", how to play, beyond, exit, DEMO, and the cursor
// follows that order; both are put back afterwards, so the places the game sets the cursor keep
// their meaning. Slot 8 stays DEMO: the drawing calls its availability function after its loop.
void recomp_menu_begin(dream::Memory& m, std::uint32_t menu) {
    if (!g_on || g_menu || !menu)
        return;
    if (g_page) {  // back from the page: its tables put back, the cursor on the item
        page_close(m);
        m.write32(menu + 32, 0);
    }
    if (!overlay_as_read(m))
        return;
    for (unsigned s = 0; s < 9; ++s)
        for (unsigned w = 0; w < 8; ++w)
            g_table[s][w] = m.read32(kMenu + 32 * s + 4 * w);
    write_text(m, kTitle.at, kTitle.page);
    for (unsigned i = 0; i < 9; ++i)
        for (unsigned w = 0; w < 8; ++w)
            m.write32(kMenu + 32 * i + 4 * w, g_table[kOrder[i]][w]);
    for (unsigned l = 0; l < 6; ++l)
        m.write32(kMenu + 32 * kItem + 4 * l, kTitle.at);
    m.write32(kMenu + 32 * kItem + 28, kReturnOne);
    const std::uint32_t cursor = m.read32(menu + 32);
    for (unsigned i = 0; i < 9; ++i)
        if (kOrder[i] == cursor) {
            m.write32(menu + 32, i);
            break;
        }
    g_menu = true;
    g_menu_state = menu;
}

void recomp_menu_chosen(dream::Memory& m) {
    if (!g_menu || m.read32(g_menu_state + 12) != kItemId)
        return;
    m.write32(g_menu_state + 12, kOptionsId);  // the options screen opens, as the page
    page_open(m);
}

void recomp_menu_end(dream::Memory& m) {
    if (!g_menu)
        return;
    const std::uint32_t cursor = m.read32(g_menu_state + 32);
    if (cursor < 9)
        m.write32(g_menu_state + 32, kOrder[cursor]);
    for (unsigned s = 0; s < 9; ++s)
        for (unsigned w = 0; w < 8; ++w)
            m.write32(kMenu + 32 * s + 4 * w, g_table[s][w]);
    if (!g_page)
        write_text(m, kTitle.at, kTitle.original);
    g_menu = false;
}

}  // namespace rez

// 0x8C301464: Choice getter.
DC_HOOK_ENTRY(recomp_get) {
    if (!rez::g_page)
        return false;
    if (c.cycles >= c.next_event) {
        c.pc = 0x8C301464u;
        dream::sh4::deliver_irq(c, m);
    }
    if (c.pr == rez::kBoxSelectInputGetReturnAddr) {
        if (c.r[13] == rez::kBoxSelect0) {
            c.r[0] = static_cast<std::uint32_t>(dreamcomp::current_aspect_family());
        } else {
            c.r[0] = dreamcomp::current_resolution_index();
        }
    } else if (c.pr == rez::kBoxSelectDrawReturnAddr) {
        const char* label = "";
        if (c.r[14] == rez::kBoxSelect0) {
            label = dreamcomp::aspect_family_label(dreamcomp::current_aspect_family());
        } else {
            const auto& res_opts = dreamcomp::resolution_options(dreamcomp::current_aspect_family());
            const unsigned idx = dreamcomp::current_resolution_index();
            label = (idx < res_opts.size()) ? res_opts[idx].label : "";
        }
        rez::write_text(m, rez::kSelectValue.at, label);
        c.r[0] = 0u;
    } else {
        c.r[0] = (rez::native_sys_get_frame_rate() == 60) ? 1u : 0u;
    }
    c.cycles += dream::sh4::cost(8);
    return true;
}

// 0x8C30146A: Choice setter.
DC_HOOK_ENTRY(recomp_set) {
    if (!rez::g_page)
        return false;
    if (c.cycles >= c.next_event) {
        c.pc = 0x8C30146Au;
        dream::sh4::deliver_irq(c, m);
    }
    if (c.pr == rez::kBoxSelectSetLeftReturnAddr || c.pr == rez::kBoxSelectSetRightReturnAddr) {
        if (c.r[13] == rez::kBoxSelect0) {
            dreamcomp::set_aspect_family(static_cast<dreamcomp::AspectFamily>(c.r[4]));
            m.write32(rez::kBoxSelect1 + 32,
                      static_cast<std::uint32_t>(dreamcomp::resolution_options(dreamcomp::current_aspect_family()).size()) - 1u);
            c.r[0] = 1u;
        } else if (c.r[13] == rez::kBoxSelect1) {
            dreamcomp::set_resolution_index(c.r[4]);
            c.r[0] = 1u;
        }
    } else if (c.r[14] == rez::kSet0) {
        const unsigned fps = c.r[4] ? 60u : 30u;
        rez::native_sys_frame_rate(fps);
        dreamcomp::settings().set("frame_rate", std::to_string(fps));
        dreamcomp::settings().save();
    }
    c.cycles += dream::sh4::cost(8);
    return true;
}

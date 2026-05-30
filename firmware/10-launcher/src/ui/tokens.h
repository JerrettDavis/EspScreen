#pragma once
#include <cstdint>

namespace tok {

// ── Background levels (darkest → lightest) ────────────────────────────────
constexpr uint32_t BG_BASE       = 0x0D1117; // app/screen base
constexpr uint32_t BG_ELEVATED   = 0x161B26; // bars / topbar / botbar
constexpr uint32_t SURFACE       = 0x1E2430; // cards / tiles / rows
constexpr uint32_t SURFACE_PRESS = 0x273040; // card pressed state
constexpr uint32_t SURFACE_HI    = 0x2C3444; // selected row

// ── Cyan accent ───────────────────────────────────────────────────────────
constexpr uint32_t ACCENT        = 0x22D3EE;
constexpr uint32_t ACCENT_PRESS  = 0x0EA5C4;
constexpr uint32_t ACCENT_DIM    = 0x155E6B;
constexpr uint32_t ACCENT_TEXT   = 0x062028; // text on cyan fill

// ── Text ─────────────────────────────────────────────────────────────────
constexpr uint32_t TEXT_PRIMARY  = 0xF1F5F9;
constexpr uint32_t TEXT_SECOND   = 0xA9B4C4;
constexpr uint32_t TEXT_MUTED    = 0x6B7689;

// ── Semantic ─────────────────────────────────────────────────────────────
constexpr uint32_t SUCCESS       = 0x34D399;
constexpr uint32_t WARN          = 0xFBBF24;
constexpr uint32_t ERROR_        = 0xF87171; // NOTE: ERROR_ avoids collision with Arduino/ESP macro ERROR

// ── Structure ─────────────────────────────────────────────────────────────
constexpr uint32_t DIVIDER       = 0x2A3140;
constexpr uint32_t BAR_TRACK     = 0x222A36;

// ── Spacing scale (px) ───────────────────────────────────────────────────
constexpr int SP_XS = 4;
constexpr int SP_S  = 8;
constexpr int SP_M  = 12;
constexpr int SP_L  = 16;
constexpr int SP_XL = 24;

// ── Radius scale (px) ────────────────────────────────────────────────────
constexpr int R_S = 6;
constexpr int R_M = 10;
constexpr int R_L = 14;

// ── Dimensional constants ─────────────────────────────────────────────────
constexpr int TOPBAR_H    = 40;
constexpr int STATUSBAR_H = 0;
constexpr int BOTBAR_H    = 40;
constexpr int TAP_MIN     = 44;
constexpr int BACK_BTN_W  = 56;
constexpr int BACK_BTN_H  = 32;
constexpr int SCREEN_PAD  = 16;

} // namespace tok

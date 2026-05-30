/**
 * screen_validate.h — layout-correctness validator for LVGL screens.
 *
 * PURE C, LVGL-ONLY. Includes only <lvgl.h>. NO Arduino / FreeRTOS / ESP / HW
 * headers, NO C++ — a later phase compiles this into the DEVICE build, so it
 * must be toolchain-agnostic and free of host/sim dependencies.
 *
 * Usage:
 *     lv_obj_update_layout(root);            // caller should; run() also does it
 *     sv_report_t r;
 *     screen_validate_run(root, &r);
 *     if (r.fail_count) { ...handle... }
 *
 * Rules (see README in screen_validate.c for full semantics):
 *   R1 SV_OUT_OF_BOUNDS      FAIL  visible rect not fully within (0,0)-(319,479)
 *   R2 SV_SIBLING_OVERLAP    FAIL  two layout-positioned siblings intersect
 *   R3 SV_ZERO_SIZE          WARN  visible object with w<=0 or h<=0
 *   R4 SV_CHILD_EXCEEDS_PARENT WARN child exceeds non-clipping parent content
 *   R5 SV_CLIPPED            WARN  visible object fully outside parent clip rect
 */
#ifndef SCREEN_VALIDATE_H
#define SCREEN_VALIDATE_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LCD geometry the screens target (ST7796, portrait). */
#define SV_LCD_W 320
#define SV_LCD_H 480

typedef enum {
    SV_OUT_OF_BOUNDS,
    SV_SIBLING_OVERLAP,
    SV_ZERO_SIZE,
    SV_CHILD_EXCEEDS_PARENT,
    SV_CLIPPED
} sv_rule_t;

typedef enum {
    SV_FAIL,
    SV_WARN
} sv_sev_t;

typedef struct {
    sv_rule_t        rule;
    sv_sev_t         sev;
    const lv_obj_t * a;
    const lv_obj_t * b;        /* NULL unless this is an overlap (R2) */
    lv_area_t        ra;       /* absolute rect of a */
    lv_area_t        rb;       /* absolute rect of b (R2 only; else zeroed) */
    char             a_desc[48]; /* class+index path, e.g. "screen/obj#2/label#0" */
} sv_violation_t;

#define SV_MAX_VIOLATIONS 64

typedef struct {
    sv_violation_t v[SV_MAX_VIOLATIONS];
    int            count;       /* total violations detected (may exceed stored) */
    int            fail_count;  /* number of SV_FAIL-severity violations detected */
} sv_report_t;

/** Exempt a specific sibling pair (by a_desc path) from R2 overlap checks.
 *  Order-independent: allow(A,B) also matches the pair reported as (B,A). */
void screen_validate_allow(const char * path_a, const char * path_b);

/** Clear all allowlisted pairs. */
void screen_validate_reset_allow(void);

/** Run all rules over the tree rooted at `root`, filling `out`.
 *  Caller SHOULD call lv_obj_update_layout(root) first; run() also calls it
 *  defensively so coordinates are valid. */
void screen_validate_run(const lv_obj_t * root, sv_report_t * out);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_VALIDATE_H */

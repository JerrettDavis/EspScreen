/**
 * screen_validate.c — layout-correctness rule engine for LVGL screens.
 *
 * PURE C, LVGL-ONLY (no Arduino/FreeRTOS/ESP/HW headers, no C++). Designed to
 * compile unchanged into both the host validator harness (P2) and the device
 * firmware (a later phase).
 *
 * ── Rule semantics ──────────────────────────────────────────────────────────
 * Visibility (used by R1/R3/R5): an object is "visible" iff it is NOT flagged
 *   LV_OBJ_FLAG_HIDDEN and its LV_PART_MAIN style opacity > LV_OPA_MIN. (We do
 *   not walk ancestor opacity for hidden-ness; a hidden ancestor still lets us
 *   flag a child, but in practice hidden subtrees are skipped by R2 via
 *   lv_obj_is_layout_positioned and rarely matter for R1.)
 *
 * R1 SV_OUT_OF_BOUNDS (FAIL): a visible object's absolute rect is not fully
 *   within (0,0)-(SV_LCD_W-1, SV_LCD_H-1). The root screen itself is exempt
 *   (it legitimately fills the panel and screens are sized exactly to it).
 *
 * R2 SV_SIBLING_OVERLAP (FAIL): two siblings (same parent) whose absolute rects
 *   intersect, where BOTH are layout-positioned (lv_obj_is_layout_positioned —
 *   which already excludes HIDDEN/IGNORE_LAYOUT/FLOATING), and the pair is not
 *   allowlisted. Intersection: !(a.x2<b.x1 || b.x2<a.x1 || a.y2<b.y1 || b.y2<a.y1).
 *
 * R3 SV_ZERO_SIZE (WARN): a visible object with width<=0 or height<=0.
 *
 * R4 SV_CHILD_EXCEEDS_PARENT (WARN): a child's rect extends beyond its parent's
 *   content area AND the parent does NOT clip its children. In LVGL children are
 *   clipped to the parent by default; clipping is disabled only by
 *   LV_OBJ_FLAG_OVERFLOW_VISIBLE. So R4 fires when the parent has
 *   OVERFLOW_VISIBLE and the child spills past the content box.
 *
 * R5 SV_CLIPPED (WARN): a visible object whose rect lies fully outside its
 *   parent's content (clip) rect — it will never be drawn.
 *
 * Output is capped at SV_MAX_VIOLATIONS stored entries; `count`/`fail_count`
 * keep incrementing past the cap so callers can see the true totals.
 */
#include "screen_validate.h"

/* ── Allowlist (R2 exemptions) ───────────────────────────────────────────── */

#define SV_ALLOW_MAX   32
#define SV_DESC_LEN    48

typedef struct {
    char a[SV_DESC_LEN];
    char b[SV_DESC_LEN];
} sv_allow_pair_t;

static sv_allow_pair_t s_allow[SV_ALLOW_MAX];
static int             s_allow_count = 0;

static int sv_streq(const char * x, const char * y) {
    if (!x || !y) return 0;
    while (*x && *y) { if (*x != *y) return 0; x++; y++; }
    return *x == *y;
}

static void sv_strcpy(char * dst, const char * src, int cap) {
    int i = 0;
    if (cap <= 0) return;
    if (src) { for (; src[i] && i < cap - 1; i++) dst[i] = src[i]; }
    dst[i] = '\0';
}

void screen_validate_allow(const char * path_a, const char * path_b) {
    if (s_allow_count >= SV_ALLOW_MAX) return;
    sv_strcpy(s_allow[s_allow_count].a, path_a, SV_DESC_LEN);
    sv_strcpy(s_allow[s_allow_count].b, path_b, SV_DESC_LEN);
    s_allow_count++;
}

void screen_validate_reset_allow(void) {
    s_allow_count = 0;
}

static int sv_is_allowed(const char * da, const char * db) {
    int i;
    for (i = 0; i < s_allow_count; i++) {
        if ((sv_streq(s_allow[i].a, da) && sv_streq(s_allow[i].b, db)) ||
            (sv_streq(s_allow[i].a, db) && sv_streq(s_allow[i].b, da))) {
            return 1;
        }
    }
    return 0;
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static const char * sv_class_hint(const lv_obj_t * obj) {
    const lv_obj_class_t * c = lv_obj_get_class(obj);
    if (c == &lv_label_class)        return "label";
    if (c == &lv_button_class)       return "button";
#if LV_USE_BAR
    if (c == &lv_bar_class)          return "bar";
#endif
#if LV_USE_BUTTONMATRIX
    if (c == &lv_buttonmatrix_class) return "btnm";
#endif
#if LV_USE_TEXTAREA
    if (c == &lv_textarea_class)     return "textarea";
#endif
#if LV_USE_KEYBOARD
    if (c == &lv_keyboard_class)     return "keyboard";
#endif
    return "obj";
}

/* Append "/<hint>#<index>" or the leading "<hint>" to a bounded buffer. */
static int sv_append(char * buf, int pos, int cap, const char * s) {
    while (*s && pos < cap - 1) buf[pos++] = *s++;
    buf[pos] = '\0';
    return pos;
}

static int sv_append_int(char * buf, int pos, int cap, int v) {
    char tmp[12];
    int n = 0, i;
    if (v < 0) { if (pos < cap - 1) buf[pos++] = '-'; v = -v; }
    do { tmp[n++] = (char)('0' + (v % 10)); v /= 10; } while (v && n < (int)sizeof(tmp));
    for (i = n - 1; i >= 0 && pos < cap - 1; i--) buf[pos++] = tmp[i];
    buf[pos] = '\0';
    return pos;
}

/* Build "screen/obj#2/label#0"-style path. Walks parents into a small stack and
 * emits root→leaf. Best-effort, always NUL-terminated within SV_DESC_LEN. */
static void sv_build_desc(const lv_obj_t * obj, char * out, int cap) {
    const lv_obj_t * chain[16];
    int depth = 0;
    const lv_obj_t * o = obj;
    int pos = 0, i;

    while (o && depth < 16) {
        chain[depth++] = o;
        o = lv_obj_get_parent(o);
    }
    /* chain[depth-1] is the topmost (screen). Emit root → leaf. */
    out[0] = '\0';
    for (i = depth - 1; i >= 0; i--) {
        const lv_obj_t * node = chain[i];
        const lv_obj_t * parent = lv_obj_get_parent(node);
        if (i == depth - 1) {
            pos = sv_append(out, pos, cap, "screen");
        } else {
            pos = sv_append(out, pos, cap, "/");
            pos = sv_append(out, pos, cap, sv_class_hint(node));
            pos = sv_append(out, pos, cap, "#");
            pos = sv_append_int(out, pos, cap, parent ? (int)lv_obj_get_index(node) : 0);
        }
        if (pos >= cap - 1) break;
    }
}

static int sv_is_visible(const lv_obj_t * obj) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return 0;
    if (lv_obj_get_style_opa((lv_obj_t *)obj, LV_PART_MAIN) <= LV_OPA_MIN) return 0;
    return 1;
}

static int sv_intersects(const lv_area_t * a, const lv_area_t * b) {
    return !(a->x2 < b->x1 || b->x2 < a->x1 || a->y2 < b->y1 || b->y2 < a->y1);
}

/* A parent clips its children unless OVERFLOW_VISIBLE is set. */
static int sv_parent_clips(const lv_obj_t * parent) {
    return !lv_obj_has_flag(parent, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
}

/* ── Report append (respects the storage cap) ────────────────────────────── */

static void sv_push(sv_report_t * r, sv_rule_t rule, sv_sev_t sev,
                    const lv_obj_t * a, const lv_obj_t * b,
                    const lv_area_t * ra, const lv_area_t * rb) {
    r->count++;
    if (sev == SV_FAIL) r->fail_count++;
    if (r->count > SV_MAX_VIOLATIONS) return;   /* keep counting, stop storing */

    sv_violation_t * v = &r->v[r->count - 1];
    v->rule = rule;
    v->sev  = sev;
    v->a    = a;
    v->b    = b;
    v->ra   = *ra;
    if (rb) { v->rb = *rb; }
    else    { v->rb.x1 = v->rb.y1 = v->rb.x2 = v->rb.y2 = 0; }
    sv_build_desc(a, v->a_desc, (int)sizeof(v->a_desc));
}

/* ── Node-rule walk (R1, R3, R4, R5) ─────────────────────────────────────── */

typedef struct {
    sv_report_t *    r;
    const lv_obj_t * root;
} sv_walk_ctx_t;

static lv_obj_tree_walk_res_t sv_node_cb(lv_obj_t * obj, void * ud) {
    sv_walk_ctx_t * ctx = (sv_walk_ctx_t *)ud;
    sv_report_t * r = ctx->r;
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    const int visible = sv_is_visible(obj);
    const lv_obj_t * parent = lv_obj_get_parent(obj);

    /* R3 SV_ZERO_SIZE (WARN) — visible objects only. */
    if (visible) {
        int w = (int)(coords.x2 - coords.x1 + 1);
        int h = (int)(coords.y2 - coords.y1 + 1);
        if (w <= 0 || h <= 0) {
            sv_push(r, SV_ZERO_SIZE, SV_WARN, obj, NULL, &coords, NULL);
        }
    }

    /* R1 SV_OUT_OF_BOUNDS (FAIL) — visible, non-root. */
    if (visible && obj != ctx->root) {
        if (coords.x1 < 0 || coords.y1 < 0 ||
            coords.x2 > (SV_LCD_W - 1) || coords.y2 > (SV_LCD_H - 1)) {
            sv_push(r, SV_OUT_OF_BOUNDS, SV_FAIL, obj, NULL, &coords, NULL);
        }
    }

    /* Parent-relative rules (R4, R5) — need a real parent (not the screen). */
    if (parent && obj != ctx->root) {
        lv_area_t content;
        lv_obj_get_content_coords((lv_obj_t *)parent, &content);

        /* R5 SV_CLIPPED (WARN) — visible object fully outside parent content. */
        if (visible && !sv_intersects(&coords, &content)) {
            sv_push(r, SV_CLIPPED, SV_WARN, obj, NULL, &coords, NULL);
        }

        /* R4 SV_CHILD_EXCEEDS_PARENT (WARN) — child spills past a NON-clipping
         * parent's content box. */
        if (!sv_parent_clips(parent)) {
            if (coords.x1 < content.x1 || coords.y1 < content.y1 ||
                coords.x2 > content.x2 || coords.y2 > content.y2) {
                sv_push(r, SV_CHILD_EXCEEDS_PARENT, SV_WARN, obj, NULL, &coords, NULL);
            }
        }
    }

    return LV_OBJ_TREE_WALK_NEXT;
}

/* ── Sibling overlap walk (R2) ───────────────────────────────────────────── */
/* For each object, examine its direct children pairwise. */
static lv_obj_tree_walk_res_t sv_parent_cb(lv_obj_t * obj, void * ud) {
    sv_walk_ctx_t * ctx = (sv_walk_ctx_t *)ud;
    sv_report_t * r = ctx->r;
    uint32_t n = lv_obj_get_child_count(obj);
    uint32_t i, j;

    for (i = 0; i + 1 < n; i++) {
        lv_obj_t * ca = lv_obj_get_child(obj, (int32_t)i);
        if (!lv_obj_is_layout_positioned(ca)) continue;
        lv_area_t ra;
        lv_obj_get_coords(ca, &ra);

        for (j = i + 1; j < n; j++) {
            lv_obj_t * cb = lv_obj_get_child(obj, (int32_t)j);
            if (!lv_obj_is_layout_positioned(cb)) continue;
            lv_area_t rb;
            lv_obj_get_coords(cb, &rb);

            if (!sv_intersects(&ra, &rb)) continue;

            char da[SV_DESC_LEN], db[SV_DESC_LEN];
            sv_build_desc(ca, da, (int)sizeof(da));
            sv_build_desc(cb, db, (int)sizeof(db));
            if (sv_is_allowed(da, db)) continue;

            sv_push(r, SV_SIBLING_OVERLAP, SV_FAIL, ca, cb, &ra, &rb);
        }
    }
    return LV_OBJ_TREE_WALK_NEXT;
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

void screen_validate_run(const lv_obj_t * root, sv_report_t * out) {
    if (!out) return;
    out->count = 0;
    out->fail_count = 0;
    if (!root) return;

    /* Defensive: ensure layout is resolved so coordinates are meaningful. */
    lv_obj_update_layout((lv_obj_t *)root);

    sv_walk_ctx_t ctx;
    ctx.r = out;
    ctx.root = root;

    lv_obj_tree_walk((lv_obj_t *)root, sv_node_cb,   &ctx);  /* R1,R3,R4,R5 */
    lv_obj_tree_walk((lv_obj_t *)root, sv_parent_cb, &ctx);  /* R2 */
}

/**
 * test_validate.cpp — Unity harness: validates ALL renderable screens.
 *
 * Builds each screen from sim_registry against the host stub layer, resolves
 * layout, and runs the pure-C screen_validate rule engine. A screen FAILS the
 * test iff it produces any SV_FAIL-severity violation (R1 out-of-bounds, R2
 * sibling overlap). Warnings (R3/R4/R5) are dumped but do not fail the build.
 *
 * Also includes a cascade-verification test proving a single root-level
 * lv_obj_update_layout() resolves nested flex containers (the claude_widget
 * content → bar-row blocks).
 *
 * Run: pio test -e test_native -f test_validate
 */
#include <unity.h>
#include <lvgl.h>
#include <cstdio>
#include <cstring>

#include "../../sim/sim_display.h"
#include "../../sim/sim_registry.h"
#include "../../src/ui/theme.h"
#include "../../validator/screen_validate.h"

void setUp(void)    {}
void tearDown(void) {}

/* ── Violation dump (actionable CI output) ──────────────────────────────────── */

static const char* rule_name(sv_rule_t r) {
    switch (r) {
        case SV_OUT_OF_BOUNDS:       return "OUT_OF_BOUNDS";
        case SV_SIBLING_OVERLAP:     return "SIBLING_OVERLAP";
        case SV_ZERO_SIZE:           return "ZERO_SIZE";
        case SV_CHILD_EXCEEDS_PARENT:return "CHILD_EXCEEDS_PARENT";
        case SV_CLIPPED:             return "CLIPPED";
        default:                     return "?";
    }
}

static void sv_dump(const sv_report_t* r, const char* screen_id) {
    std::printf("  [validate] screen '%s': %d violation(s), %d fail(s)\n",
                screen_id, r->count, r->fail_count);
    int stored = r->count < SV_MAX_VIOLATIONS ? r->count : SV_MAX_VIOLATIONS;
    for (int i = 0; i < stored; ++i) {
        const sv_violation_t* v = &r->v[i];
        std::printf("    %-5s %-20s %s  a=(%d,%d)-(%d,%d)",
                    v->sev == SV_FAIL ? "FAIL" : "WARN",
                    rule_name(v->rule), v->a_desc,
                    (int)v->ra.x1, (int)v->ra.y1, (int)v->ra.x2, (int)v->ra.y2);
        if (v->b) {
            char db[48];
            /* second object: report its rect (desc is built only for a) */
            std::printf("  b=(%d,%d)-(%d,%d)",
                        (int)v->rb.x1, (int)v->rb.y1, (int)v->rb.x2, (int)v->rb.y2);
            (void)db;
        }
        std::printf("\n");
    }
}

/* ── Shared one-time setup ──────────────────────────────────────────────────── */

static void ensure_env(void) {
    static bool done = false;
    if (done) return;
    sim_display_get();   /* lv_init + in-memory display */
    ui_theme::apply();   /* dark theme + styles, as on device */
    done = true;
}

/* ── Per-screen check ───────────────────────────────────────────────────────── */

/* Validate one screen; returns its FAIL count. Does NOT assert, so the sweep
 * can report EVERY screen's result before the final assertion. */
static int check_screen(const sim_screen_t* s) {
    ensure_env();
    screen_validate_reset_allow();

    lv_obj_t* root = s->create();
    if (!root) { std::printf("  [validate] screen '%s': create() returned NULL\n", s->id); return 1; }
    lv_obj_update_layout(root);

    sv_report_t r;
    screen_validate_run(root, &r);
    if (r.count) sv_dump(&r, s->id);
    else         std::printf("  [validate] screen '%s': clean\n", s->id);

    int fails = r.fail_count;

    /* Clean up: load a throwaway screen so we can delete this root safely. */
    lv_obj_t* blank = lv_obj_create(NULL);
    lv_screen_load(blank);
    lv_obj_delete(root);
    return fails;
}

/* Sweep ALL screens, accumulate total FAILs, then assert once at the end so the
 * CI log shows every screen's result (no abort on the first failure). */
static void test_all_screens(void) {
    int total_fail = 0;
    for (int i = 0; i < sim_registry_count; ++i) {
        std::printf("[validate] checking '%s'...\n", sim_registry[i].id);
        total_fail += check_screen(&sim_registry[i]);
    }
    std::printf("[validate] TOTAL fail-severity violations across all screens: %d\n", total_fail);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, total_fail, "one or more screens have FAIL-severity layout violations");
}

/* ── Cascade verification ───────────────────────────────────────────────────── */
/* Build claude_widget, run a SINGLE root-level update_layout, then prove the
 * nested flex (content container → the two make_bar_row column blocks) got real,
 * vertically-distinct coordinates — i.e. layout cascaded through nesting. */
static void test_cascade_layout(void) {
    ensure_env();

    const sim_screen_t* cw = nullptr;
    for (int i = 0; i < sim_registry_count; ++i)
        if (std::strcmp(sim_registry[i].id, "claude_widget") == 0) cw = &sim_registry[i];
    TEST_ASSERT_NOT_NULL_MESSAGE(cw, "claude_widget not in registry");

    lv_obj_t* root = cw->create();
    TEST_ASSERT_NOT_NULL(root);
    lv_obj_update_layout(root);   /* single root-level cascade */

    /* claude_widget children of the screen: topbar(0), content(1), botbar(2). */
    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(3, (int)lv_obj_get_child_count(root),
                                             "claude screen child count");
    lv_obj_t* content = lv_obj_get_child(root, 1);
    TEST_ASSERT_NOT_NULL(content);

    /* content flex children (in creation order):
     *   0 badge_expired, 1 status, 2 bar-row-5h(col), 3 bar-row-7d(col), 4 divider, 5 kv-row */
    int n = (int)lv_obj_get_child_count(content);
    std::printf("  [cascade] content has %d flex children\n", n);
    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(4, n, "content child count");

    lv_obj_t* bar5 = lv_obj_get_child(content, 2);
    lv_obj_t* bar7 = lv_obj_get_child(content, 3);
    TEST_ASSERT_NOT_NULL(bar5);
    TEST_ASSERT_NOT_NULL(bar7);

    lv_area_t a5, a7;
    lv_obj_get_coords(bar5, &a5);
    lv_obj_get_coords(bar7, &a7);
    std::printf("  [cascade] bar5=(%d,%d)-(%d,%d)  bar7=(%d,%d)-(%d,%d)\n",
                (int)a5.x1,(int)a5.y1,(int)a5.x2,(int)a5.y2,
                (int)a7.x1,(int)a7.y1,(int)a7.x2,(int)a7.y2);

    /* Non-zero size */
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, (int)(a5.x2 - a5.x1), "bar5 width");
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, (int)(a5.y2 - a5.y1), "bar5 height");
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, (int)(a7.y2 - a7.y1), "bar7 height");
    /* Vertically distinct & ordered (bar7 below bar5) — proves the flex stacked
     * the nested blocks rather than collapsing them all to (0,0). */
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE((int)a5.y1, (int)a7.y1,
                                         "bar7 must be below bar5 (cascade)");

    lv_obj_t* blank = lv_obj_create(NULL);
    lv_screen_load(blank);
    lv_obj_delete(root);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_all_screens);
    RUN_TEST(test_cascade_layout);
    return UNITY_END();
}

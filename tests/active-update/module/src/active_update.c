/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <cormoran/runtime_combo/runtime_combo.h>
#include <cormoran/zmk/custom_settings.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>

enum { REVIEW_MAX_POSITION = 33 };
BUILD_ASSERT(REVIEW_MAX_POSITION < ZMK_KEYMAP_LEN,
             "Every active-update fixture position must belong to the resolved matrix");
BUILD_ASSERT(DT_PROP_LEN(DT_PATH(keymap, default_layer), bindings) == ZMK_KEYMAP_LEN,
             "Default fixture bindings must cover the resolved matrix");
BUILD_ASSERT(DT_PROP_LEN(DT_PATH(keymap, alternate_layer), bindings) == ZMK_KEYMAP_LEN,
             "Alternate fixture bindings must cover the resolved matrix");

static void position(int32_t pos, bool pressed) {
    __ASSERT(pos >= 0 && pos < ZMK_KEYMAP_LEN, "Fixture position outside resolved keymap");
    raise_zmk_position_state_changed((struct zmk_position_state_changed){
        .position = pos,
        .state = pressed,
        .timestamp = k_uptime_get(),
    });
}

/* Observe the actual downstream keycode path. Raising new positions while
 * the first replayed E press is dispatched must not overwrite its saved tail. */
static bool replay_track;
static bool replay_inject;
static bool replay_release_active;
static unsigned replay_down[14], replay_up[14];
static int replay_keycode_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *event = as_zmk_keycode_state_changed(eh);
    if (!event || !replay_track ||
        ((event->keycode < ZMK_HID_USAGE_ID(E) || event->keycode > ZMK_HID_USAGE_ID(Q)) &&
         event->keycode != ZMK_HID_USAGE_ID(LSHFT))) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    unsigned index =
        event->keycode == ZMK_HID_USAGE_ID(LSHFT) ? 13 : event->keycode - ZMK_HID_USAGE_ID(E);
    if (event->state) {
        replay_down[index]++;
    } else {
        __ASSERT(replay_up[index] < replay_down[index], "Replay release preceded its press");
        replay_up[index]++;
    }
    if (event->state && event->keycode == ZMK_HID_USAGE_ID(E) && replay_inject) {
        replay_inject = false;
        for (unsigned pos = 18; pos < 33; pos++) {
            position(pos, true);
        }
    }
    if (event->state && event->keycode == ZMK_HID_USAGE_ID(LSHFT) && replay_release_active) {
        replay_release_active = false;
        position(8, false);
        position(9, false);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(runtime_combo_replay_keys, replay_keycode_listener);
ZMK_SUBSCRIPTION(runtime_combo_replay_keys, zmk_keycode_state_changed);

/* Controlled persistence result; no flash or power-loss acceptance claim. */
static int save_error;
K_SEM_DEFINE(body_written, 0, 1);
K_SEM_DEFINE(resume_body_write, 0, 1);
static bool pause_body_write;
static int save(struct settings_store *store, const char *name, const char *value, size_t len) {
    ARG_UNUSED(store);
    ARG_UNUSED(value);
    ARG_UNUSED(len);
    ARG_UNUSED(name);
    return save_error;
}

/* Pause a real module write after the body change has left the registry guard
 * and before its binding write. The cache mark makes this interleaving
 * independent of the linker order of the two change-event listeners. */
static int body_change_listener(const zmk_event_t *eh) {
    const struct zmk_custom_setting_changed *changed = as_zmk_custom_setting_changed(eh);
    if (pause_body_write && changed && changed->setting->array_index == 0 &&
        zmk_custom_setting_matches(changed->setting, ZMK_RUNTIME_COMBO_SUBSYSTEM_ID,
                                   ZMK_RUNTIME_COMBO_COMBOS_KEY, NULL)) {
        pause_body_write = false;
        zmk_runtime_combo_invalidate_cache();
        k_sem_give(&body_written);
        __ASSERT_NO_MSG(k_sem_take(&resume_body_write, K_SECONDS(2)) == 0);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(runtime_combo_review_body, body_change_listener);
ZMK_SUBSCRIPTION(runtime_combo_review_body, zmk_custom_setting_changed);

/* The boot load stores slow release on. A key press arrives while main() is
 * still loading, before that record is applied, as when the system work
 * queue preempts main(). Later targeted reloads find nothing here. */
static bool boot_loaded;
static ssize_t read_true(void *cb_arg, void *data, size_t len) {
    ARG_UNUSED(cb_arg);
    if (len < sizeof(bool)) {
        return -EINVAL;
    }
    *(bool *)data = true;
    return sizeof(bool);
}
static int load(struct settings_store *store, const struct settings_load_arg *arg) {
    ARG_UNUSED(store);
    if (boot_loaded) {
        return 0;
    }
    boot_loaded = true;
    position(0, true);
    position(0, false);
    return settings_call_set_handler("custom_settings/" ZMK_RUNTIME_COMBO_SUBSYSTEM_ID
                                     "/" ZMK_RUNTIME_COMBO_SLOW_RELEASE_KEY,
                                     sizeof(bool), read_true, NULL, arg);
}
static const struct settings_store_itf store_itf = {.csi_load = load, .csi_save = save};
static struct settings_store store = {.cs_itf = &store_itf};

/* settings_subsys_init() drops sources registered before it, and main() calls
 * it again (a no-op) before settings_load(). */
static int register_store(void) {
    int ret = settings_subsys_init();
    __ASSERT(ret == 0, "settings_subsys_init: %d", ret);
    settings_src_register(&store);
    settings_dst_register(&store);
    return ret;
}
SYS_INIT(register_store, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

static bool held(uint32_t key) { return zmk_hid_is_pressed(key); }
static struct zmk_runtime_combo_config config(uint32_t key) {
    return (struct zmk_runtime_combo_config){
        .enabled = true,
        .key_position_len = 2,
        .key_positions = {0, 1},
        .behavior = {.behavior_dev = "key_press", .param1 = key}};
}
static void start(struct zmk_runtime_combo_config *combo) {
    __ASSERT_NO_MSG(zmk_runtime_combo_write(0, combo, false) == 0);
    position(0, true);
    position(1, true);
    __ASSERT_NO_MSG(held(B));
}
/* The next activation of positions 0+1 types `key` (A: no combo matches). */
static void assert_activates(uint32_t key) {
    position(0, true);
    position(1, true);
    __ASSERT_NO_MSG(held(key));
    __ASSERT_NO_MSG(key == B || !held(B));
    position(0, false);
    position(1, false);
    __ASSERT_NO_MSG(!held(key));
}
static void finish(const char *name, uint32_t next) {
    position(0, false);
    position(1, false);
    __ASSERT_NO_MSG(!held(B));
    __ASSERT_NO_MSG(!held(C));
    if (next) {
        assert_activates(next);
    }
    printk("PASS active-update %s time=%lld\n", name, k_uptime_get());
}
/* The compile-time default (positions 2+3 -> D) stays held until its last key
 * is released. Nothing is written here, so a change only reaches the cache
 * through the path under test. */
static void assert_default_slow_release(const char *name) {
    position(2, true);
    position(3, true);
    __ASSERT_NO_MSG(held(D));
    position(2, false);
    __ASSERT_NO_MSG(held(D));
    position(3, false);
    __ASSERT_NO_MSG(!held(D));
    /* Back to plain release; the press also consumes the stale mark this
     * write leaves, so it cannot stand in for the next group's path. */
    __ASSERT_NO_MSG(zmk_runtime_combo_write_slow_release(false, false) == 0);
    position(2, true);
    position(3, true);
    position(2, false);
    __ASSERT_NO_MSG(!held(D));
    position(3, false);
    printk("PASS active-update %s time=%lld\n", name, k_uptime_get());
}

/* Use the real system work queue for position events and timeout callbacks.
 * The calling test thread waits for each input instead of racing HID reads. */
static int32_t pending_position_value;
static bool pending_position_state;
static int64_t pending_position_time;
static void pending_position_worker(struct k_work *work) {
    ARG_UNUSED(work);
    pending_position_time = k_uptime_get();
    position(pending_position_value, pending_position_state);
}
K_WORK_DEFINE(pending_position_work, pending_position_worker);
static int64_t pending_position(int32_t pos, bool pressed) {
    pending_position_value = pos;
    pending_position_state = pressed;
    __ASSERT_NO_MSG(k_work_submit(&pending_position_work) > 0);
    struct k_work_sync sync;
    k_work_flush(&pending_position_work, &sync);
    return pending_position_time;
}

static void pending_setup(void) {
    for (uint32_t slot = 0; slot < CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS; slot++) {
        const int ret = zmk_runtime_combo_delete(slot, false);
        __ASSERT_NO_MSG(ret == 0 || ret == -ENOENT);
    }
    __ASSERT_NO_MSG(zmk_runtime_combo_write_slow_release(false, false) == 0);
    __ASSERT_NO_MSG(zmk_runtime_combo_write_require_prior_idle_ms(0, false) == 0);
}

static void pending_test(const char *scenario) {
    pending_setup();
    struct zmk_runtime_combo_config first = config(B), second = config(C);
    first.timeout_ms = 200;
    second.timeout_ms = 800;
    if (!strcmp(scenario, "layer") || !strcmp(scenario, "layer-control") ||
        !strcmp(scenario, "idle") || !strcmp(scenario, "idle-first-press")) {
        if (!strcmp(scenario, "layer")) {
            first.layer_mask = BIT(1);
        }
        if (!strcmp(scenario, "layer-control")) {
            first.enabled = false;
        }
        if (!strcmp(scenario, "idle") || !strcmp(scenario, "idle-first-press")) {
            first.require_prior_idle_ms = !strcmp(scenario, "idle") ? 1000 : 100;
            first.timeout_ms = second.timeout_ms = 1000;
        }
        second.layer_mask = BIT(0);
        __ASSERT_NO_MSG(zmk_runtime_combo_write(0, &first, false) == 0);
        __ASSERT_NO_MSG(zmk_runtime_combo_write(1, &second, false) == 0);
        if (first.require_prior_idle_ms) {
            /* The ordinary tap must be newer than any preceding combo,
             * including when all previous groups completed in one tick. */
            k_sleep(K_MSEC(1));
            pending_position(3, true);
            pending_position(3, false);
        }
        int64_t t0 = pending_position(0, true);
        if (!strcmp(scenario, "idle-first-press")) {
            k_sleep(K_MSEC(200));
        }
        int64_t t1 = pending_position(1, true);
        printk("pending eligibility %s first=%lld second=%lld B=%d C=%d\n", scenario, t0, t1,
               held(B), held(C));
        __ASSERT_NO_MSG(held(C) && !held(B));
        pending_position(0, false);
        pending_position(1, false);
    } else if (!strcmp(scenario, "overlap-early") || !strcmp(scenario, "overlap-late") ||
               !strcmp(scenario, "overlap-expiry") || !strcmp(scenario, "overlap-unrelated")) {
        second.key_position_len = 3;
        second.key_positions[2] = 2;
        __ASSERT_NO_MSG(zmk_runtime_combo_write(0, &first, false) == 0);
        __ASSERT_NO_MSG(zmk_runtime_combo_write(1, &second, false) == 0);
        int64_t t0 = pending_position(0, true);
        pending_position(1, true);
        __ASSERT_NO_MSG(!held(B) && !held(C));
        if (!strcmp(scenario, "overlap-expiry")) {
            k_sleep(K_MSEC(900));
            printk("pending overlap expiry first=%lld now=%lld B=%d C=%d\n", t0, k_uptime_get(),
                   held(B), held(C));
            __ASSERT_NO_MSG(held(B) && !held(C) && !held(A));
        } else {
            k_sleep(K_MSEC(!strcmp(scenario, "overlap-early") ? 20 : 300));
            __ASSERT_NO_MSG(k_uptime_get() - t0 < 800);
            if (!strcmp(scenario, "overlap-unrelated")) {
                pending_position(3, true);
                __ASSERT_NO_MSG(held(B) && held(A) && !held(C));
                pending_position(3, false);
            } else {
                int64_t t2 = pending_position(2, true);
                printk("pending overlap %s first=%lld third=%lld B=%d C=%d\n", scenario, t0, t2,
                       held(B), held(C));
                __ASSERT_NO_MSG(held(C) && !held(B));
                pending_position(2, false);
            }
        }
        pending_position(0, false);
        pending_position(1, false);
    } else if (!strcmp(scenario, "timer-retime") || !strcmp(scenario, "timer-control")) {
        first.enabled = strcmp(scenario, "timer-control") != 0;
        second.key_position_len = 3;
        second.key_positions[1] = 2;
        second.key_positions[2] = 3;
        __ASSERT_NO_MSG(zmk_runtime_combo_write(0, &first, false) == 0);
        __ASSERT_NO_MSG(zmk_runtime_combo_write(1, &second, false) == 0);
        int64_t t0 = pending_position(0, true);
        k_sleep(K_MSEC(20));
        int64_t t2 = pending_position(2, true);
        __ASSERT_NO_MSG(t2 - t0 < 200);
        k_sleep(K_MSEC(900));
        printk("pending timeout %s first=%lld second=%lld now=%lld A=%d B=%d C=%d\n", scenario, t0,
               t2, k_uptime_get(), held(A), held(B), held(C));
        __ASSERT_NO_MSG(held(A) && !held(B) && !held(C));
        pending_position(0, false);
        pending_position(2, false);
    } else {
        __ASSERT(false, "Unknown pending scenario: %s", scenario);
    }
    __ASSERT_NO_MSG(!held(A) && !held(B) && !held(C));
    printk("PASS pending %s time=%lld\n", scenario, k_uptime_get());
}

static void pending_tests(void) {
    const char *scenarios[] = {
        "layer",        "idle",           "idle-first-press",  "layer-control", "overlap-early",
        "overlap-late", "overlap-expiry", "overlap-unrelated", "timer-retime",  "timer-control"};
    for (size_t i = 0; i < ARRAY_SIZE(scenarios); i++) {
        pending_test(scenarios[i]);
    }
    printk("runtime combo pending: PASS groups=%u\n", (unsigned)ARRAY_SIZE(scenarios));
}

K_SEM_DEFINE(review_writer_start, 0, 1);
K_SEM_DEFINE(review_writer_done, 0, 1);
K_SEM_DEFINE(review_reader_start, 0, 1);
K_SEM_DEFINE(review_reader_entered, 0, 1);
K_SEM_DEFINE(review_reader_done, 0, 1);
static struct zmk_runtime_combo_config review_write_config, review_read_config;
static int review_write_result, review_read_result;

static void review_writer(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    for (;;) {
        k_sem_take(&review_writer_start, K_FOREVER);
        review_write_result = zmk_runtime_combo_write(0, &review_write_config, true);
        k_sem_give(&review_writer_done);
    }
}
K_THREAD_DEFINE(review_writer_thread, 4096, review_writer, NULL, NULL, NULL, 5, 0, 0);

static void review_reader(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    for (;;) {
        k_sem_take(&review_reader_start, K_FOREVER);
        k_sem_give(&review_reader_entered);
        review_read_result = zmk_runtime_combo_read(0, &review_read_config);
        k_sem_give(&review_reader_done);
    }
}
K_THREAD_DEFINE(review_reader_thread, 2048, review_reader, NULL, NULL, NULL, 5, 0, 0);

static struct zmk_runtime_combo_config review_config(uint32_t key, uint32_t first,
                                                     uint32_t second) {
    struct zmk_runtime_combo_config combo = config(key);
    combo.key_positions[0] = first;
    combo.key_positions[1] = second;
    combo.timeout_ms = 200;
    return combo;
}

static void review_replay(const char *scenario) {
    struct zmk_runtime_combo_config combo = review_config(B, 4, 5);
    combo.key_position_len = 3;
    combo.key_positions[2] = 6;
    __ASSERT_NO_MSG(zmk_runtime_combo_write(0, &combo, false) == 0);
    pending_position(4, true);
    pending_position(5, true);
    __ASSERT_NO_MSG(!held(E) && !held(F) && !held(B));
    if (!strcmp(scenario, "replay-control")) {
        pending_position(6, true);
        __ASSERT_NO_MSG(held(B) && !held(E) && !held(F) && !held(G));
        pending_position(6, false);
    } else if (!strcmp(scenario, "replay-release")) {
        pending_position(5, false);
        k_sleep(K_MSEC(300));
        __ASSERT_NO_MSG(held(E) && !held(F) && !held(B));
    } else {
        pending_position(7, true);
        __ASSERT_NO_MSG(held(E) && held(F) && held(H) && !held(B));
        pending_position(7, false);
        pending_position(5, false);
    }
    pending_position(4, false);
    pending_position(5, false);
    __ASSERT_NO_MSG(!held(E) && !held(F) && !held(G) && !held(H) && !held(B));
}

static void review_replay_bounds(const char *scenario) {
    BUILD_ASSERT(CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE == 2048,
                 "Replay regressions retain the ordinary system workqueue stack");
    BUILD_ASSERT(CONFIG_ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO == 16);
    memset(replay_down, 0, sizeof(replay_down));
    memset(replay_up, 0, sizeof(replay_up));
    replay_inject = false;
    replay_release_active = false;
    replay_track = true;
    if (!strcmp(scenario, "replay-max")) {
        struct zmk_runtime_combo_config combo = review_config(B, 4, 5);
        combo.key_position_len = 16;
        for (unsigned i = 0; i < 13; i++) {
            combo.key_positions[i] = 4 + i;
        }
        combo.key_positions[13] = 18;
        combo.key_positions[14] = 19;
        combo.key_positions[15] = 20;
        __ASSERT_NO_MSG(zmk_runtime_combo_write(0, &combo, false) == 0);
        for (unsigned i = 0; i < 15; i++) {
            pending_position(combo.key_positions[i], true);
        }
        pending_position(19, false);
        __ASSERT_NO_MSG(held(E) && held(F) && !held(B));
        for (unsigned i = 0; i < 13; i++) {
            __ASSERT_NO_MSG(replay_down[i] == 1 && replay_up[i] == 0);
            pending_position(4 + i, false);
        }
        __ASSERT_NO_MSG(replay_down[13] == 2 && replay_up[13] == 1);
        pending_position(18, false);
        for (unsigned i = 0; i < 13; i++) {
            __ASSERT_NO_MSG(replay_down[i] == 1 && replay_up[i] == 1);
        }
        __ASSERT_NO_MSG(replay_down[13] == 2 && replay_up[13] == 2);
    } else {
        struct zmk_runtime_combo_config first = review_config(B, 4, 5);
        first.key_position_len = 4;
        first.key_positions[2] = 6;
        first.key_positions[3] = 7;
        struct zmk_runtime_combo_config second = review_config(C, 18, 19);
        second.key_position_len = 16;
        for (unsigned i = 0; i < 16; i++) {
            second.key_positions[i] = 18 + i;
        }
        __ASSERT_NO_MSG(zmk_runtime_combo_write(0, &first, false) == 0);
        __ASSERT_NO_MSG(zmk_runtime_combo_write(1, &second, false) == 0);
        struct zmk_runtime_combo_config active = review_config(Q, 8, 9);
        __ASSERT_NO_MSG(zmk_runtime_combo_write(2, &active, false) == 0);
        pending_position(8, true);
        pending_position(9, true);
        __ASSERT_NO_MSG(held(Q));
        pending_position(4, true);
        pending_position(5, true);
        pending_position(6, true);
        replay_inject = true;
        replay_release_active = true;
        pending_position(6, false);
        __ASSERT(!replay_inject && !replay_release_active && held(E) && held(F) && !held(G) &&
                     !held(B) && !held(C) && !held(Q),
                 "Reentrant positions overwrote the immutable replay tail");
        __ASSERT_NO_MSG(replay_down[0] == 1 && replay_up[0] == 0);
        __ASSERT_NO_MSG(replay_down[1] == 1 && replay_up[1] == 0);
        __ASSERT_NO_MSG(replay_down[2] == 1 && replay_up[2] == 1);
        __ASSERT(replay_down[13] == 15 && replay_up[13] == 0,
                 "Reentrant replay lost an accepted press");
        pending_position(4, false);
        pending_position(5, false);
        for (unsigned pos = 18; pos < 33; pos++) {
            pending_position(pos, false);
        }
        __ASSERT(replay_down[13] == 15 && replay_up[13] == 15,
                 "Reentrant replay lost an accepted release");
        __ASSERT_NO_MSG(replay_up[0] == 1 && replay_up[1] == 1);
    }
    replay_track = false;
    __ASSERT_NO_MSG(!held(E) && !held(F) && !held(G) && !held(LSHFT) && !held(B) && !held(C));
}

static void review_name(const char *scenario) {
    struct zmk_runtime_combo_config shorter = review_config(B, 4, 5), longer = shorter;
    shorter.slow_release_override = ZMK_RUNTIME_COMBO_SLOW_RELEASE_ON;
    longer.behavior.param1 = C;
    longer.key_position_len = 3;
    longer.key_positions[2] = 6;
    longer.timeout_ms = 800;
    __ASSERT_NO_MSG(zmk_runtime_combo_write(0, &shorter, false) == 0);
    __ASSERT_NO_MSG(zmk_runtime_combo_write(1, &longer, false) == 0);
    pending_position(4, true);
    pending_position(5, true);
    __ASSERT_NO_MSG(!held(B) && !held(C));
    uint32_t slot = !strcmp(scenario, "name-same-slot") ? 0 : 6;
    __ASSERT_NO_MSG(zmk_runtime_combo_write_name(slot, "display only", false) == 0);
    pending_position(5, false);
    __ASSERT_NO_MSG(held(B) && !held(C) && !held(E) && !held(F));
    pending_position(4, false);
    __ASSERT_NO_MSG(!held(B));
}

static void review_capacity(void) {
    const uint32_t keys[] = {B, C, D, Q};
    BUILD_ASSERT(CONFIG_ZMK_COMBO_MAX_PRESSED_COMBOS == ARRAY_SIZE(keys));
    for (size_t i = 0; i < ARRAY_SIZE(keys); i++) {
        struct zmk_runtime_combo_config combo = review_config(keys[i], 4 + 2 * i, 5 + 2 * i);
        __ASSERT_NO_MSG(zmk_runtime_combo_write(i, &combo, false) == 0);
        pending_position(4 + 2 * i, true);
        pending_position(5 + 2 * i, true);
        __ASSERT_NO_MSG(held(keys[i]));
    }
    struct zmk_runtime_combo_config shorter = review_config(R, 12, 13), longer = shorter;
    longer.behavior.param1 = S;
    longer.key_position_len = 3;
    longer.key_positions[2] = 14;
    __ASSERT_NO_MSG(zmk_runtime_combo_write(4, &shorter, false) == 0);
    __ASSERT_NO_MSG(zmk_runtime_combo_write(5, &longer, false) == 0);
    pending_position(12, true);
    pending_position(13, true);
    pending_position(13, false);
    __ASSERT_NO_MSG(held(M) && !held(N) && !held(R) && !held(S));
    pending_position(12, false);
    __ASSERT_NO_MSG(!held(M));
    for (size_t i = 0; i < ARRAY_SIZE(keys); i++) {
        __ASSERT_NO_MSG(held(keys[i]));
        pending_position(4 + 2 * i, false);
        pending_position(5 + 2 * i, false);
        __ASSERT_NO_MSG(!held(keys[i]));
    }
}

static void review_pair(void) {
    struct zmk_runtime_combo_config old = review_config(B, 4, 5);
    __ASSERT_NO_MSG(zmk_runtime_combo_write(0, &old, false) == 0);
    pending_position(4, true);
    review_write_config = review_config(C, 4, 6);
    pause_body_write = true;
    k_sem_give(&review_writer_start);
    __ASSERT_NO_MSG(k_sem_take(&body_written, K_SECONDS(1)) == 0);
    k_sem_give(&review_reader_start);
    __ASSERT_NO_MSG(k_sem_take(&review_reader_entered, K_SECONDS(1)) == 0);
    __ASSERT_NO_MSG(k_sem_take(&review_reader_done, K_MSEC(20)) == -EAGAIN);
    /* The real system work queue must make progress while the writer pauses;
     * the old complete body does not match positions 4+6. */
    pending_position(6, true);
    __ASSERT_NO_MSG(held(E) && held(G) && !held(B) && !held(C));
    pending_position(4, false);
    pending_position(6, false);
    k_sem_give(&resume_body_write);
    __ASSERT_NO_MSG(k_sem_take(&review_writer_done, K_SECONDS(1)) == 0);
    __ASSERT_NO_MSG(k_sem_take(&review_reader_done, K_SECONDS(1)) == 0);
    __ASSERT_NO_MSG(review_write_result == 0 && review_read_result == 0);
    __ASSERT_NO_MSG(review_read_config.key_positions[0] == 4 &&
                    review_read_config.key_positions[1] == 6 &&
                    review_read_config.behavior.param1 == C);
    pending_position(4, true);
    pending_position(6, true);
    __ASSERT_NO_MSG(held(C) && !held(B) && !held(E) && !held(G));
    pending_position(4, false);
    pending_position(6, false);
    __ASSERT_NO_MSG(!held(C));
}

struct review_stored_binding {
    zmk_behavior_local_id_t behavior_id;
    uint32_t param1;
    uint32_t param2;
} __packed;

static ssize_t read_review_binding(void *arg, void *data, size_t len) {
    if (len < sizeof(struct review_stored_binding)) {
        return -EINVAL;
    }
    memcpy(data, arg, sizeof(struct review_stored_binding));
    return sizeof(struct review_stored_binding);
}

static void review_stored_binding(void) {
    BUILD_ASSERT(ZMK_KEYMAP_LAYERS_LEN == 2,
                 "The first invalid layer parameter is distinct from the position count");
    struct zmk_runtime_combo_config combo = review_config(1, 4, 5);
    combo.behavior.behavior_dev = "momentary_layer";
    __ASSERT_NO_MSG(zmk_runtime_combo_write(0, &combo, false) == 0);
    struct review_stored_binding record = {
        .behavior_id = zmk_behavior_get_local_id(combo.behavior.behavior_dev),
    };
    const uint32_t parameters[] = {2, 256, 257, UINT32_MAX, 1};
    for (size_t i = 0; i < ARRAY_SIZE(parameters); i++) {
        record.param1 = parameters[i];
        __ASSERT_NO_MSG(settings_call_set_handler(
                            "custom_settings/" ZMK_RUNTIME_COMBO_SUBSYSTEM_ID "/behaviors/0",
                            sizeof(record), read_review_binding, &record, NULL) == 0);
        struct zmk_custom_setting_value stored;
        __ASSERT_NO_MSG(zmk_custom_setting_read_array_by_key(ZMK_RUNTIME_COMBO_SUBSYSTEM_ID,
                                                             ZMK_RUNTIME_COMBO_BEHAVIORS_KEY, 0,
                                                             &stored) == 0);
        __ASSERT_NO_MSG(stored.type == ZMK_CUSTOM_SETTING_VALUE_TYPE_BEHAVIOR &&
                        stored.behavior_value.behavior_id == record.behavior_id &&
                        stored.behavior_value.param1 == parameters[i] &&
                        stored.behavior_value.param2 == record.param2);
        zmk_runtime_combo_invalidate_cache();
        struct zmk_runtime_combo_config actual;
        int ret = zmk_runtime_combo_read(0, &actual);
        __ASSERT_NO_MSG(parameters[i] == 1 ? ret == 0 : ret == -EINVAL);
        pending_position(4, true);
        pending_position(5, true);
        __ASSERT_NO_MSG(zmk_keymap_layer_active(1) == (parameters[i] == 1));
        pending_position(4, false);
        pending_position(5, false);
        __ASSERT_NO_MSG(!zmk_keymap_layer_active(1) && !held(E) && !held(F));

        combo.behavior.param1 = 1;
        __ASSERT_NO_MSG(zmk_runtime_combo_write(0, &combo, false) == 0);
        struct zmk_runtime_combo_config attempted = combo;
        attempted.behavior.param1 = parameters[i];
        int write_ret = zmk_runtime_combo_write(0, &attempted, false);
        __ASSERT_NO_MSG(parameters[i] == 1 ? write_ret == 0 : write_ret == -EINVAL);
        struct zmk_runtime_combo_config unchanged;
        __ASSERT_NO_MSG(zmk_runtime_combo_read(0, &unchanged) == 0);
        __ASSERT_NO_MSG(unchanged.behavior.param1 == 1);
    }
}

static void review_name_bounds(void) {
    char boundary[ZMK_RUNTIME_COMBO_NAME_MAX_BYTES + 2];
    memset(boundary, 'x', sizeof(boundary));
    boundary[ZMK_RUNTIME_COMBO_NAME_MAX_BYTES] = '\0';
    __ASSERT_NO_MSG(zmk_runtime_combo_write_name(0, boundary, false) == 0);
    boundary[ZMK_RUNTIME_COMBO_NAME_MAX_BYTES] = 'x';
    boundary[ZMK_RUNTIME_COMBO_NAME_MAX_BYTES + 1] = '\0';
    __ASSERT_NO_MSG(zmk_runtime_combo_write_name(0, boundary, false) == -ENAMETOOLONG);
    char actual[sizeof(boundary)];
    __ASSERT_NO_MSG(zmk_runtime_combo_read_name(0, actual, sizeof(actual)) == 0);
    __ASSERT_NO_MSG(strlen(actual) == ZMK_RUNTIME_COMBO_NAME_MAX_BYTES);
}

static void review_test(const char *scenario) {
    pending_setup();
    if (!strcmp(scenario, "replay-max") || !strcmp(scenario, "replay-reentrant")) {
        review_replay_bounds(scenario);
    } else if (!strncmp(scenario, "replay-", 7)) {
        review_replay(scenario);
    } else if (!strcmp(scenario, "name-same-slot") || !strcmp(scenario, "name-other-slot")) {
        review_name(scenario);
    } else if (!strcmp(scenario, "full-active-table")) {
        review_capacity();
    } else if (!strcmp(scenario, "paired-update")) {
        review_pair();
    } else if (!strcmp(scenario, "stored-binding")) {
        review_stored_binding();
    } else if (!strcmp(scenario, "name-bounds")) {
        review_name_bounds();
    } else {
        __ASSERT(false, "Unknown review scenario: %s", scenario);
    }
    printk("PASS review %s time=%lld\n", scenario, k_uptime_get());
}

static void review_tests(void) {
    const char *scenarios[] = {"replay-release", "replay-unrelated", "replay-control",
                               "name-same-slot", "name-other-slot",  "full-active-table",
                               "paired-update",  "stored-binding",   "name-bounds"};
    for (size_t i = 0; i < ARRAY_SIZE(scenarios); i++) {
        review_test(scenarios[i]);
    }
    printk("runtime combo review: PASS groups=%u\n", (unsigned)ARRAY_SIZE(scenarios));
    review_test("replay-max");
    review_test("replay-reentrant");
    printk("runtime combo replay: PASS groups=2\n");
}

static void tests(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    __ASSERT_NO_MSG(boot_loaded);
    __ASSERT_NO_MSG(!held(A));
    /* Focused failing-before controls use the same production binary and
     * assertions. The canonical runner requires the complete default path. */
    const char *pending_case = getenv("COMBO_PENDING_CASE");
    if (pending_case) {
        pending_test(pending_case);
        exit(0);
    }
    const char *review_case = getenv("COMBO_REVIEW_CASE");
    if (review_case) {
        review_test(review_case);
        exit(0);
    }
    assert_default_slow_release("boot-load");

    struct zmk_runtime_combo_config combo = config(B);
    start(&combo);
    combo.behavior.param1 = C;
    __ASSERT_NO_MSG(zmk_runtime_combo_write(0, &combo, false) == 0);
    finish("binding", C);

    combo = config(B);
    start(&combo);
    combo.key_position_len = 3;
    combo.key_positions[2] = 2;
    __ASSERT_NO_MSG(zmk_runtime_combo_write(0, &combo, false) == 0);
    finish("position-count", 0);

    combo = config(B);
    start(&combo);
    __ASSERT_NO_MSG(zmk_runtime_combo_reset(0) == 0);
    finish("reset-empty", A);

    combo = config(B);
    start(&combo);
    save_error = -EIO;
    __ASSERT_NO_MSG(zmk_runtime_combo_reset(0) == -EIO);
    save_error = 0;
    finish("reset-save-failure", A);

    combo = config(B);
    start(&combo);
    combo.behavior.param1 = C;
    save_error = -EIO;
    __ASSERT_NO_MSG(zmk_runtime_combo_write(0, &combo, true) == -EIO);
    save_error = 0;
    finish("write-save-failure", C);

    combo = config(B);
    combo.slow_release_override = ZMK_RUNTIME_COMBO_SLOW_RELEASE_ON;
    start(&combo);
    position(0, false);
    __ASSERT_NO_MSG(held(B));
    combo.slow_release_override = ZMK_RUNTIME_COMBO_SLOW_RELEASE_OFF;
    __ASSERT_NO_MSG(zmk_runtime_combo_write(0, &combo, false) == 0);
    position(1, false);
    __ASSERT_NO_MSG(!held(B));
    printk("PASS active-update release-policy time=%lld\n", k_uptime_get());

    combo = config(B);
    __ASSERT_NO_MSG(zmk_runtime_combo_write_slow_release(true, false) == 0);
    start(&combo);
    position(0, false);
    __ASSERT_NO_MSG(held(B));
    __ASSERT_NO_MSG(zmk_runtime_combo_write_slow_release(false, false) == 0);
    position(1, false);
    __ASSERT_NO_MSG(!held(B));
    printk("PASS active-update global-release-policy time=%lld\n", k_uptime_get());

    save_error = -EIO;
    __ASSERT_NO_MSG(zmk_runtime_combo_write_slow_release(true, true) == -EIO);
    save_error = 0;
    assert_default_slow_release("global-save-failure");

    /* A writer past this module's API, as the generic custom settings RPC. */
    __ASSERT_NO_MSG(zmk_custom_setting_write_by_key(ZMK_RUNTIME_COMBO_SUBSYSTEM_ID,
                                                    ZMK_RUNTIME_COMBO_SLOW_RELEASE_KEY,
                                                    &ZMK_CUSTOM_SETTING_VALUE_BOOL(true),
                                                    ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY) == 0);
    assert_default_slow_release("generic-write");

    combo = config(B);
    start(&combo);
    __ASSERT_NO_MSG(zmk_runtime_combo_delete(0, false) == 0);
    finish("disable", A);

    /* A combo held back for a longer candidate is not fired once its slot is
     * disabled meanwhile; its keys replay as plain presses. */
    struct zmk_runtime_combo_config longer = config(C);
    longer.key_position_len = 3;
    longer.key_positions[2] = 2;
    __ASSERT_NO_MSG(zmk_runtime_combo_write(1, &longer, false) == 0);
    combo = config(B);
    combo.slow_release_override = ZMK_RUNTIME_COMBO_SLOW_RELEASE_ON;
    __ASSERT_NO_MSG(zmk_runtime_combo_write(0, &combo, false) == 0);
    position(0, true);
    position(1, true);
    __ASSERT_NO_MSG(!held(B) && !held(C));
    __ASSERT_NO_MSG(zmk_runtime_combo_delete(0, false) == 0);
    position(0, false);
    __ASSERT_NO_MSG(!held(B) && !held(C));
    position(1, false);
    __ASSERT_NO_MSG(!held(A) && !held(B) && !held(C));
    printk("PASS active-update held-back-edit time=%lld\n", k_uptime_get());

    printk("runtime combo active-update: PASS groups=12\n");
    pending_tests();
    review_tests();
    exit(0);
}
K_THREAD_DEFINE(active_update_test, 4096, tests, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO,
                0, 150);

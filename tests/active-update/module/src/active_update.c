/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <errno.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <cormoran/runtime_combo/runtime_combo.h>
#include <cormoran/zmk/custom_settings.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/hid.h>

static void position(int32_t pos, bool pressed) {
    raise_zmk_position_state_changed((struct zmk_position_state_changed){
        .position = pos,
        .state = pressed,
        .timestamp = k_uptime_get(),
    });
}

/* Controlled persistence result; no flash or power-loss acceptance claim. */
static int save_error;
static int save(struct settings_store *store, const char *name, const char *value, size_t len) {
    ARG_UNUSED(store);
    ARG_UNUSED(name);
    ARG_UNUSED(value);
    ARG_UNUSED(len);
    return save_error;
}

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
static void tests(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    __ASSERT_NO_MSG(boot_loaded);
    __ASSERT_NO_MSG(!held(A));
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
    exit(0);
}
K_THREAD_DEFINE(active_update_test, 4096, tests, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO,
                0, 150);

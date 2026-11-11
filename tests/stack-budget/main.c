/* SPDX-License-Identifier: MIT */
/* Actual DYA matching, events, behaviors, HID and Zephyr work queue on ARM.
 * Fixed in-range settings/input; storage/physical transport are not exercised. */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <cormoran/runtime_combo/runtime_combo.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>

BUILD_ASSERT(CONFIG_ARM &&CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE == 8192);
BUILD_ASSERT(CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS == 8);
BUILD_ASSERT(CONFIG_ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO == 16);
BUILD_ASSERT(ZMK_KEYMAP_LEN == 34);

static const int64_t press_times[15] = {100000, 101100, 101650, 101925, 102060,
                                        102125, 102160, 102175, 102176, 102177,
                                        102178, 102179, 102180, 102181, 102182};
static struct {
    uint32_t key;
    bool pressed;
    int64_t timestamp;
} observed[32];
static unsigned observed_count;
static bool tracking;
static uintptr_t entry_sp, lowest_sp = UINTPTR_MAX;
static int64_t case_offset;

static int keycode_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *event = as_zmk_keycode_state_changed(eh);
    if (tracking && event) {
        uintptr_t sp;
        __asm__ volatile("mov %0, sp" : "=r"(sp));
        lowest_sp = MIN(lowest_sp, sp);
        __ASSERT(observed_count < ARRAY_SIZE(observed), "STACK_TRACE_COUNT");
        observed[observed_count].key = event->keycode;
        observed[observed_count].pressed = event->state;
        observed[observed_count].timestamp = event->timestamp;
        observed_count++;
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(runtime_combo_stack_keys, keycode_listener);
ZMK_SUBSCRIPTION(runtime_combo_stack_keys, zmk_keycode_state_changed);

static void position(uint32_t pos, bool pressed, int64_t timestamp) {
    __ASSERT(pos < ZMK_KEYMAP_LEN, "STACK_INPUT_POSITION");
    raise_zmk_position_state_changed((struct zmk_position_state_changed){
        .position = pos, .state = pressed, .timestamp = timestamp + case_offset});
}

static void input_worker(struct k_work *work) {
    ARG_UNUSED(work);
    __asm__ volatile("mov %0, sp" : "=r"(entry_sp));
    tracking = true;
    for (unsigned i = 0; i < ARRAY_SIZE(press_times); i++) {
        position(4 + i, true, press_times[i]);
    }
    position(3, true, 102183);
    for (unsigned i = 0; i < ARRAY_SIZE(press_times); i++) {
        position(4 + i, false, 102200 + i);
    }
    position(3, false, 102220);
    tracking = false;
}
K_WORK_DEFINE(input_work, input_worker);

static void run_case(unsigned count, int64_t offset) {
    observed_count = 0;
    lowest_sp = UINTPTR_MAX;
    case_offset = offset;
    const uint16_t idle[] = {2000, 1000, 500, 250, 120, 60, 30, 10};
    for (unsigned slot = 0; slot < ARRAY_SIZE(idle); slot++) {
        int ret = zmk_runtime_combo_delete(slot, false);
        __ASSERT(ret == 0 || ret == -ENOENT, "STACK_CLEAR");
    }
    for (unsigned slot = 0; slot < count; slot++) {
        struct zmk_runtime_combo_config combo = {
            .enabled = true,
            .timeout_ms = UINT16_MAX,
            .require_prior_idle_ms = idle[slot],
            .key_position_len = 16 - 2 * slot,
            .behavior = {.behavior_dev = "key_press", .param1 = B}};
        for (unsigned i = 0; i + 1 < combo.key_position_len; i++) {
            combo.key_positions[i] = 4 + slot + i;
        }
        combo.key_positions[combo.key_position_len - 1] = 33;
        __ASSERT(zmk_runtime_combo_write(slot, &combo, false) == 0, "STACK_SETTINGS");
    }
    __ASSERT(zmk_runtime_combo_write_slow_release(false, false) == 0, "STACK_GLOBAL");
    __ASSERT(zmk_runtime_combo_write_require_prior_idle_ms(0, false) == 0, "STACK_GLOBAL");
    __ASSERT(k_work_submit(&input_work) > 0, "STACK_SUBMIT");
    struct k_work_sync sync;
    k_work_flush(&input_work, &sync);
    __ASSERT(observed_count == 32, "STACK_TRACE_COUNT");
    for (unsigned i = 0; i < 32; i++) {
        bool pressed = i < 16;
        unsigned j = i % 16;
        uint32_t usage = j == 15 ? A : E + j;
        uint32_t expected_key = ZMK_HID_USAGE_ID(usage);
        int64_t time =
            pressed ? (j == 15 ? 102183 : press_times[j]) : (j == 15 ? 102220 : 102200 + j);
        __ASSERT(observed[i].key == expected_key && observed[i].pressed == pressed &&
                     observed[i].timestamp == time + offset,
                 "STACK_ORDER_TIME");
        __ASSERT(!zmk_hid_is_pressed(usage), "STACK_RELEASE");
        printk("COMBO_STACK_EVENT case=%u index=%u key=%u state=%u time=%lld\n", count, i,
               observed[i].key, observed[i].pressed, observed[i].timestamp);
    }
    __ASSERT(!zmk_hid_is_pressed(B), "STACK_UNEXPECTED_COMBO");
    size_t unused;
    __ASSERT(k_thread_stack_space_get(&k_sys_work_q.thread, &unused) == 0, "STACK_SAMPLE");
    size_t usable = k_sys_work_q.thread.stack_info.size;
    __ASSERT(usable >= unused && entry_sp >= lowest_sp, "STACK_SAMPLE");
    printk(
        "COMBO_STACK case=%u configured=%u usable=%u unused=%u used=%u replay_span=%u reports=%u\n",
        count, CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE, (unsigned)usable, (unsigned)unused,
        (unsigned)(usable - unused), (unsigned)(entry_sp - lowest_sp), observed_count);
    __ASSERT(unused >= 128, "COMBO_ARM_STACK_HEADROOM");
}
static void run(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    run_case(1, 0);
    run_case(8, 1000000);
    printk("COMBO_STACK_RESULT PASS groups=2 reports=64\n");
}
K_THREAD_DEFINE(stack_test, 4096, run, NULL, NULL, NULL, 10, 0, 100);

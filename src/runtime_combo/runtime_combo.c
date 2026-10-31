/*
 * Copyright (c) 2026 cormoran
 *
 * SPDX-License-Identifier: MIT
 */

#include "../runtime_combo_timeout.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <cormoran/runtime_combo/runtime_combo.h>
#include <cormoran/zmk/custom_settings.h>

#include <dt-bindings/zmk/keys.h>
#include <drivers/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/virtual_key_position.h>

#ifndef CONFIG_ZMK_RUNTIME_COMBO_FORK_LOG_LEVEL
#define CONFIG_ZMK_RUNTIME_COMBO_FORK_LOG_LEVEL 3
#endif
LOG_MODULE_REGISTER(zmk_runtime_combo_fork_runtime_combo, CONFIG_ZMK_RUNTIME_COMBO_FORK_LOG_LEVEL);

/* --- Storage --- */

/* A combo's behavior binding is stored in a parallel BEHAVIOR-typed array
 * (ZMK_RUNTIME_COMBO_BEHAVIORS_KEY), so the packed body only carries the rest
 * of its configuration: version, flags, position count, a reserved byte, the
 * layer mask, the timeout, the require-prior-idle time and the positions, all
 * little-endian. */
#define RUNTIME_COMBO_STORAGE_VERSION 4
#define RUNTIME_COMBO_FLAG_ENABLED BIT(0)
#define RUNTIME_COMBO_FLAG_SLOW_RELEASE_OVERRIDE BIT(1)
#define RUNTIME_COMBO_FLAG_SLOW_RELEASE_VALUE BIT(2)
#define RUNTIME_COMBO_PACKED_HEADER_SIZE 12
#define RUNTIME_COMBO_PACKED_MAX_SIZE                                                              \
    (RUNTIME_COMBO_PACKED_HEADER_SIZE +                                                            \
     (CONFIG_ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO * sizeof(uint16_t)))

BUILD_ASSERT(RUNTIME_COMBO_PACKED_MAX_SIZE <= CONFIG_ZMK_CUSTOM_SETTINGS_VALUE_MAX_SIZE,
             "Runtime combo packed value must fit in one custom setting bytes value");

#define RUNTIME_COMBO_EMPTY_BYTES                                                                  \
    ((struct zmk_custom_setting_value){.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_BYTES, .size = 0})

/* Every array element is seeded from its defaults entry, so each array needs a
 * full-length table. Bare aggregate initializers keep these constant under
 * strict -std=c11. Every default is empty and default_size is 0: the arrays
 * grow on write, and an element that has not grown reads back -ENOENT, which
 * falls back to the compile-time default. */
#define RUNTIME_COMBO_COMBOS_DEFAULT(i, _) {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_BYTES, .size = 0}
#define RUNTIME_COMBO_NAMES_DEFAULT(i, _)                                                          \
    {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_STRING, .size = 0, .string_value = ""}
#define RUNTIME_COMBO_BEHAVIORS_DEFAULT(i, _)                                                      \
    {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_BEHAVIOR,                                               \
     .behavior_value = {.behavior_id = 0, .param1 = 0, .param2 = 0}}

static const struct zmk_custom_setting_value
    runtime_combo_combos_defaults[CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS] = {
        LISTIFY(CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS, RUNTIME_COMBO_COMBOS_DEFAULT, (, ))};
static const struct zmk_custom_setting_value
    runtime_combo_names_defaults[CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS] = {
        LISTIFY(CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS, RUNTIME_COMBO_NAMES_DEFAULT, (, ))};
static const struct zmk_custom_setting_value
    runtime_combo_behaviors_defaults[CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS] = {
        LISTIFY(CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS, RUNTIME_COMBO_BEHAVIORS_DEFAULT, (, ))};

ZMK_CUSTOM_SETTING_ARRAY_DEFINE(runtime_combo_combos, ZMK_RUNTIME_COMBO_SUBSYSTEM_ID,
                                ZMK_RUNTIME_COMBO_COMBOS_KEY, ZMK_CUSTOM_SETTING_VALUE_TYPE_BYTES,
                                CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS, 0,
                                runtime_combo_combos_defaults,
                                ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC,
                                ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
                                ZMK_CUSTOM_SETTING_PERMISSION_SECURE,
                                ZMK_CUSTOM_SETTING_NO_CONSTRAINT);

ZMK_CUSTOM_SETTING_ARRAY_DEFINE(runtime_combo_names, ZMK_RUNTIME_COMBO_SUBSYSTEM_ID,
                                ZMK_RUNTIME_COMBO_NAMES_KEY, ZMK_CUSTOM_SETTING_VALUE_TYPE_STRING,
                                CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS, 0,
                                runtime_combo_names_defaults,
                                ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC,
                                ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
                                ZMK_CUSTOM_SETTING_PERMISSION_SECURE,
                                ZMK_CUSTOM_SETTING_NO_CONSTRAINT);

ZMK_CUSTOM_SETTING_ARRAY_DEFINE(
    runtime_combo_behaviors, ZMK_RUNTIME_COMBO_SUBSYSTEM_ID, ZMK_RUNTIME_COMBO_BEHAVIORS_KEY,
    ZMK_CUSTOM_SETTING_VALUE_TYPE_BEHAVIOR, CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS, 0,
    runtime_combo_behaviors_defaults, ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC,
    ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE, ZMK_CUSTOM_SETTING_PERMISSION_SECURE,
    ZMK_CUSTOM_SETTING_NO_CONSTRAINT);

ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(
    runtime_combo_timeout_ms, ZMK_RUNTIME_COMBO_SUBSYSTEM_ID, ZMK_RUNTIME_COMBO_TIMEOUT_MS_KEY,
    ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
    ZMK_CUSTOM_SETTING_VALUE_INT32(CONFIG_ZMK_RUNTIME_COMBO_DEFAULT_TIMEOUT_MS),
    ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
    ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_RANGE_INT32(1, 65535));

ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(
    runtime_combo_slow_release, ZMK_RUNTIME_COMBO_SUBSYSTEM_ID, ZMK_RUNTIME_COMBO_SLOW_RELEASE_KEY,
    ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, ZMK_CUSTOM_SETTING_VALUE_BOOL(false),
    ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
    ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_NO_CONSTRAINT);

ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(
    runtime_combo_require_prior_idle_ms, ZMK_RUNTIME_COMBO_SUBSYSTEM_ID,
    ZMK_RUNTIME_COMBO_REQUIRE_PRIOR_IDLE_MS_KEY, ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
    ZMK_CUSTOM_SETTING_VALUE_INT32(CONFIG_ZMK_RUNTIME_COMBO_DEFAULT_REQUIRE_PRIOR_IDLE_MS),
    ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
    ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_RANGE_INT32(0, 65535));

/* --- Compile-time defaults (optional cormoran,runtime-combo-defaults node) --- */

struct zmk_runtime_combo_default {
    uint32_t slot;
    const char *name;
    struct zmk_runtime_combo_config config;
};

#define DT_DRV_COMPAT cormoran_runtime_combo_defaults

/* Usable in `#if`, unlike the sizeof-based RUNTIME_COMBO_DEFAULT_COUNT. */
#define RUNTIME_COMBO_HAS_DEFAULTS DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#if RUNTIME_COMBO_HAS_DEFAULTS

#define RUNTIME_COMBO_DEFAULT_PROP_BIT_AT_IDX(n, prop, idx) BIT(DT_PROP_BY_IDX(n, prop, idx))
#define RUNTIME_COMBO_DEFAULT_LAYER_MASK(n)                                                        \
    COND_CODE_1(DT_NODE_HAS_PROP(n, layers),                                                       \
                (DT_FOREACH_PROP_ELEM_SEP(n, layers, RUNTIME_COMBO_DEFAULT_PROP_BIT_AT_IDX, (|))), \
                (0))

#define RUNTIME_COMBO_DEFAULT_SLOW_RELEASE(n)                                                      \
    (DT_PROP(n, slow_release) ? ZMK_RUNTIME_COMBO_SLOW_RELEASE_ON                                  \
                              : ZMK_RUNTIME_COMBO_SLOW_RELEASE_INHERIT)

/* Check every raw cell before it enters uint16 storage or a layer bit shift. */
#define RUNTIME_COMBO_DEFAULT_VALIDATE_POSITION(n, prop, idx)                                      \
    BUILD_ASSERT(DT_PROP_BY_IDX(n, prop, idx) <= UINT16_MAX,                                       \
                 "runtime combo default key position must fit uint16");

#define RUNTIME_COMBO_DEFAULT_VALIDATE_LAYER(n, prop, idx)                                         \
    BUILD_ASSERT(DT_PROP_BY_IDX(n, prop, idx) <= 31,                                               \
                 "runtime combo default layer must be in 0..31");

#define RUNTIME_COMBO_DEFAULT_VALIDATE(n)                                                          \
    BUILD_ASSERT(DT_PROP_LEN(n, key_positions) >= 2,                                               \
                 "runtime combo default must have at least two key positions");                    \
    BUILD_ASSERT(DT_PROP_LEN(n, key_positions) <=                                                  \
                     CONFIG_ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO,                             \
                 "runtime combo default has too many key positions");                              \
    BUILD_ASSERT(DT_PROP_OR(n, slot, DT_NODE_CHILD_IDX(n)) < CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS,  \
                 "runtime combo default slot is out of range");                                    \
    BUILD_ASSERT(DT_PROP_OR(n, timeout_ms, 0) <= UINT16_MAX,                                       \
                 "runtime combo default timeout-ms must fit uint16");                              \
    BUILD_ASSERT(DT_PROP_OR(n, require_prior_idle_ms, 0) <= UINT16_MAX,                            \
                 "runtime combo default require-prior-idle-ms must fit uint16");                   \
    DT_FOREACH_PROP_ELEM(n, key_positions, RUNTIME_COMBO_DEFAULT_VALIDATE_POSITION)                \
    COND_CODE_1(DT_NODE_HAS_PROP(n, layers),                                                       \
                (DT_FOREACH_PROP_ELEM(n, layers, RUNTIME_COMBO_DEFAULT_VALIDATE_LAYER)), ())

DT_INST_FOREACH_CHILD(0, RUNTIME_COMBO_DEFAULT_VALIDATE)

#define RUNTIME_COMBO_DEFAULT_INST(n)                                                              \
    {                                                                                              \
        .slot = DT_PROP_OR(n, slot, DT_NODE_CHILD_IDX(n)),                                         \
        .name = DT_PROP_OR(n, display_name, DT_NODE_FULL_NAME(n)),                                 \
        .config =                                                                                  \
            {                                                                                      \
                .enabled = true,                                                                   \
                .key_position_len = DT_PROP_LEN(n, key_positions),                                 \
                .layer_mask = RUNTIME_COMBO_DEFAULT_LAYER_MASK(n),                                 \
                .behavior = ZMK_KEYMAP_EXTRACT_BINDING(0, n),                                      \
                .timeout_ms = DT_PROP_OR(n, timeout_ms, 0),                                        \
                .require_prior_idle_ms = DT_PROP_OR(n, require_prior_idle_ms, 0),                  \
                .slow_release_override = RUNTIME_COMBO_DEFAULT_SLOW_RELEASE(n),                    \
                .key_positions = DT_PROP(n, key_positions),                                        \
            },                                                                                     \
    },

static const struct zmk_runtime_combo_default runtime_combo_defaults[] = {
    DT_INST_FOREACH_CHILD(0, RUNTIME_COMBO_DEFAULT_INST)};

#define RUNTIME_COMBO_DEFAULT_COUNT ARRAY_SIZE(runtime_combo_defaults)

#else

static const struct zmk_runtime_combo_default runtime_combo_defaults[1];

#define RUNTIME_COMBO_DEFAULT_COUNT 0

#endif

static const struct zmk_runtime_combo_default *runtime_combo_find_default(uint32_t index) {
    for (size_t i = 0; i < RUNTIME_COMBO_DEFAULT_COUNT; i++) {
        if (runtime_combo_defaults[i].slot == index) {
            return &runtime_combo_defaults[i];
        }
    }
    return NULL;
}

static uint32_t runtime_combo_default_upper_bound(void) {
    uint32_t upper_bound = 0;
    for (size_t i = 0; i < RUNTIME_COMBO_DEFAULT_COUNT; i++) {
        upper_bound = MAX(upper_bound, runtime_combo_defaults[i].slot + 1);
    }
    return upper_bound;
}

uint32_t zmk_runtime_combo_default_count(void) { return RUNTIME_COMBO_DEFAULT_COUNT; }

bool zmk_runtime_combo_has_default(uint32_t index) {
    return runtime_combo_find_default(index) != NULL;
}

int zmk_runtime_combo_read_default(uint32_t index, struct zmk_runtime_combo_config *combo) {
    if (!combo) {
        return -EINVAL;
    }
    const struct zmk_runtime_combo_default *def = runtime_combo_find_default(index);
    if (!def) {
        return -ENOENT;
    }
    *combo = def->config;
    return 0;
}

/* --- Decoded-combo cache for the key-event path --- */

/* An effective combo (stored value, or its compile-time default), decoded so
 * that key events never touch the settings registry. Positions are range
 * checked on write, so uint16_t holds them. */
struct runtime_combo {
    bool valid;
    bool enabled;
    uint8_t key_position_len;
    uint32_t layer_mask;
    struct zmk_behavior_binding behavior;
    uint16_t timeout_ms;
    uint16_t require_prior_idle_ms;
    enum zmk_runtime_combo_slow_release_override slow_release_override;
    uint16_t key_positions[CONFIG_ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO];
};

/* Read and rebuilt only on the system work queue, which runs every position
 * event and the timeout work. A Studio RPC change runs on its own thread; it
 * writes the settings, then marks its slot (or the global settings) stale, and
 * the next event rebuilds what is stale before matching. An entry is never
 * copied while it is matched, and a rebuild that overlaps a write is redone on
 * the following event, because the write marks again when it ends. Every
 * slot starts stale and is marked again once custom settings finish the boot
 * settings_load(): main() runs it after every SYS_INIT hook, but the work
 * queue can preempt main() and handle a key press while it loads. */
static struct runtime_combo runtime_combo_cache[CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS];
static struct zmk_runtime_combo_global_settings runtime_combo_global_cache;
static ATOMIC_DEFINE(runtime_combo_stale_slots, CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS);
static atomic_t runtime_combo_global_stale;

static void mark_slot_stale(uint32_t index) { atomic_set_bit(runtime_combo_stale_slots, index); }

static void mark_global_stale(void) { atomic_set(&runtime_combo_global_stale, 1); }

void zmk_runtime_combo_invalidate_cache(void) {
    for (uint32_t i = 0; i < CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS; i++) {
        mark_slot_stale(i);
    }
    mark_global_stale();
}

/* --- Settings access --- */

static const struct zmk_custom_setting *combo_setting(uint32_t index) {
    return zmk_custom_setting_find_array_element(ZMK_RUNTIME_COMBO_SUBSYSTEM_ID,
                                                 ZMK_RUNTIME_COMBO_COMBOS_KEY, index);
}

static const struct zmk_custom_setting *name_setting(uint32_t index) {
    return zmk_custom_setting_find_array_element(ZMK_RUNTIME_COMBO_SUBSYSTEM_ID,
                                                 ZMK_RUNTIME_COMBO_NAMES_KEY, index);
}

static const struct zmk_custom_setting *behavior_setting(uint32_t index) {
    return zmk_custom_setting_find_array_element(ZMK_RUNTIME_COMBO_SUBSYSTEM_ID,
                                                 ZMK_RUNTIME_COMBO_BEHAVIORS_KEY, index);
}

static enum zmk_custom_setting_write_mode write_mode(bool persist) {
    return persist ? ZMK_CUSTOM_SETTING_WRITE_MODE_PERSIST : ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY;
}

/* Writes element `index` of an array setting, growing the array to cover it. */
static int write_element(const struct zmk_custom_setting *setting, uint32_t index,
                         const struct zmk_custom_setting_value *value,
                         enum zmk_custom_setting_write_mode mode) {
    if (!setting) {
        return -ENOENT;
    }
    uint32_t array_size = MAX(zmk_custom_setting_array_size(setting), index + 1);
    return zmk_custom_setting_write_array_element(setting, value, array_size, mode);
}

/* Read a combo's behavior binding and resolve its local ID back to a name. */
static int read_combo_behavior(uint32_t index, struct zmk_behavior_binding *behavior) {
    struct zmk_custom_setting_value value;
    int ret = zmk_custom_setting_read_array_by_key(ZMK_RUNTIME_COMBO_SUBSYSTEM_ID,
                                                   ZMK_RUNTIME_COMBO_BEHAVIORS_KEY, index, &value);
    if (ret < 0) {
        return ret;
    }
    if (value.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_BEHAVIOR) {
        return -EINVAL;
    }
    zmk_behavior_local_id_t behavior_id = value.behavior_value.behavior_id;
    const char *behavior_name = zmk_behavior_find_behavior_name_from_local_id(behavior_id);
    if (!behavior_name) {
        return -ENODEV;
    }
    *behavior = (struct zmk_behavior_binding){
        .behavior_dev = behavior_name,
        .param1 = value.behavior_value.param1,
        .param2 = value.behavior_value.param2,
    };
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_LOCAL_IDS_IN_BINDINGS)
    behavior->local_id = behavior_id;
#endif
    return 0;
}

/* The custom-settings write path validates the binding against the target
 * behavior's parameter metadata. */
static int write_combo_behavior(uint32_t index, const struct zmk_behavior_binding *behavior,
                                bool persist) {
    zmk_behavior_local_id_t behavior_id = zmk_behavior_get_local_id(behavior->behavior_dev);
    if (behavior_id == 0 || behavior_id == UINT16_MAX) {
        return -ENODEV;
    }
    struct zmk_custom_setting_value value =
        ZMK_CUSTOM_SETTING_VALUE_BEHAVIOR(behavior_id, behavior->param1, behavior->param2);
    return write_element(behavior_setting(index), index, &value, write_mode(persist));
}

uint32_t zmk_runtime_combo_count(void) {
    const struct zmk_custom_setting *setting =
        zmk_custom_setting_find_array(ZMK_RUNTIME_COMBO_SUBSYSTEM_ID, ZMK_RUNTIME_COMBO_COMBOS_KEY);
    uint32_t array_size = setting ? zmk_custom_setting_array_size(setting) : 0;
    /* A slot covered only by a compile-time default may never have grown the
     * array, but must still be matched and listed. */
    return MAX(array_size, runtime_combo_default_upper_bound());
}

uint32_t zmk_runtime_combo_max_count(void) { return CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS; }

static int packed_to_combo(const struct zmk_custom_setting_value *value,
                           struct zmk_runtime_combo_config *combo) {
    if (value->type != ZMK_CUSTOM_SETTING_VALUE_TYPE_BYTES) {
        return -EINVAL;
    }
    if (value->size == 0) {
        *combo = (struct zmk_runtime_combo_config){0};
        return 0;
    }

    const uint8_t *bytes = value->bytes_value;
    if (bytes[0] != RUNTIME_COMBO_STORAGE_VERSION ||
        value->size < RUNTIME_COMBO_PACKED_HEADER_SIZE) {
        return -EINVAL;
    }

    uint8_t key_position_len = bytes[2];
    if (key_position_len < 2 ||
        key_position_len > CONFIG_ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO ||
        value->size != RUNTIME_COMBO_PACKED_HEADER_SIZE + key_position_len * sizeof(uint16_t)) {
        return -EINVAL;
    }

    uint8_t flags = bytes[1];
    enum zmk_runtime_combo_slow_release_override slow_release_override =
        ZMK_RUNTIME_COMBO_SLOW_RELEASE_INHERIT;
    if (flags & RUNTIME_COMBO_FLAG_SLOW_RELEASE_OVERRIDE) {
        slow_release_override = (flags & RUNTIME_COMBO_FLAG_SLOW_RELEASE_VALUE)
                                    ? ZMK_RUNTIME_COMBO_SLOW_RELEASE_ON
                                    : ZMK_RUNTIME_COMBO_SLOW_RELEASE_OFF;
    }

    /* The caller fills in the behavior binding (read_combo_behavior). */
    *combo = (struct zmk_runtime_combo_config){
        .enabled = (flags & RUNTIME_COMBO_FLAG_ENABLED) != 0,
        .key_position_len = key_position_len,
        .layer_mask = sys_get_le32(&bytes[4]),
        .timeout_ms = sys_get_le16(&bytes[8]),
        .require_prior_idle_ms = sys_get_le16(&bytes[10]),
        .slow_release_override = slow_release_override,
    };
    for (uint8_t i = 0; i < key_position_len; i++) {
        combo->key_positions[i] =
            sys_get_le16(&bytes[RUNTIME_COMBO_PACKED_HEADER_SIZE + i * sizeof(uint16_t)]);
    }
    return 0;
}

static int combo_to_packed(const struct zmk_runtime_combo_config *combo,
                           struct zmk_custom_setting_value *value) {
    if (combo->key_position_len < 2 ||
        combo->key_position_len > CONFIG_ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO) {
        return -EINVAL;
    }
    for (uint8_t i = 0; i < combo->key_position_len; i++) {
        if (combo->key_positions[i] < 0 || combo->key_positions[i] > UINT16_MAX) {
            return -ERANGE;
        }
    }

    *value = (struct zmk_custom_setting_value){
        .type = ZMK_CUSTOM_SETTING_VALUE_TYPE_BYTES,
        .size = RUNTIME_COMBO_PACKED_HEADER_SIZE + combo->key_position_len * sizeof(uint16_t),
    };
    uint8_t flags = combo->enabled ? RUNTIME_COMBO_FLAG_ENABLED : 0;
    if (combo->slow_release_override != ZMK_RUNTIME_COMBO_SLOW_RELEASE_INHERIT) {
        flags |= RUNTIME_COMBO_FLAG_SLOW_RELEASE_OVERRIDE;
        if (combo->slow_release_override == ZMK_RUNTIME_COMBO_SLOW_RELEASE_ON) {
            flags |= RUNTIME_COMBO_FLAG_SLOW_RELEASE_VALUE;
        }
    }
    uint8_t *bytes = value->bytes_value;
    bytes[0] = RUNTIME_COMBO_STORAGE_VERSION;
    bytes[1] = flags;
    bytes[2] = combo->key_position_len;
    bytes[3] = 0;
    sys_put_le32(combo->layer_mask, &bytes[4]);
    sys_put_le16(combo->timeout_ms, &bytes[8]);
    sys_put_le16(combo->require_prior_idle_ms, &bytes[10]);
    for (uint8_t i = 0; i < combo->key_position_len; i++) {
        sys_put_le16(combo->key_positions[i],
                     &bytes[RUNTIME_COMBO_PACKED_HEADER_SIZE + i * sizeof(uint16_t)]);
    }
    return 0;
}

int zmk_runtime_combo_read(uint32_t index, struct zmk_runtime_combo_config *combo) {
    if (!combo || index >= CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS) {
        return -EINVAL;
    }

    struct zmk_custom_setting_value value;
    int ret = zmk_custom_setting_read_array_by_key(ZMK_RUNTIME_COMBO_SUBSYSTEM_ID,
                                                   ZMK_RUNTIME_COMBO_COMBOS_KEY, index, &value);
    /* An element the array has not grown to (-ENOENT) and an explicitly empty
     * record both fall back to the compile-time default. */
    bool unset = ret == -ENOENT ||
                 (ret == 0 && value.type == ZMK_CUSTOM_SETTING_VALUE_TYPE_BYTES && value.size == 0);
    const struct zmk_runtime_combo_default *def = unset ? runtime_combo_find_default(index) : NULL;
    if (def) {
        *combo = def->config;
        return 0;
    }
    if (ret < 0) {
        return ret;
    }

    ret = packed_to_combo(&value, combo);
    if (ret < 0 || combo->key_position_len == 0) {
        /* An empty record with no default has no behavior binding to read. */
        return ret;
    }
    return read_combo_behavior(index, &combo->behavior);
}

int zmk_runtime_combo_read_name(uint32_t index, char *name, size_t name_size) {
    if (!name || name_size == 0 || index >= CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS) {
        return -EINVAL;
    }

    struct zmk_custom_setting_value value;
    int ret = zmk_custom_setting_read_array_by_key(ZMK_RUNTIME_COMBO_SUBSYSTEM_ID,
                                                   ZMK_RUNTIME_COMBO_NAMES_KEY, index, &value);
    if (ret == 0 && value.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_STRING) {
        return -EINVAL;
    }
    /* A missing (-ENOENT) or empty name falls back to the compile-time default's. */
    const char *source = ret == 0 ? value.string_value : "";
    if (source[0] == '\0' && (ret == 0 || ret == -ENOENT)) {
        const struct zmk_runtime_combo_default *def = runtime_combo_find_default(index);
        if (def) {
            source = def->name;
            ret = 0;
        }
    }
    strncpy(name, source, name_size - 1);
    name[name_size - 1] = '\0';
    return ret;
}

int zmk_runtime_combo_read_global_settings(struct zmk_runtime_combo_global_settings *settings) {
    if (!settings) {
        return -EINVAL;
    }

    struct zmk_custom_setting_value timeout_value;
    int ret = zmk_custom_setting_read_by_key(ZMK_RUNTIME_COMBO_SUBSYSTEM_ID,
                                             ZMK_RUNTIME_COMBO_TIMEOUT_MS_KEY, &timeout_value);
    if (ret < 0) {
        return ret;
    }
    if (timeout_value.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32 ||
        timeout_value.int32_value < 1 || timeout_value.int32_value > UINT16_MAX) {
        return -EINVAL;
    }

    struct zmk_custom_setting_value slow_release_value;
    ret = zmk_custom_setting_read_by_key(ZMK_RUNTIME_COMBO_SUBSYSTEM_ID,
                                         ZMK_RUNTIME_COMBO_SLOW_RELEASE_KEY, &slow_release_value);
    if (ret < 0) {
        return ret;
    }
    if (slow_release_value.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL) {
        return -EINVAL;
    }

    struct zmk_custom_setting_value require_prior_idle_value;
    ret = zmk_custom_setting_read_by_key(ZMK_RUNTIME_COMBO_SUBSYSTEM_ID,
                                         ZMK_RUNTIME_COMBO_REQUIRE_PRIOR_IDLE_MS_KEY,
                                         &require_prior_idle_value);
    if (ret < 0) {
        return ret;
    }
    if (require_prior_idle_value.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32 ||
        require_prior_idle_value.int32_value < 0 ||
        require_prior_idle_value.int32_value > UINT16_MAX) {
        return -EINVAL;
    }

    *settings = (struct zmk_runtime_combo_global_settings){
        .timeout_ms = timeout_value.int32_value,
        .slow_release = slow_release_value.bool_value,
        .max_combo = zmk_runtime_combo_max_count(),
        .require_prior_idle_ms = require_prior_idle_value.int32_value,
    };
    return 0;
}

static int ensure_name_array_size(uint32_t index, bool persist) {
    const struct zmk_custom_setting *setting = name_setting(index);
    if (!setting) {
        return -ENOENT;
    }
    if (zmk_custom_setting_array_size(setting) > index) {
        return 0;
    }
    return write_element(setting, index, &ZMK_CUSTOM_SETTING_VALUE_STRING(""), write_mode(persist));
}

/* Every write below changes RAM before it persists, so a slot is marked stale
 * whatever the write returns; custom settings raise no change event for a
 * write that fails. */

int zmk_runtime_combo_write(uint32_t index, const struct zmk_runtime_combo_config *combo,
                            bool persist) {
    if (!combo || index >= CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS) {
        return -EINVAL;
    }

    /* Validate the binding before any array element is written or grown. */
    zmk_behavior_local_id_t behavior_id = zmk_behavior_get_local_id(combo->behavior.behavior_dev);
    if (behavior_id == 0 || behavior_id == UINT16_MAX) {
        return -ENODEV;
    }
    int ret = zmk_behavior_validate_binding(&combo->behavior);
    if (ret < 0) {
        return ret;
    }

    struct zmk_custom_setting_value value;
    ret = combo_to_packed(combo, &value);
    if (ret < 0) {
        return ret;
    }
    const struct zmk_custom_setting *setting = combo_setting(index);
    if (!setting) {
        return -ENOENT;
    }

    int write_ret = write_element(setting, index, &value, write_mode(persist));
    int behavior_ret = write_combo_behavior(index, &combo->behavior, persist);
    int name_ret = ensure_name_array_size(index, persist);
    mark_slot_stale(index);
    if (write_ret < 0) {
        return write_ret;
    }
    if (behavior_ret < 0) {
        return behavior_ret;
    }
    return name_ret;
}

int zmk_runtime_combo_write_name(uint32_t index, const char *name, bool persist) {
    if (!name || index >= CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS) {
        return -EINVAL;
    }

    struct zmk_custom_setting_value value = {
        .type = ZMK_CUSTOM_SETTING_VALUE_TYPE_STRING,
    };
    strncpy(value.string_value, name, CONFIG_ZMK_CUSTOM_SETTINGS_VALUE_MAX_SIZE);
    value.string_value[CONFIG_ZMK_CUSTOM_SETTINGS_VALUE_MAX_SIZE] = '\0';
    value.size = strlen(value.string_value);
    return write_element(name_setting(index), index, &value, write_mode(persist));
}

static int write_global(const char *key, const struct zmk_custom_setting_value *value,
                        bool persist) {
    int ret = zmk_custom_setting_write_by_key(ZMK_RUNTIME_COMBO_SUBSYSTEM_ID, key, value,
                                              write_mode(persist));
    mark_global_stale();
    return ret;
}

int zmk_runtime_combo_write_timeout_ms(uint16_t timeout_ms, bool persist) {
    if (timeout_ms == 0) {
        return -EINVAL;
    }
    return write_global(ZMK_RUNTIME_COMBO_TIMEOUT_MS_KEY,
                        &ZMK_CUSTOM_SETTING_VALUE_INT32(timeout_ms), persist);
}

int zmk_runtime_combo_write_slow_release(bool slow_release, bool persist) {
    return write_global(ZMK_RUNTIME_COMBO_SLOW_RELEASE_KEY,
                        &ZMK_CUSTOM_SETTING_VALUE_BOOL(slow_release), persist);
}

int zmk_runtime_combo_write_require_prior_idle_ms(uint16_t require_prior_idle_ms, bool persist) {
    return write_global(ZMK_RUNTIME_COMBO_REQUIRE_PRIOR_IDLE_MS_KEY,
                        &ZMK_CUSTOM_SETTING_VALUE_INT32(require_prior_idle_ms), persist);
}

int zmk_runtime_combo_delete(uint32_t index, bool persist) {
    struct zmk_runtime_combo_config combo;
    int ret = zmk_runtime_combo_read(index, &combo);
    if (ret < 0) {
        return ret;
    }
    if (combo.key_position_len == 0) {
        return -ENOENT; /* nothing to disable in an empty slot */
    }
    combo.enabled = false;
    return zmk_runtime_combo_write(index, &combo, persist);
}

bool zmk_runtime_combo_has_override(uint32_t index) {
    if (index >= CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS) {
        return false;
    }
    struct zmk_custom_setting_value value;
    int ret = zmk_custom_setting_read_array_by_key(ZMK_RUNTIME_COMBO_SUBSYSTEM_ID,
                                                   ZMK_RUNTIME_COMBO_COMBOS_KEY, index, &value);
    return ret == 0 && value.size > 0;
}

int zmk_runtime_combo_reset(uint32_t index) {
    if (index >= CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS) {
        return -EINVAL;
    }

    /* zmk_custom_setting_reset() would restore the whole array's default size
     * and hide every other combo. An explicit empty record at just this index
     * reads back like an unset slot. */
    const struct zmk_custom_setting *setting = combo_setting(index);
    if (!setting) {
        return -ENOENT;
    }
    int ret = write_element(setting, index, &RUNTIME_COMBO_EMPTY_BYTES,
                            ZMK_CUSTOM_SETTING_WRITE_MODE_PERSIST);
    const struct zmk_custom_setting *name = name_setting(index);
    if (name) {
        write_element(name, index, &ZMK_CUSTOM_SETTING_VALUE_STRING(""),
                      ZMK_CUSTOM_SETTING_WRITE_MODE_PERSIST);
    }
    mark_slot_stale(index);
    return ret;
}

static void runtime_combo_cache_rebuild_slot(uint32_t index) {
    struct runtime_combo *entry = &runtime_combo_cache[index];
    struct zmk_runtime_combo_config combo;
    if (zmk_runtime_combo_read(index, &combo) < 0) {
        *entry = (struct runtime_combo){.valid = false};
        return;
    }

    *entry = (struct runtime_combo){
        .valid = true,
        .enabled = combo.enabled,
        .key_position_len = combo.key_position_len,
        .layer_mask = combo.layer_mask,
        .behavior = combo.behavior,
        .timeout_ms = combo.timeout_ms,
        .require_prior_idle_ms = combo.require_prior_idle_ms,
        .slow_release_override = combo.slow_release_override,
    };
    for (uint8_t i = 0; i < combo.key_position_len; i++) {
        entry->key_positions[i] = (uint16_t)combo.key_positions[i];
    }
}

static struct zmk_runtime_combo_global_settings current_global_settings(void) {
    struct zmk_runtime_combo_global_settings settings = {
        .timeout_ms = CONFIG_ZMK_RUNTIME_COMBO_DEFAULT_TIMEOUT_MS,
        .slow_release = false,
        .require_prior_idle_ms = CONFIG_ZMK_RUNTIME_COMBO_DEFAULT_REQUIRE_PRIOR_IDLE_MS,
    };
    if (zmk_runtime_combo_read_global_settings(&settings) < 0) {
        LOG_INF("runtime combo global defaults applied");
    }
    return settings;
}

/* --- Matching (system work queue only; see runtime_combo_cache) --- */

struct runtime_combo_active {
    uint16_t combo_idx;
    /* The release belongs to this press, even if its slot is edited/reset. */
    struct zmk_behavior_binding binding;
    uint8_t key_positions_count;
    bool slow_release;
    uint8_t key_positions_pressed_count;
    struct zmk_position_state_changed_event
        key_positions_pressed[CONFIG_ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO];
};

static uint8_t pending_count;
static struct zmk_position_state_changed_event
    pending_keys[CONFIG_ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO];
static struct runtime_combo_active active_combos[CONFIG_ZMK_COMBO_MAX_PRESSED_COMBOS];
static uint8_t active_combo_count;
static struct k_work_delayable timeout_task;
static int64_t timeout_task_timeout_at;
static int64_t last_tapped_timestamp = INT32_MIN;
static int64_t last_combo_timestamp = INT32_MIN;
/* Index of a combo whose positions are all pressed but which is being held back
 * because a strict superset combo is still viable. -1 means none. */
static int32_t fully_pressed_combo_idx = -1;

static void finish_pending_and_activate(uint32_t combo_idx, const struct runtime_combo *combo);

/* Rebuilds what was marked stale. The flag is cleared before the rebuild, so
 * a change that lands meanwhile is picked up by the next event. */
static void runtime_combo_cache_refresh(void) {
    for (uint32_t i = 0; i < CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS; i++) {
        if (atomic_test_and_clear_bit(runtime_combo_stale_slots, i)) {
            runtime_combo_cache_rebuild_slot(i);
            /* A combo held back for a longer candidate is not fired from a
             * slot that changed meanwhile; its keys replay as plain presses. */
            if (fully_pressed_combo_idx == (int32_t)i) {
                fully_pressed_combo_idx = -1;
            }
        }
    }
    if (atomic_cas(&runtime_combo_global_stale, 1, 0)) {
        runtime_combo_global_cache = current_global_settings();
    }
}

/* The cached combo of slot `index` if it is enabled, else NULL. */
static const struct runtime_combo *enabled_combo(uint32_t index) {
    const struct runtime_combo *combo = &runtime_combo_cache[index];
    return combo->valid && combo->enabled ? combo : NULL;
}

static bool combo_contains_position(const struct runtime_combo *combo, uint32_t position) {
    for (uint8_t i = 0; i < combo->key_position_len; i++) {
        if (combo->key_positions[i] == position) {
            return true;
        }
    }
    return false;
}

static bool combo_active_on_layer(const struct runtime_combo *combo, uint8_t layer) {
    return combo->layer_mask == 0 || (combo->layer_mask & BIT(layer)) != 0;
}

static uint16_t effective_timeout_ms(const struct runtime_combo *combo) {
    return combo->timeout_ms != 0 ? combo->timeout_ms : runtime_combo_global_cache.timeout_ms;
}

static uint16_t effective_require_prior_idle_ms(const struct runtime_combo *combo) {
    return combo->require_prior_idle_ms != 0 ? combo->require_prior_idle_ms
                                             : runtime_combo_global_cache.require_prior_idle_ms;
}

static bool effective_slow_release(const struct runtime_combo *combo) {
    switch (combo->slow_release_override) {
    case ZMK_RUNTIME_COMBO_SLOW_RELEASE_ON:
        return true;
    case ZMK_RUNTIME_COMBO_SLOW_RELEASE_OFF:
        return false;
    default:
        return runtime_combo_global_cache.slow_release;
    }
}

static bool combo_matches_pending(const struct runtime_combo *combo, int64_t timestamp) {
    if (pending_count == 0) {
        return false;
    }
    if (pending_keys[0].data.timestamp + effective_timeout_ms(combo) <= timestamp) {
        return false;
    }
    for (uint8_t i = 0; i < pending_count; i++) {
        if (!combo_contains_position(combo, pending_keys[i].data.position)) {
            return false;
        }
    }
    return true;
}

static bool is_quick_tap(const struct runtime_combo *combo, int64_t timestamp) {
    uint16_t require_prior_idle_ms = effective_require_prior_idle_ms(combo);
    if (require_prior_idle_ms == 0 || last_tapped_timestamp == INT32_MIN) {
        return false;
    }
    return last_tapped_timestamp > last_combo_timestamp &&
           last_tapped_timestamp + require_prior_idle_ms > timestamp;
}

static int count_candidates_for_position(uint32_t position, int64_t timestamp) {
    int count = 0;
    uint8_t highest_active_layer = zmk_keymap_highest_layer_active();

    for (uint32_t i = 0; i < CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS; i++) {
        const struct runtime_combo *combo = enabled_combo(i);
        if (!combo || !combo_contains_position(combo, position) ||
            !combo_active_on_layer(combo, highest_active_layer) || is_quick_tap(combo, timestamp)) {
            continue;
        }
        if (pending_count == 0 || combo_matches_pending(combo, timestamp)) {
            count++;
        }
    }
    return count;
}

/* True if some combo strictly longer than `completed_len` still matches everything
 * currently pending, meaning a shorter fully-pressed combo should not fire yet. */
static bool has_longer_viable_candidate(uint8_t completed_len, int64_t timestamp) {
    uint8_t highest_active_layer = zmk_keymap_highest_layer_active();

    for (uint32_t i = 0; i < CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS; i++) {
        const struct runtime_combo *combo = enabled_combo(i);
        if (!combo || combo->key_position_len <= completed_len ||
            !combo_active_on_layer(combo, highest_active_layer) || is_quick_tap(combo, timestamp)) {
            continue;
        }
        if (combo_matches_pending(combo, timestamp)) {
            return true;
        }
    }
    return false;
}

static int release_pending_keys(void) {
    uint8_t count = pending_count;
    pending_count = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (i == 0) {
            ZMK_EVENT_RELEASE(pending_keys[i]);
        } else {
            ZMK_EVENT_RAISE(pending_keys[i]);
        }
    }
    return count;
}

static void cleanup_pending(void) {
    k_work_cancel_delayable(&timeout_task);
    timeout_task_timeout_at = 0;
    fully_pressed_combo_idx = -1;
    release_pending_keys();
}

/* Fire the combo remembered as fully pressed (if any), replaying any pending keys
 * beyond its own positions as regular key events. Returns false if none was stored. */
static bool fire_fully_pressed_combo(void) {
    if (fully_pressed_combo_idx < 0) {
        return false;
    }
    uint32_t combo_idx = fully_pressed_combo_idx;
    fully_pressed_combo_idx = -1;

    const struct runtime_combo *combo = &runtime_combo_cache[combo_idx];
    if (!combo->valid || combo->key_position_len > pending_count) {
        return false;
    }
    finish_pending_and_activate(combo_idx, combo);
    return true;
}

static void update_timeout_task(void) {
    if (pending_count == 0) {
        k_work_cancel_delayable(&timeout_task);
        timeout_task_timeout_at = 0;
        return;
    }

    int64_t timeout_at = LLONG_MAX;
    for (uint32_t i = 0; i < CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS; i++) {
        const struct runtime_combo *combo = enabled_combo(i);
        if (!combo || !combo_matches_pending(combo, k_uptime_get())) {
            continue;
        }
        timeout_at = MIN(timeout_at, pending_keys[0].data.timestamp + effective_timeout_ms(combo));
    }

    if (timeout_at == LLONG_MAX) {
        if (!fire_fully_pressed_combo()) {
            cleanup_pending();
        }
        return;
    }
    if (timeout_task_timeout_at != timeout_at &&
        k_work_schedule(&timeout_task, K_TICKS(runtime_combo_timeout_ticks(
                                           (uint32_t)MAX(timeout_at - k_uptime_get(), 0),
                                           CONFIG_SYS_CLOCK_TICKS_PER_SEC))) >= 0) {
        timeout_task_timeout_at = timeout_at;
    }
}

static struct zmk_behavior_binding_event combo_event(uint32_t combo_idx, int64_t timestamp) {
    return (struct zmk_behavior_binding_event){
        .position = ZMK_VIRTUAL_KEY_POSITION_COMBO(ZMK_COMBOS_LEN + combo_idx),
        .timestamp = timestamp,
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };
}

static int press_combo_behavior(uint32_t combo_idx, const struct runtime_combo *combo,
                                int64_t timestamp) {
    last_combo_timestamp = timestamp;
    return zmk_behavior_invoke_binding(&combo->behavior, combo_event(combo_idx, timestamp), true);
}

static int release_combo_behavior(const struct runtime_combo_active *active, int64_t timestamp) {
    return zmk_behavior_invoke_binding(&active->binding, combo_event(active->combo_idx, timestamp),
                                       false);
}

static struct runtime_combo_active *store_active_combo(uint32_t combo_idx, uint8_t count,
                                                       const struct runtime_combo *combo) {
    if (active_combo_count >= CONFIG_ZMK_COMBO_MAX_PRESSED_COMBOS) {
        LOG_WRN("Unable to store runtime combo; active combo limit reached");
        return NULL;
    }

    struct runtime_combo_active *active = &active_combos[active_combo_count++];
    *active = (struct runtime_combo_active){
        .combo_idx = combo_idx,
        .binding = combo->behavior,
        .key_positions_count = count,
        .slow_release = effective_slow_release(combo),
    };
    for (uint8_t i = 0; i < count; i++) {
        active->key_positions_pressed[i] = pending_keys[i];
    }
    active->key_positions_pressed_count = count;
    return active;
}

static int activate_combo(uint32_t combo_idx, const struct runtime_combo *combo) {
    struct runtime_combo_active *active = store_active_combo(combo_idx, pending_count, combo);
    if (!active) {
        cleanup_pending();
        return ZMK_EV_EVENT_CAPTURED;
    }
    pending_count = 0;
    fully_pressed_combo_idx = -1;
    k_work_cancel_delayable(&timeout_task);
    timeout_task_timeout_at = 0;
    press_combo_behavior(combo_idx, combo, active->key_positions_pressed[0].data.timestamp);
    return ZMK_EV_EVENT_CAPTURED;
}

/* Fire `combo` (whose positions are the first `combo->key_position_len` pending keys)
 * and replay any remaining pending keys as regular key events, since they were
 * captured while a longer candidate looked viable but ended up unused. */
static void finish_pending_and_activate(uint32_t combo_idx, const struct runtime_combo *combo) {
    uint8_t combo_len = combo->key_position_len;
    uint8_t leftover_count = pending_count - combo_len;
    struct zmk_position_state_changed_event
        leftover[CONFIG_ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO];
    for (uint8_t i = 0; i < leftover_count; i++) {
        leftover[i] = pending_keys[combo_len + i];
    }

    struct runtime_combo_active *active = store_active_combo(combo_idx, combo_len, combo);
    pending_count = 0;
    k_work_cancel_delayable(&timeout_task);
    timeout_task_timeout_at = 0;

    if (active) {
        press_combo_behavior(combo_idx, combo, active->key_positions_pressed[0].data.timestamp);
    }

    /* Replayed events re-enter the listener; `combo` is not used past here. */
    for (uint8_t i = 0; i < leftover_count; i++) {
        if (i == 0) {
            ZMK_EVENT_RELEASE(leftover[i]);
        } else {
            ZMK_EVENT_RAISE(leftover[i]);
        }
    }
}

static int find_complete_combo(void) {
    for (uint32_t i = 0; i < CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS; i++) {
        const struct runtime_combo *combo = enabled_combo(i);
        if (combo && combo->key_position_len == pending_count &&
            combo_matches_pending(combo, pending_keys[pending_count - 1].data.timestamp)) {
            return i;
        }
    }
    return -ENOENT;
}

static int position_state_down(struct zmk_position_state_changed *data) {
    int candidates = count_candidates_for_position(data->position, data->timestamp);
    if (candidates == 0) {
        if (fire_fully_pressed_combo()) {
            return position_state_down(data);
        }
        if (pending_count > 0) {
            cleanup_pending();
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (pending_count >= CONFIG_ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO) {
        cleanup_pending();
        return ZMK_EV_EVENT_BUBBLE;
    }

    pending_keys[pending_count++] = copy_raised_zmk_position_state_changed(data);

    int complete_combo = find_complete_combo();
    if (complete_combo >= 0) {
        const struct runtime_combo *combo = &runtime_combo_cache[complete_combo];
        if (has_longer_viable_candidate(combo->key_position_len, data->timestamp)) {
            fully_pressed_combo_idx = complete_combo;
            update_timeout_task();
            return ZMK_EV_EVENT_CAPTURED;
        }
        return activate_combo(complete_combo, combo);
    }

    update_timeout_task();
    return ZMK_EV_EVENT_CAPTURED;
}

static void deactivate_combo(uint8_t active_combo_index) {
    active_combo_count--;
    if (active_combo_index != active_combo_count) {
        active_combos[active_combo_index] = active_combos[active_combo_count];
    }
    active_combos[active_combo_count] = (struct runtime_combo_active){0};
}

static bool release_combo_key(uint32_t position, int64_t timestamp) {
    for (uint8_t active_idx = 0; active_idx < active_combo_count; active_idx++) {
        struct runtime_combo_active *active = &active_combos[active_idx];
        bool key_released = false;
        bool all_keys_pressed = active->key_positions_pressed_count == active->key_positions_count;
        bool all_keys_released = true;
        for (uint8_t i = 0; i < active->key_positions_pressed_count; i++) {
            if (key_released) {
                active->key_positions_pressed[i - 1] = active->key_positions_pressed[i];
                all_keys_released = false;
            } else if (active->key_positions_pressed[i].data.position != position) {
                all_keys_released = false;
            } else {
                key_released = true;
            }
        }

        if (!key_released) {
            continue;
        }

        active->key_positions_pressed_count--;
        if ((active->slow_release && all_keys_released) ||
            (!active->slow_release && all_keys_pressed)) {
            release_combo_behavior(active, timestamp);
        }
        if (all_keys_released) {
            deactivate_combo(active_idx);
        }
        return true;
    }
    return false;
}

static int position_state_up(struct zmk_position_state_changed *data) {
    if (pending_count > 0 && !fire_fully_pressed_combo()) {
        cleanup_pending();
    }
    if (release_combo_key(data->position, data->timestamp)) {
        return ZMK_EV_EVENT_HANDLED;
    }
    return ZMK_EV_EVENT_BUBBLE;
}

static void combo_timeout_handler(struct k_work *item) {
    ARG_UNUSED(item);
    runtime_combo_cache_refresh();
    if (timeout_task_timeout_at != 0 && k_uptime_get() >= timeout_task_timeout_at) {
        if (!fire_fully_pressed_combo()) {
            cleanup_pending();
        }
    }
}

#if IS_ENABLED(CONFIG_ZMK_RUNTIME_COMBO_TEST)
#include "../../tests/support/runtime_combo_assertions_0.inc"
#endif

static int position_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_position_state_changed *data = as_zmk_position_state_changed(eh);
    if (!data) {
        return ZMK_EV_EVENT_BUBBLE;
    }
#if IS_ENABLED(CONFIG_ZMK_RUNTIME_COMBO_TEST)
    runtime_combo_test_setup();
#endif
    runtime_combo_cache_refresh();
    return data->state ? position_state_down(data) : position_state_up(data);
}

static void store_last_tapped(int64_t timestamp) {
    if (timestamp > last_combo_timestamp) {
        last_tapped_timestamp = timestamp;
    }
}

static int keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev && ev->state && !is_mod(ev->usage_page, ev->keycode)) {
        store_last_tapped(ev->timestamp);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

static int runtime_combo_listener(const zmk_event_t *eh) {
    if (as_zmk_position_state_changed(eh) != NULL) {
        return position_state_changed_listener(eh);
    }
    if (as_zmk_keycode_state_changed(eh) != NULL) {
        return keycode_state_changed_listener(eh);
    }
    const struct zmk_custom_setting_changed *changed = as_zmk_custom_setting_changed(eh);
    if (as_zmk_custom_settings_initialized(eh) != NULL ||
        (changed && zmk_custom_setting_matches(changed->setting, ZMK_RUNTIME_COMBO_SUBSYSTEM_ID,
                                               NULL, NULL))) {
        /* The end of the boot load drops whatever a key press cached while it
         * ran; a change event also covers writers that bypass this module's
         * API (the generic custom settings RPC, scoped save/discard). */
        zmk_runtime_combo_invalidate_cache();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(runtime_combo, runtime_combo_listener);
ZMK_SUBSCRIPTION(runtime_combo, zmk_position_state_changed);
ZMK_SUBSCRIPTION(runtime_combo, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(runtime_combo, zmk_custom_settings_initialized);
ZMK_SUBSCRIPTION(runtime_combo, zmk_custom_setting_changed);

static int runtime_combo_init(void) {
    k_work_init_delayable(&timeout_task, combo_timeout_handler);
    zmk_runtime_combo_invalidate_cache();
    return 0;
}

SYS_INIT(runtime_combo_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

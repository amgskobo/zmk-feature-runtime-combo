/* SPDX-License-Identifier: MIT
 * Studio lock policy of the runtime combo RPC, with the real handler and real
 * nanopb codec. The runtime combo store, custom settings, behaviors and the
 * Studio lock are controlled; each mutator counts its calls.
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pb_decode.h>
#include <pb_encode.h>

#ifdef NDEBUG
#error "Runtime combo RPC lock tests require assertions enabled"
#endif

#define LOG_MODULE_DECLARE(...)
#define LOG_MODULE_REGISTER(...)
#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_WRN(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define BUILD_ASSERT(condition, message) _Static_assert(condition, message)
#define ZMK_STUDIO_RPC_HANDLER_UNSECURED 0
#define ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS(...) .unused = 0
#define ZMK_RPC_CUSTOM_SUBSYSTEM(...)
#define ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER(...)
#define ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER_ALLOCATE(name, callback)                          \
    allocate_response(callback)

typedef uint16_t zmk_behavior_local_id_t;
struct zmk_behavior_binding {
    const char *behavior_dev;
    uint32_t param1;
    uint32_t param2;
};
struct zmk_rpc_custom_subsystem_meta {
    int unused, security;
};
typedef struct {
    struct {
        size_t size;
        uint8_t bytes[256];
    } payload;
} zmk_custom_CallRequest;

enum zmk_studio_core_lock_state {
    ZMK_STUDIO_CORE_LOCK_STATE_LOCKED = 0,
    ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED = 1,
};
static enum zmk_studio_core_lock_state lock_state;
static enum zmk_studio_core_lock_state zmk_studio_core_get_lock_state(void) { return lock_state; }

#include <cormoran/runtime_combo/runtime_combo.pb.h>

static cormoran_runtime_combo_Response response;
static cormoran_runtime_combo_Response *allocate_response(pb_callback_t *callback) {
    memset(&response, 0, sizeof(response));
    memset(callback, 0, sizeof(*callback));
    return &response;
}

static unsigned mutations;
static const char key_press[] = "key_press";
static zmk_behavior_local_id_t zmk_behavior_get_local_id(const char *name) {
    (void)name;
    return 1;
}
static const char *zmk_behavior_find_behavior_name_from_local_id(zmk_behavior_local_id_t id) {
    return id == 1 ? key_press : NULL;
}
static int zmk_custom_settings_save_scope(const char *subsystem, const char *key,
                                          const char *prefix, uint32_t *affected) {
    (void)subsystem, (void)key, (void)prefix;
    mutations++;
    *affected = 0;
    return 0;
}
static int zmk_custom_settings_discard_scope(const char *subsystem, const char *key,
                                             const char *prefix, uint32_t *affected) {
    (void)subsystem, (void)key, (void)prefix;
    mutations++;
    *affected = 0;
    return 0;
}

#include <cormoran/runtime_combo/runtime_combo.h>

/* Slot 0 holds a combo; slot 1 a record whose behavior left the keymap. */
int zmk_runtime_combo_read(uint32_t index, struct zmk_runtime_combo_config *combo) {
    if (index == 1) {
        return -ENODEV;
    }
    if (index != 0) {
        return -ENOENT;
    }
    *combo = (struct zmk_runtime_combo_config){
        .enabled = true,
        .key_position_len = 2,
        .behavior = {.behavior_dev = key_press, .param1 = 5},
        .key_positions = {0, 1},
    };
    return 0;
}
int zmk_runtime_combo_read_name(uint32_t index, char *name, size_t size) {
    (void)index;
    snprintf(name, size, "combo");
    return 0;
}
int zmk_runtime_combo_read_global_settings(struct zmk_runtime_combo_global_settings *settings) {
    *settings = (struct zmk_runtime_combo_global_settings){.timeout_ms = 50, .max_combo = 4};
    return 0;
}
uint32_t zmk_runtime_combo_count(void) { return 2; }
uint32_t zmk_runtime_combo_max_count(void) { return 4; }
bool zmk_runtime_combo_has_default(uint32_t index) { return index == 0; }
bool zmk_runtime_combo_has_override(uint32_t index) { return false; }
#define MUTATOR(...)                                                                               \
    {                                                                                              \
        mutations++;                                                                               \
        return 0;                                                                                  \
    }
int zmk_runtime_combo_write(uint32_t i, const struct zmk_runtime_combo_config *c, bool p)
    MUTATOR() int zmk_runtime_combo_write_name(uint32_t i, const char *n, bool p)
        MUTATOR() int zmk_runtime_combo_write_timeout_ms(uint16_t t, bool p)
            MUTATOR() int zmk_runtime_combo_write_slow_release(bool s, bool p)
                MUTATOR() int zmk_runtime_combo_write_require_prior_idle_ms(uint16_t r, bool p)
                    MUTATOR() int zmk_runtime_combo_delete(uint32_t i, bool p)
                        MUTATOR() int zmk_runtime_combo_reset(uint32_t i)
                            MUTATOR() void zmk_runtime_combo_invalidate_cache(void) {
    mutations++;
}

#include HANDLER_SOURCE

static void call(pb_size_t tag) {
    cormoran_runtime_combo_Request req = cormoran_runtime_combo_Request_init_zero;
    req.which_request_type = tag;
    if (tag == cormoran_runtime_combo_Request_set_combo_tag) {
        req.request_type.set_combo.key_positions_count = 2;
        req.request_type.set_combo.key_positions[1] = 1;
        req.request_type.set_combo.has_behavior = true;
        req.request_type.set_combo.behavior.behavior_id = 1;
    }
    zmk_custom_CallRequest raw = {0};
    pb_ostream_t out = pb_ostream_from_buffer(raw.payload.bytes, sizeof(raw.payload.bytes));
    assert(pb_encode(&out, cormoran_runtime_combo_Request_fields, &req));
    raw.payload.size = out.bytes_written;
    pb_callback_t callback = {0};
    assert(runtime_combo_rpc_handle_request(&raw, &callback));
}

static void name_codec_bounds(void) {
    const size_t lengths[] = {63, 64, 63, 66};
    for (size_t i = 0; i < ARRAY_SIZE(lengths); i++) {
        zmk_custom_CallRequest raw = {0};
        /* A Web-style length-delimited name, including names the bounded
         * nanopb C request cannot represent. The latter must fail decoding
         * before the controlled name writer sees any mutation. */
        raw.payload.bytes[0] = (cormoran_runtime_combo_Request_set_combo_name_tag << 3) | 2;
        raw.payload.bytes[1] = lengths[i] + 2;
        raw.payload.bytes[2] = 0x12;
        raw.payload.bytes[3] = lengths[i];
        const uint8_t utf8[] = {0xE7, 0x95, 0x8C};
        for (size_t j = 0; j < lengths[i]; j++) {
            raw.payload.bytes[4 + j] = i < 2 ? 'x' : utf8[j % sizeof(utf8)];
        }
        raw.payload.size = lengths[i] + 4;
        unsigned before = mutations;
        pb_callback_t callback = {0};
        assert(runtime_combo_rpc_handle_request(&raw, &callback));
        if (lengths[i] == ZMK_RUNTIME_COMBO_NAME_MAX_BYTES) {
            assert(response.which_response_type == cormoran_runtime_combo_Response_status_tag);
            assert(mutations == before + 1);
        } else {
            assert(response.which_response_type == cormoran_runtime_combo_Response_error_tag);
            assert(mutations == before);
        }
    }
}

int main(void) {
    const pb_size_t changes[] = {
        cormoran_runtime_combo_Request_set_combo_tag,
        cormoran_runtime_combo_Request_set_combo_name_tag,
        cormoran_runtime_combo_Request_delete_combo_tag,
        cormoran_runtime_combo_Request_set_timeout_ms_tag,
        cormoran_runtime_combo_Request_set_slow_release_tag,
        cormoran_runtime_combo_Request_save_tag,
        cormoran_runtime_combo_Request_discard_tag,
        cormoran_runtime_combo_Request_set_require_prior_idle_ms_tag,
        cormoran_runtime_combo_Request_reset_combo_tag,
    };

    /* Locked: every change is refused before it reaches the store. */
    lock_state = ZMK_STUDIO_CORE_LOCK_STATE_LOCKED;
    for (size_t i = 0; i < sizeof(changes) / sizeof(changes[0]); i++) {
        call(changes[i]);
        assert(response.which_response_type == cormoran_runtime_combo_Response_error_tag);
        assert(strcmp(response.response_type.error.message, "Unlock required") == 0);
    }
    assert(mutations == 0);

    /* Reads still work while locked; the unreadable slot is left out of the
     * list instead of failing it. */
    call(cormoran_runtime_combo_Request_list_combos_tag);
    assert(response.which_response_type == cormoran_runtime_combo_Response_list_combos_tag);
    assert(response.response_type.list_combos.combos_count == 1);
    assert(response.response_type.list_combos.combos[0].index == 0);
    call(cormoran_runtime_combo_Request_get_combo_tag);
    assert(response.which_response_type == cormoran_runtime_combo_Response_get_combo_tag);
    call(cormoran_runtime_combo_Request_get_global_settings_tag);
    assert(response.which_response_type == cormoran_runtime_combo_Response_get_global_settings_tag);
    assert(mutations == 0);

    /* Unlocked: no change is refused for the lock. Empty requests may still
     * fail the handlers' own validation; save and discard need no arguments
     * and must reach the store. */
    lock_state = ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED;
    for (size_t i = 0; i < sizeof(changes) / sizeof(changes[0]); i++) {
        call(changes[i]);
        assert(response.which_response_type != cormoran_runtime_combo_Response_error_tag ||
               strcmp(response.response_type.error.message, "Unlock required") != 0);
    }
    unsigned before = mutations;
    call(cormoran_runtime_combo_Request_save_tag);
    assert(response.which_response_type == cormoran_runtime_combo_Response_status_tag);
    call(cormoran_runtime_combo_Request_discard_tag);
    assert(response.which_response_type == cormoran_runtime_combo_Response_status_tag);
    assert(mutations > before);
    name_codec_bounds();

    (void)runtime_combo_meta;
    printf("runtime combo rpc lock: PASS variant=%s changes=%zu\n", TEST_VARIANT,
           sizeof(changes) / sizeof(changes[0]));
    return 0;
}

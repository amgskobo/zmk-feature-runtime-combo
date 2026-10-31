/* SPDX-License-Identifier: MIT */
static bool runtime_combo_test_setup_done;

static int runtime_combo_test_setup(void) {
    if (runtime_combo_test_setup_done) {
        return 0;
    }

    const char *key_press = "key_press";
    zmk_behavior_local_id_t behavior_id = zmk_behavior_get_local_id(key_press);
    if (behavior_id == 0 || behavior_id == UINT16_MAX) {
        LOG_ERR("Runtime combo test cannot find key_press behavior");
        return -ENODEV;
    }

    struct zmk_runtime_combo_config combo = {
        .enabled = true,
        .key_position_len = 2,
        .behavior = {.behavior_dev = key_press, .param1 = B},
        .key_positions = {0, 1},
    };

    int ret = zmk_runtime_combo_write(0, &combo, false);
    if (ret < 0) {
        LOG_ERR("Runtime combo test setup failed: %d", ret);
        return ret;
    }

#if RUNTIME_COMBO_HAS_DEFAULTS
    /* Exercises the DT-default read-path fallback (see tests/defaults):
     * slot 1's default is left untouched, slot 2's is tombstoned, slot 3's
     * is overridden to a different behavior, and slot 4's is tombstoned then
     * restored via reset. Slot numbers and positions must match that
     * fixture's cormoran,runtime-combo-defaults node. */
    ret = zmk_runtime_combo_delete(2, false);
    if (ret < 0) {
        LOG_ERR("Runtime combo default tombstone setup failed: %d", ret);
        return ret;
    }

    struct zmk_runtime_combo_config default_override = {
        .enabled = true,
        .key_position_len = 2,
        .behavior = {.behavior_dev = key_press, .param1 = J},
        .key_positions = {6, 7},
    };
    ret = zmk_runtime_combo_write(3, &default_override, false);
    if (ret < 0) {
        LOG_ERR("Runtime combo default override setup failed: %d", ret);
        return ret;
    }

    ret = zmk_runtime_combo_delete(4, false);
    if (ret < 0) {
        LOG_ERR("Runtime combo default reset setup (delete) failed: %d", ret);
        return ret;
    }
    ret = zmk_runtime_combo_reset(4);
    if (ret < 0) {
        LOG_ERR("Runtime combo default reset setup (reset) failed: %d", ret);
        return ret;
    }
#elif IS_ENABLED(CONFIG_ZMK_RUNTIME_COMBO_TEST_DISCARD)
    /* Written to memory only, then reverted outside of
     * zmk_runtime_combo_write()/reset() via the same
     * zmk_custom_settings_discard_scope() + zmk_runtime_combo_invalidate_cache()
     * pair the Save/Discard RPC handlers use, to exercise that cache
     * invalidation path directly (see tests/discard). */
    struct zmk_runtime_combo_config to_be_discarded = {
        .enabled = true,
        .key_position_len = 2,
        .behavior = {.behavior_dev = key_press, .param1 = C},
        .key_positions = {2, 3},
    };
    ret = zmk_runtime_combo_write(1, &to_be_discarded, false);
    if (ret < 0) {
        LOG_ERR("Runtime combo discard setup (write) failed: %d", ret);
        return ret;
    }
    uint32_t discard_affected = 0;
    ret = zmk_custom_settings_discard_scope(ZMK_RUNTIME_COMBO_SUBSYSTEM_ID, NULL, NULL,
                                            &discard_affected);
    if (ret < 0) {
        LOG_ERR("Runtime combo discard setup (discard_scope) failed: %d", ret);
        return ret;
    }
    zmk_runtime_combo_invalidate_cache();
#else
    /* Slots 1-4 exercise overlap resolution and per-combo overrides (see
     * tests/overrides). Skipped when a cormoran,runtime-combo-defaults node
     * is compiled in, since that fixture (tests/defaults) reuses these slot
     * numbers to exercise the default/override/reset paths instead. */

    /* Subset of combo 2 below; used to exercise overlap resolution. */
    struct zmk_runtime_combo_config overlap_subset = {
        .enabled = true,
        .key_position_len = 2,
        .behavior = {.behavior_dev = key_press, .param1 = C},
        .key_positions = {2, 3},
    };
    ret = zmk_runtime_combo_write(1, &overlap_subset, false);
    if (ret < 0) {
        LOG_ERR("Runtime combo overlap subset setup failed: %d", ret);
        return ret;
    }

    /* Strict superset of combo 1 above; the longer combo should always win. */
    struct zmk_runtime_combo_config overlap_superset = {
        .enabled = true,
        .key_position_len = 3,
        .behavior = {.behavior_dev = key_press, .param1 = D},
        .key_positions = {2, 3, 4},
    };
    ret = zmk_runtime_combo_write(2, &overlap_superset, false);
    if (ret < 0) {
        LOG_ERR("Runtime combo overlap superset setup failed: %d", ret);
        return ret;
    }

    /* Requires a full second of prior idle time; used to exercise the
     * per-combo require-prior-idle override (global default is disabled). */
    struct zmk_runtime_combo_config idle_guarded = {
        .enabled = true,
        .key_position_len = 2,
        .behavior = {.behavior_dev = key_press, .param1 = E},
        .key_positions = {5, 6},
        .require_prior_idle_ms = 1000,
    };
    ret = zmk_runtime_combo_write(3, &idle_guarded, false);
    if (ret < 0) {
        LOG_ERR("Runtime combo idle-guarded setup failed: %d", ret);
        return ret;
    }

    /* Longer timeout than the global default; used to exercise the per-combo
     * timeout override. */
    struct zmk_runtime_combo_config long_timeout = {
        .enabled = true,
        .key_position_len = 2,
        .behavior = {.behavior_dev = key_press, .param1 = F},
        .key_positions = {7, 8},
        .timeout_ms = 1000,
    };
    ret = zmk_runtime_combo_write(4, &long_timeout, false);
    if (ret < 0) {
        LOG_ERR("Runtime combo long-timeout setup failed: %d", ret);
        return ret;
    }
#endif

    runtime_combo_test_setup_done = true;
    LOG_INF("PASS: runtime_combo_test_setup");
    return 0;
}

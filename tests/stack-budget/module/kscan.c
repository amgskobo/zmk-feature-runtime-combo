/* SPDX-License-Identifier: MIT */
/* No scheduled input: the fixture injects position events on the actual
 * system work queue. This boundary does not model GPIO, a bus or a radio. */
#define DT_DRV_COMPAT amgskobo_combo_stack_kscan
#include <zephyr/device.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/sys/util.h>

static int configure(const struct device *dev, kscan_callback_t callback) {
    ARG_UNUSED(dev);
    ARG_UNUSED(callback);
    return 0;
}
static int enable(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}
static int disable(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}
static const struct kscan_driver_api api = {
    .config = configure, .enable_callback = enable, .disable_callback = disable};
DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
                      &api);

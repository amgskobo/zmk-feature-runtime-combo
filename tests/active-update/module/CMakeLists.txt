# SPDX-License-Identifier: MIT
# ZMK application headers must not depend on another module's global includes.
target_sources(app PRIVATE src/active_update.c)

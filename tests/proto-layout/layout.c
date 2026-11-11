/* SPDX-License-Identifier: MIT
 * Compile the actual generated storage types with the selected ARM ABI.
 */
#include <cormoran/runtime_combo/runtime_combo.pb.h>

char response_type[sizeof(cormoran_runtime_combo_Response)];
char combo_type[sizeof(cormoran_runtime_combo_Combo)];
char position_word[sizeof(((cormoran_runtime_combo_Combo *)0)->key_positions[0])];
char layer_word[sizeof(((cormoran_runtime_combo_Combo *)0)->layer_mask)];
char binding_param1[sizeof(((cormoran_runtime_combo_BehaviorBinding *)0)->param1)];
char binding_param2[sizeof(((cormoran_runtime_combo_BehaviorBinding *)0)->param2)];
char request_index[sizeof(((cormoran_runtime_combo_GetComboRequest *)0)->index)];
char request_timeout[sizeof(((cormoran_runtime_combo_SetComboRequest *)0)->timeout_ms)];
char request_prior[sizeof(((cormoran_runtime_combo_SetComboRequest *)0)->require_prior_idle_ms)];
char request_position[sizeof(((cormoran_runtime_combo_SetComboRequest *)0)->key_positions[0])];

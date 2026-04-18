#ifndef USER_CUSTS1_IMPL_H
#define USER_CUSTS1_IMPL_H

#include <stdint.h>
#include <stdbool.h>

#ifndef PP_TARGET_TEST
#include "ke_msg.h"
#endif

void user_custs1_ctrl_wr_handler(ke_msg_id_t msgid,
                                 const void *param,
                                 ke_task_id_t dest_id,
                                 ke_task_id_t src_id);

void user_custs1_status_ntf_cfg_handler(ke_msg_id_t msgid,
                                        const void *param,
                                        ke_task_id_t dest_id,
                                        ke_task_id_t src_id);

void user_custs1_att_info_handler(ke_msg_id_t msgid,
                                  const void *param,
                                  ke_task_id_t dest_id,
                                  ke_task_id_t src_id);

bool     pp_custs1_is_ctrl_handle(uint16_t handle);
bool     pp_custs1_is_status_ntf_cfg_handle(uint16_t handle);
bool     pp_custs1_status_ntf_enabled(void);
void     pp_custs1_reset_state(void);
void     pp_custs1_send_status_ntf(uint8_t status);

#endif /* USER_CUSTS1_IMPL_H */

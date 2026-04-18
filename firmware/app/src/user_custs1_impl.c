#include "user_custs1_impl.h"
#include "user_custs1_def.h"
#include "user_custs_config.h"
#include "pp_pipeline_service.h"
#include "paddling_pulse_app.h"
#include "app_customs.h"
#include "custs1_task.h"
#include "prf_utils.h"
#include "app.h"

static bool s_status_ntf_enabled;

/* ---- cust_prf_funcs registration ---- */

const struct cust_prf_func_callbacks cust_prf_funcs[] =
{
    {
        .task_id              = TASK_ID_CUSTS1,
        .att_db               = custs1_att_db,
        .max_nb_att           = PP_PIPE_IDX_NB,
        .db_create_func       = app_custs1_create_db,
        .enable_func          = NULL,
        .init_func            = NULL,
        .value_wr_validation_func = NULL,
    },
    {TASK_ID_INVALID, NULL, 0, NULL, NULL, NULL, NULL},
};

/* ---- Handle classification helpers ---- */

bool pp_custs1_is_ctrl_handle(uint16_t handle)
{
    return (handle == PP_PIPE_IDX_CTRL_VAL);
}

bool pp_custs1_is_status_ntf_cfg_handle(uint16_t handle)
{
    return (handle == PP_PIPE_IDX_STATUS_NTF_CFG);
}

bool pp_custs1_status_ntf_enabled(void)
{
    return s_status_ntf_enabled;
}

void pp_custs1_reset_state(void)
{
    s_status_ntf_enabled = false;
}

/* ---- CUSTS1_VAL_WRITE_IND handlers ---- */

void user_custs1_ctrl_wr_handler(ke_msg_id_t msgid,
                                 const void *param,
                                 ke_task_id_t dest_id,
                                 ke_task_id_t src_id)
{
    const struct custs1_val_write_ind *ind =
        (const struct custs1_val_write_ind *)param;

    pp_pipeline_service_on_write(ind->value, ind->length);

    (void)msgid;
    (void)dest_id;
    (void)src_id;
}

void user_custs1_status_ntf_cfg_handler(ke_msg_id_t msgid,
                                        const void *param,
                                        ke_task_id_t dest_id,
                                        ke_task_id_t src_id)
{
    const struct custs1_val_write_ind *ind =
        (const struct custs1_val_write_ind *)param;

    if (ind->length >= 2)
    {
        uint16_t cccd = (uint16_t)ind->value[0] | ((uint16_t)ind->value[1] << 8);
        s_status_ntf_enabled = (cccd != 0);
    }

    (void)msgid;
    (void)dest_id;
    (void)src_id;
}

/* ---- CUSTS1_ATT_INFO_REQ handler ---- */

void user_custs1_att_info_handler(ke_msg_id_t msgid,
                                  const void *param,
                                  ke_task_id_t dest_id,
                                  ke_task_id_t src_id)
{
    const struct custs1_att_info_req *req =
        (const struct custs1_att_info_req *)param;

    struct custs1_att_info_rsp *rsp = KE_MSG_ALLOC(CUSTS1_ATT_INFO_RSP,
                                                   src_id,
                                                   dest_id,
                                                   custs1_att_info_rsp);

    rsp->conidx  = app_env[req->conidx].conidx;
    rsp->att_idx = req->att_idx;
    rsp->length  = 0;
    rsp->status  = ATT_ERR_NO_ERROR;

    if (req->att_idx == PP_PIPE_IDX_CTRL_VAL)
    {
        rsp->length = PP_PIPE_CTRL_CHAR_LEN;
    }
    else if (req->att_idx == PP_PIPE_IDX_STATUS_NTF_CFG)
    {
        rsp->length = sizeof(uint16_t);
    }

    KE_MSG_SEND(rsp);

    (void)msgid;
}

/* ---- Status notification sender ---- */

void pp_custs1_send_status_ntf(uint8_t status)
{
    if (!s_status_ntf_enabled)
        return;

    if (app_connection_idx == GAP_INVALID_CONIDX)
        return;

    struct custs1_val_ntf_ind_req *req = KE_MSG_ALLOC_DYN(
        CUSTS1_VAL_NTF_REQ,
        prf_get_task_from_id(TASK_ID_CUSTS1),
        TASK_APP,
        custs1_val_ntf_ind_req,
        PP_PIPE_STATUS_CHAR_LEN);

    req->conidx       = app_env[app_connection_idx].conidx;
    req->notification = true;
    req->handle       = PP_PIPE_IDX_STATUS_VAL;
    req->length       = PP_PIPE_STATUS_CHAR_LEN;
    req->value[0]     = status;

    KE_MSG_SEND(req);
}

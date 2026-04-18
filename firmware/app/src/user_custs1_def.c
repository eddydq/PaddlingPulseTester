#include "user_custs1_def.h"

#include <stdint.h>
#include "att.h"

static const att_svc_desc128_t custs1_svc1 = PP_PIPE_SVC_UUID_128;

static const uint8_t s_ctrl_uuid[ATT_UUID_128_LEN] = PP_PIPE_CTRL_UUID_128;
static const uint8_t s_status_uuid[ATT_UUID_128_LEN] = PP_PIPE_STATUS_UUID_128;

static const uint16_t s_att_decl_svc = ATT_DECL_PRIMARY_SERVICE;
static const uint16_t s_att_decl_char = ATT_DECL_CHARACTERISTIC;
static const uint16_t s_att_desc_cfg = ATT_DESC_CLIENT_CHAR_CFG;

const uint8_t custs1_services[] = { PP_PIPE_IDX_SVC, PP_PIPE_IDX_NB };
const uint8_t custs1_services_size = 1U;
const uint16_t custs1_att_max_nb = PP_PIPE_IDX_NB;

struct attm_desc_128 custs1_att_db[PP_PIPE_IDX_NB] =
{
    [PP_PIPE_IDX_SVC] = {
        (uint8_t *)&s_att_decl_svc,
        ATT_UUID_128_LEN,
        PERM(RD, ENABLE),
        sizeof(custs1_svc1),
        sizeof(custs1_svc1),
        (uint8_t *)&custs1_svc1,
    },

    [PP_PIPE_IDX_CTRL_CHAR] = {
        (uint8_t *)&s_att_decl_char,
        ATT_UUID_16_LEN,
        PERM(RD, ENABLE),
        0,
        0,
        NULL,
    },

    [PP_PIPE_IDX_CTRL_VAL] = {
        (uint8_t *)s_ctrl_uuid,
        ATT_UUID_128_LEN,
        PERM(WR, ENABLE) | PERM(WRITE_REQ, ENABLE) | PERM(WRITE_COMMAND, ENABLE),
        PP_PIPE_CTRL_CHAR_LEN,
        0,
        NULL,
    },

    [PP_PIPE_IDX_STATUS_CHAR] = {
        (uint8_t *)&s_att_decl_char,
        ATT_UUID_16_LEN,
        PERM(RD, ENABLE),
        0,
        0,
        NULL,
    },

    [PP_PIPE_IDX_STATUS_VAL] = {
        (uint8_t *)s_status_uuid,
        ATT_UUID_128_LEN,
        PERM(RD, ENABLE) | PERM(NTF, ENABLE),
        PP_PIPE_STATUS_CHAR_LEN,
        0,
        NULL,
    },

    [PP_PIPE_IDX_STATUS_NTF_CFG] = {
        (uint8_t *)&s_att_desc_cfg,
        ATT_UUID_16_LEN,
        PERM(RD, ENABLE) | PERM(WR, ENABLE) | PERM(WRITE_REQ, ENABLE),
        sizeof(uint16_t),
        0,
        NULL,
    },
};

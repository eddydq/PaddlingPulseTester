#ifndef USER_CUSTS1_DEF_H
#define USER_CUSTS1_DEF_H

#ifndef PP_TARGET_TEST
#include "attm_db_128.h"
#endif

/*
 * 128-bit UUIDs (little-endian byte order)
 *
 * Service:       50500001-4F54-4150-5049-504C494E4500
 * Control Point: 50500002-4F54-4150-5049-504C494E4500
 * Status:        50500003-4F54-4150-5049-504C494E4500
 */
#define PP_PIPE_SVC_UUID_128    {0x00, 0x45, 0x4E, 0x49, 0x4C, 0x50, 0x49, 0x50, \
                                 0x50, 0x41, 0x54, 0x4F, 0x01, 0x00, 0x50, 0x50}

#define PP_PIPE_CTRL_UUID_128   {0x00, 0x45, 0x4E, 0x49, 0x4C, 0x50, 0x49, 0x50, \
                                 0x50, 0x41, 0x54, 0x4F, 0x02, 0x00, 0x50, 0x50}

#define PP_PIPE_STATUS_UUID_128 {0x00, 0x45, 0x4E, 0x49, 0x4C, 0x50, 0x49, 0x50, \
                                 0x50, 0x41, 0x54, 0x4F, 0x03, 0x00, 0x50, 0x50}

#define PP_PIPE_CTRL_CHAR_LEN   244
#define PP_PIPE_STATUS_CHAR_LEN 1

enum
{
    PP_PIPE_IDX_SVC,

    PP_PIPE_IDX_CTRL_CHAR,
    PP_PIPE_IDX_CTRL_VAL,

    PP_PIPE_IDX_STATUS_CHAR,
    PP_PIPE_IDX_STATUS_VAL,
    PP_PIPE_IDX_STATUS_NTF_CFG,

    PP_PIPE_IDX_NB,
};

#ifndef PP_TARGET_TEST
extern const uint8_t custs1_services[];
extern const uint8_t custs1_services_size;
extern const uint16_t custs1_att_max_nb;
extern struct attm_desc_128 custs1_att_db[PP_PIPE_IDX_NB];
#endif

#endif /* USER_CUSTS1_DEF_H */

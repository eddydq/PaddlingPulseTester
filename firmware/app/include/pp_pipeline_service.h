#ifndef PP_PIPELINE_SERVICE_H
#define PP_PIPELINE_SERVICE_H

#include <stdint.h>

#define PP_SVC_STATUS_IDLE           0x00
#define PP_SVC_STATUS_RECEIVING      0x01
#define PP_SVC_STATUS_VALID_RESET    0x02
#define PP_SVC_STATUS_ERR_CRC        0x80
#define PP_SVC_STATUS_ERR_BLOCK_ID   0x81
#define PP_SVC_STATUS_ERR_GRAPH      0x82
#define PP_SVC_STATUS_ERR_TOO_LARGE  0x83
#define PP_SVC_STATUS_ERR_SOURCE_CFG 0x84

#define PP_CHUNK_FLAG_FIRST 0x01
#define PP_CHUNK_FLAG_LAST  0x02
#define PP_CHUNK_FLAG_ABORT 0x04

void pp_pipeline_service_init(void);
void pp_pipeline_service_on_write(const uint8_t *data, uint16_t len);
uint8_t pp_pipeline_service_status(void);

#endif /* PP_PIPELINE_SERVICE_H */

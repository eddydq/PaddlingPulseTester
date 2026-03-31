#ifndef _PADDLING_PULSE_CONSOLE_H_
#define _PADDLING_PULSE_CONSOLE_H_

#include <stdbool.h>
#include "ke_msg.h"

void paddling_pulse_console_init(void);
bool paddling_pulse_console_handle_message(ke_msg_id_t const msgid, void const *param);

#endif // _PADDLING_PULSE_CONSOLE_H_

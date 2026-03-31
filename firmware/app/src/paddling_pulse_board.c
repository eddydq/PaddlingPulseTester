/**
 ****************************************************************************************
 *
 * @file paddling_pulse_board.c
 *
 * @brief PaddlingPulse board setup and initialization.
 *
 * Copyright (C) 2015-2025 Renesas Electronics Corporation and/or its affiliates.
 * All rights reserved. Confidential Information.
 *
 * This software ("Software") is supplied by Renesas Electronics Corporation and/or its
 * affiliates ("Renesas"). Renesas grants you a personal, non-exclusive, non-transferable,
 * revocable, non-sub-licensable right and license to use the Software, solely if used in
 * or together with Renesas products. You may make copies of this Software, provided this
 * copyright notice and disclaimer ("Notice") is included in all such copies. Renesas
 * reserves the right to change or discontinue the Software at any time without notice.
 *
 * THE SOFTWARE IS PROVIDED "AS IS". RENESAS DISCLAIMS ALL WARRANTIES OF ANY KIND,
 * WHETHER EXPRESS, IMPLIED, OR STATUTORY, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. TO THE
 * MAXIMUM EXTENT PERMITTED UNDER LAW, IN NO EVENT SHALL RENESAS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE, EVEN IF RENESAS HAS BEEN ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGES. USE OF THIS SOFTWARE MAY BE SUBJECT TO TERMS AND CONDITIONS CONTAINED IN
 * AN ADDITIONAL AGREEMENT BETWEEN YOU AND RENESAS. IN CASE OF CONFLICT BETWEEN THE TERMS
 * OF THIS NOTICE AND ANY SUCH ADDITIONAL LICENSE AGREEMENT, THE TERMS OF THE AGREEMENT
 * SHALL TAKE PRECEDENCE. BY CONTINUING TO USE THIS SOFTWARE, YOU AGREE TO THE TERMS OF
 * THIS NOTICE.IF YOU DO NOT AGREE TO THESE TERMS, YOU ARE NOT PERMITTED TO USE THIS
 * SOFTWARE.
 *
 ****************************************************************************************
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */

#include "paddling_pulse_board.h"
#include "datasheet.h"
#include "system_library.h"
#include "rwip_config.h"
#include "gpio.h"
#include "uart.h"
#include "syscntl.h"
#include "fpga_helper.h"

/*
 * GLOBAL VARIABLE DEFINITIONS
 ****************************************************************************************
 */

#if DEVELOPMENT_DEBUG

void GPIO_reservations(void)
{
/*
    i.e. to reserve P0_1 as Generic Purpose I/O:
    RESERVE_GPIO(DESCRIPTIVE_NAME, GPIO_PORT_0, GPIO_PIN_1, PID_GPIO);
*/

#if defined (CFG_PRINTF)
    RESERVE_GPIO(UART1_SW, UART1_SW_PORT, UART1_SW_PIN, PID_UART1_RX);
#endif

    RESERVE_GPIO(SPI_EN, SPI_EN_PORT, SPI_EN_PIN, PID_SPI_EN);
}

#endif

void set_pad_functions(void)
{
/*
    i.e. to set P0_1 as Generic purpose Output:
    GPIO_ConfigurePin(GPIO_PORT_0, GPIO_PIN_1, OUTPUT, PID_GPIO, false);
*/

    // Disallow spontaneous SPI Flash wake-up
    GPIO_ConfigurePin(SPI_EN_PORT, SPI_EN_PIN, OUTPUT, PID_SPI_EN, true);

#if defined (CFG_PRINTF)
    GPIO_ConfigurePin(UART1_SW_PORT, UART1_SW_PIN, INPUT, PID_UART1_RX, false);
#endif
}

#if defined (CFG_PRINTF)
// Configuration struct for UART1 SDK driver
static const uart_cfg_t uart_cfg = {
    .baud_rate = UART1_BAUDRATE,
    .data_bits = UART1_DATABITS,
    .parity = UART1_PARITY,
    .stop_bits = UART1_STOPBITS,
    .auto_flow_control = UART1_AFCE,
    .use_fifo = UART1_FIFO,
    .tx_fifo_tr_lvl = UART1_TX_FIFO_LEVEL,
    .rx_fifo_tr_lvl = UART1_RX_FIFO_LEVEL,
    .intr_priority = 2,
};
#endif

void periph_init(void)
{
    // Select FPGA GPIO_MAP 2 to work with the FPGA add-on board SPI memory flash
    // set debugger SWD to SW_CLK = P0[2], SW_DIO=P0[5]
    FPGA_HELPER(FPGA_GPIO_MAP_2, SWD_DATA_AT_P0_5);

    // In Boost mode enable the DCDC converter to supply VBAT_HIGH for the used GPIOs
    syscntl_dcdc_turn_on_in_boost(SYSCNTL_DCDC_LEVEL_3V0);

    // ROM patch
    patch_func();

    // Initialize peripherals
#if defined (CFG_PRINTF)
    uart_initialize(UART1, &uart_cfg);
#endif

    // Set pad functionality
    set_pad_functions();

#if defined (CFG_PRINTF)
    uart_one_wire_enable(UART1, UART1_SW_PORT, UART1_SW_PIN);
    uart_one_wire_tx_en(UART1);
#endif

    // Enable the pads
    GPIO_set_pad_latch_en(true);
}

/**
 ****************************************************************************************
 *
 * @file da14531_config_basic.h
 *
 * @brief Basic compile configuration file.
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

#ifndef _DA14531_CONFIG_BASIC_H_
#define _DA14531_CONFIG_BASIC_H_

#include "da1458x_stack_config.h"
#include "user_profiles_config.h"

/***************************************************************************************************************/
/* Integrated or external processor configuration                                                              */
/*    -defined      Integrated processor mode. Host application runs in DA14585 processor. Host application    */
/*                  is the TASK_APP kernel task.                                                               */
/*    -undefined    External processor mode. Host application runs on an external processor. Communicates with */
/*                  BLE application through GTL protocol over a signalling iface (UART, SPI etc)               */
/***************************************************************************************************************/
#define CFG_APP

/****************************************************************************************************************/
/* Enables the BLE security functionality in TASK_APP. If not defined BLE security related code is compiled out.*/
/****************************************************************************************************************/
#undef CFG_APP_SECURITY

/****************************************************************************************************************/
/* Enables WatchDog timer.                                                                                      */
/****************************************************************************************************************/
#define CFG_WDOG

/****************************************************************************************************************/
/* Watchdog timer behavior in production mode:                                                                  */
/*     Flag is not defined: Watchdog timer generates NMI at value 0.                                            */
/*     Flag is defined    : Watchdog timer generates a WDOG (SYS) reset at value 0.                             */
/****************************************************************************************************************/
#undef CFG_WDG_TRIGGER_HW_RESET_IN_PRODUCTION_MODE

/****************************************************************************************************************/
/* Determines maximum concurrent connections supported by application. It configures the heap memory allocated  */
/* to service multiple connections. It is used for GAP central role applications. For GAP peripheral role it    */
/* should be set to 1 for optimizing memory utilization.                                                        */
/*      - MAX value for DA14531: 3                                                                              */
/****************************************************************************************************************/
#ifdef CFG_IMU_POLAR
#define CFG_MAX_CONNECTIONS     (2)
#else
#define CFG_MAX_CONNECTIONS     (2)
#endif

/****************************************************************************************************************/
/* Enables development/debug mode. For production mode builds it must be disabled.                              */
/* When enabled the following debugging features are enabled                                                    */
/*      -   Allows the emulation of the OTP mirroring to System RAM. No actual writing to RAM is done, but the  */
/*          exact same amount of time is spend as if the mirroring would take place. This is to mimic the       */
/*          behavior as if the System Code is already in OTP, and the mirroring takes place after waking up,    */
/*          but the (development) code still resides in an external source.                                     */
/*      -   Validation of GPIO reservations.                                                                    */
/*      -   Enables Debug module and sets code execution in breakpoint in Hardfault and NMI (Watchdog) handlers.*/
/*          It allows developer to hot attach debugger and get debug information                                */
/****************************************************************************************************************/
#undef CFG_DEVELOPMENT_DEBUG

/****************************************************************************************************************/
/* Console mode: single-wire UART now, AT command support later.                                                */
/****************************************************************************************************************/
#define CFG_PADDLING_PULSE_CONSOLE_MODE

#if defined(CFG_PADDLING_PULSE_CONSOLE_MODE)
    #define CFG_PADDLING_PULSE_AT_COMMANDS

    /************************************************************************************************************/
    /* UART Console Print. Keep console output on UART1 so the board layer can configure single-wire mode.      */
    /************************************************************************************************************/
    #define CFG_PRINTF
    #ifdef CFG_PRINTF_UART2
        #undef CFG_PRINTF_UART2
    #endif

    /************************************************************************************************************/
    /* UART1 Driver Implementation. Use the SDK UART1 driver for single-wire console support.                   */
    /************************************************************************************************************/
    #define CFG_UART1_SDK

    /************************************************************************************************************/
    /* Enable UART1 one-wire mode for the console pin.                                                           */
    /************************************************************************************************************/
    #define CFG_UART_ONE_WIRE_SUPPORT
#endif


/****************************************************************************************************************/
/* IMU source selection — exactly one must be defined.                                                          */
/****************************************************************************************************************/
// define CFG_IMU_LIS3DH
// #define CFG_IMU_MPU6050
#define CFG_IMU_POLAR

/****************************************************************************************************************/
/* IMU axis selection — exactly one must be defined.                                                            */
/****************************************************************************************************************/
// #define CFG_IMU_AXIS_X
// #define CFG_IMU_AXIS_Y
#define CFG_IMU_AXIS_Z

/****************************************************************************************************************/
/* Select external memory device for data storage                                                               */
/* SPI FLASH  (#define CFG_SPI_FLASH_ENABLE)                                                                    */
/* I2C EEPROM (#define CFG_I2C_EEPROM_ENABLE)                                                                   */
/****************************************************************************************************************/
#undef CFG_SPI_FLASH_ENABLE
#undef CFG_I2C_EEPROM_ENABLE

/****************************************************************************************************************/
/* Enables/Disables the DMA Support for the following interfaces:                                               */
/*     - UART                                                                                                   */
/*     - SPI                                                                                                    */
/*     - I2C                                                                                                    */
/*     - ADC                                                                                                    */
/****************************************************************************************************************/
#undef CFG_UART_DMA_SUPPORT
#undef CFG_SPI_DMA_SUPPORT
#undef CFG_I2C_DMA_SUPPORT
#undef CFG_ADC_DMA_SUPPORT

/****************************************************************************************************************/
/* Notify the SDK about the fixed power mode (currently used only for Bypass):                                  */
/*     - CFG_POWER_MODE_BYPASS = Bypass mode                                                                    */
/****************************************************************************************************************/
#undef CFG_POWER_MODE_BYPASS

#endif // _DA14531_CONFIG_BASIC_H_


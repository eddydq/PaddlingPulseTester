/**
 ****************************************************************************************
 * @file paddling_pulse_imu_lis3dh.c
 * @brief LIS3DH accelerometer driver — I2C, 100 Hz, low-power, stream FIFO.
 ****************************************************************************************
 */

#include "da14531_config_basic.h"

#ifdef CFG_IMU_LIS3DH

#include "paddling_pulse_imu_lis3dh.h"
#include "paddling_pulse_sample_store.h"
#include "paddling_pulse_board.h"
#include "i2c.h"
#include "arch.h"
#include <string.h>

/*
 * REGISTERS
 */
#define LIS3_WHO_AM_I       0x0F
#define LIS3_CTRL_REG1      0x20
#define LIS3_CTRL_REG2      0x21
#define LIS3_CTRL_REG3      0x22
#define LIS3_CTRL_REG4      0x23
#define LIS3_CTRL_REG5      0x24
#define LIS3_FIFO_CTRL_REG  0x2E
#define LIS3_FIFO_SRC_REG   0x2F
#define LIS3_OUT_X_L        0x28
#define LIS3_AUTO_INC       0x80

#define LIS3_I2C_ADDR       0x19
#define LIS3_I2C_ADDR_ALT   0x18
#define LIS3_WHO_AM_I_VAL   0x33

/* 100 Hz, low-power, XYZ enabled */
#define LIS3_CTRL1_VAL      0x5F
/* HPF on data output (FDS=1), HPIS1 */
#define LIS3_CTRL2_VAL      0x09
/* FIFO watermark interrupt on INT1 */
#define LIS3_CTRL3_VAL      0x04
/* +/-2g, 8-bit LP mode */
#define LIS3_CTRL4_VAL      0x00
/* FIFO enable */
#define LIS3_CTRL5_VAL      0x40
/* Stream mode, watermark 25 */
#define LIS3_FIFO_CTRL_VAL  0x99

#define LIS3_FIFO_MAX_SAMPLES  32
#define LIS3_BYTES_PER_SAMPLE  6

/*
 * STATE
 */
static bool s_running __SECTION_ZERO("retention_mem_area0");
static uint8_t s_addr __SECTION_ZERO("retention_mem_area0");

/*
 * I2C HELPERS
 */
static bool lis3_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    i2c_abort_t abrt = I2C_ABORT_NONE;
    i2c_master_transmit_buffer_sync(buf, 2, &abrt, I2C_F_WAIT_FOR_STOP);
    return (abrt == I2C_ABORT_NONE);
}

static bool lis3_read_reg(uint8_t reg, uint8_t *val)
{
    i2c_abort_t abrt = I2C_ABORT_NONE;
    i2c_master_transmit_buffer_sync(&reg, 1, &abrt, I2C_F_NONE);
    if (abrt != I2C_ABORT_NONE)
        return false;
    i2c_master_receive_buffer_sync(val, 1, &abrt, I2C_F_ADD_STOP);
    return (abrt == I2C_ABORT_NONE);
}

static bool lis3_read_regs(uint8_t reg, uint8_t *buf, uint16_t len)
{
    reg |= LIS3_AUTO_INC;
    i2c_abort_t abrt = I2C_ABORT_NONE;
    i2c_master_transmit_buffer_sync(&reg, 1, &abrt, I2C_F_NONE);
    if (abrt != I2C_ABORT_NONE)
        return false;
    i2c_master_receive_buffer_sync(buf, len, &abrt, I2C_F_ADD_STOP);
    return (abrt == I2C_ABORT_NONE);
}

static bool lis3_probe(uint8_t addr)
{
    i2c_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.speed          = I2C_SPEED_FAST;
    cfg.mode           = I2C_MODE_MASTER;
    cfg.addr_mode      = I2C_ADDRESSING_7B;
    cfg.address        = addr;
    cfg.tx_fifo_level  = 16;
    cfg.rx_fifo_level  = 16;
    i2c_init(&cfg);

    uint8_t id = 0;
    if (!lis3_read_reg(LIS3_WHO_AM_I, &id))
        return false;
    return (id == LIS3_WHO_AM_I_VAL);
}

/*
 * CONFIGURE
 */
static bool lis3_configure(void)
{
    if (!lis3_write_reg(LIS3_CTRL_REG1, LIS3_CTRL1_VAL)) return false;
    if (!lis3_write_reg(LIS3_CTRL_REG2, LIS3_CTRL2_VAL)) return false;
    if (!lis3_write_reg(LIS3_CTRL_REG3, LIS3_CTRL3_VAL)) return false;
    if (!lis3_write_reg(LIS3_CTRL_REG4, LIS3_CTRL4_VAL)) return false;
    if (!lis3_write_reg(LIS3_CTRL_REG5, LIS3_CTRL5_VAL)) return false;
    /* Reset FIFO to bypass, then set stream mode */
    if (!lis3_write_reg(LIS3_FIFO_CTRL_REG, 0x00)) return false;
    if (!lis3_write_reg(LIS3_FIFO_CTRL_REG, LIS3_FIFO_CTRL_VAL)) return false;
    return true;
}

/*
 * PUBLIC API
 */
bool pp_imu_lis3dh_init(void)
{
    s_running = false;
    /* Probe primary address, fall back to alternate */
    if (lis3_probe(LIS3_I2C_ADDR))
    {
        s_addr = LIS3_I2C_ADDR;
    }
    else if (lis3_probe(LIS3_I2C_ADDR_ALT))
    {
        s_addr = LIS3_I2C_ADDR_ALT;
    }
    else
    {
        return false;
    }
    return lis3_configure();
}

void pp_imu_lis3dh_start(void)
{
    s_running = true;
}

void pp_imu_lis3dh_stop(void)
{
    s_running = false;
    lis3_write_reg(LIS3_CTRL_REG1, 0x00);  /* power down */
}

void pp_imu_lis3dh_process(void)
{
    if (!s_running) return;

    uint8_t fifo_src = 0;
    if (!lis3_read_reg(LIS3_FIFO_SRC_REG, &fifo_src))
        return;

    uint8_t fifo_count = fifo_src & 0x1F;
    if (fifo_count == 0) return;
    if (fifo_count > LIS3_FIFO_MAX_SAMPLES)
        fifo_count = LIS3_FIFO_MAX_SAMPLES;

    uint8_t buf[LIS3_FIFO_MAX_SAMPLES * LIS3_BYTES_PER_SAMPLE];
    uint16_t len = (uint16_t)fifo_count * LIS3_BYTES_PER_SAMPLE;
    if (!lis3_read_regs(LIS3_OUT_X_L, buf, len))
        return;

    uint8_t i;
    for (i = 0; i < fifo_count; i++)
    {
        int16_t sample;
        /* 8-bit LP mode: only high byte is valid per axis */
#if defined(CFG_IMU_AXIS_X)
        sample = (int16_t)((int8_t)buf[i * 6 + 1]);   /* OUT_X_H */
#elif defined(CFG_IMU_AXIS_Y)
        sample = (int16_t)((int8_t)buf[i * 6 + 3]);   /* OUT_Y_H */
#elif defined(CFG_IMU_AXIS_Z)
        sample = (int16_t)((int8_t)buf[i * 6 + 5]);   /* OUT_Z_H */
#endif
        pp_sample_store_push(sample);
    }
}

bool pp_imu_lis3dh_is_running(void)
{
    return s_running;
}

#endif /* CFG_IMU_LIS3DH */

/**
 ****************************************************************************************
 * @file paddling_pulse_imu_mpu6050.c
 * @brief MPU6050 accelerometer driver — I2C, 100 Hz, +/-2g, FIFO.
 ****************************************************************************************
 */

#include "da14531_config_basic.h"

#ifdef CFG_IMU_MPU6050

#include "paddling_pulse_imu_mpu6050.h"
#include "paddling_pulse_sample_store.h"
#include "paddling_pulse_board.h"
#include "i2c.h"
#include "arch.h"
#include <string.h>

/*
 * REGISTERS
 */
#define MPU_WHO_AM_I        0x75
#define MPU_PWR_MGMT_1      0x6B
#define MPU_SMPLRT_DIV      0x19
#define MPU_CONFIG          0x1A
#define MPU_ACCEL_CONFIG    0x1C
#define MPU_FIFO_EN         0x23
#define MPU_INT_PIN_CFG     0x37
#define MPU_INT_ENABLE      0x38
#define MPU_INT_STATUS      0x3A
#define MPU_USER_CTRL       0x6A
#define MPU_FIFO_COUNT_H    0x72
#define MPU_FIFO_COUNT_L    0x73
#define MPU_FIFO_R_W        0x74

#define MPU_I2C_ADDR        0x68
#define MPU_I2C_ADDR_ALT    0x69
#define MPU_WHO_AM_I_VAL    0x68

#define MPU_INT_FIFO_OVF    0x10

/* Config: 100 Hz, +/-2g, DLPF ~44 Hz */
#define MPU_PWR_VAL         0x01    /* PLL with X gyro ref */
#define MPU_SMPLRT_VAL      0x09    /* 1 kHz / (1+9) = 100 Hz */
#define MPU_CONFIG_VAL      0x03    /* DLPF ~44 Hz */
#define MPU_ACCEL_VAL       0x00    /* +/-2g */
#define MPU_FIFO_EN_VAL     0x08    /* Accel to FIFO */
#define MPU_INT_CFG_VAL     0x20    /* Latch interrupt */
#define MPU_INT_EN_VAL      0x01    /* Data-ready IRQ */
#define MPU_USER_CTRL_VAL   0x44    /* FIFO enable + reset */

#define MPU_BYTES_PER_SAMPLE    6
#define MPU_FIFO_WM_SAMPLES    25
#define MPU_FIFO_MAX_SAMPLES   32

/*
 * STATE
 */
static bool s_running __SECTION_ZERO("retention_mem_area0");
static uint8_t s_addr __SECTION_ZERO("retention_mem_area0");

/*
 * I2C HELPERS
 */
static bool mpu_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    i2c_abort_t abrt = I2C_ABORT_NONE;
    i2c_master_transmit_buffer_sync(buf, 2, &abrt, I2C_F_WAIT_FOR_STOP);
    return (abrt == I2C_ABORT_NONE);
}

static bool mpu_read_reg(uint8_t reg, uint8_t *val)
{
    i2c_abort_t abrt = I2C_ABORT_NONE;
    i2c_master_transmit_buffer_sync(&reg, 1, &abrt, I2C_F_NONE);
    if (abrt != I2C_ABORT_NONE)
        return false;
    i2c_master_receive_buffer_sync(val, 1, &abrt, I2C_F_ADD_STOP);
    return (abrt == I2C_ABORT_NONE);
}

static bool mpu_read_regs(uint8_t reg, uint8_t *buf, uint16_t len)
{
    i2c_abort_t abrt = I2C_ABORT_NONE;
    i2c_master_transmit_buffer_sync(&reg, 1, &abrt, I2C_F_NONE);
    if (abrt != I2C_ABORT_NONE)
        return false;
    i2c_master_receive_buffer_sync(buf, len, &abrt, I2C_F_ADD_STOP);
    return (abrt == I2C_ABORT_NONE);
}

static bool mpu_probe(uint8_t addr)
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
    if (!mpu_read_reg(MPU_WHO_AM_I, &id))
        return false;
    return (id == MPU_WHO_AM_I_VAL);
}

static bool mpu_configure(void)
{
    if (!mpu_write_reg(MPU_PWR_MGMT_1, MPU_PWR_VAL))       return false;
    if (!mpu_write_reg(MPU_SMPLRT_DIV, MPU_SMPLRT_VAL))    return false;
    if (!mpu_write_reg(MPU_CONFIG, MPU_CONFIG_VAL))         return false;
    if (!mpu_write_reg(MPU_ACCEL_CONFIG, MPU_ACCEL_VAL))    return false;
    if (!mpu_write_reg(MPU_INT_PIN_CFG, MPU_INT_CFG_VAL))   return false;
    if (!mpu_write_reg(MPU_INT_ENABLE, MPU_INT_EN_VAL))     return false;
    if (!mpu_write_reg(MPU_USER_CTRL, MPU_USER_CTRL_VAL))   return false;
    if (!mpu_write_reg(MPU_FIFO_EN, MPU_FIFO_EN_VAL))       return false;
    return true;
}

/*
 * PUBLIC API
 */
bool pp_imu_mpu6050_init(void)
{
    s_running = false;
    if (mpu_probe(MPU_I2C_ADDR))
    {
        s_addr = MPU_I2C_ADDR;
    }
    else if (mpu_probe(MPU_I2C_ADDR_ALT))
    {
        s_addr = MPU_I2C_ADDR_ALT;
    }
    else
    {
        return false;
    }
    return mpu_configure();
}

void pp_imu_mpu6050_start(void)
{
    s_running = true;
}

void pp_imu_mpu6050_stop(void)
{
    s_running = false;
    mpu_write_reg(MPU_PWR_MGMT_1, 0x40);  /* sleep */
}

void pp_imu_mpu6050_process(void)
{
    if (!s_running) return;

    /* Check interrupt status and FIFO count */
    uint8_t int_status = 0;
    mpu_read_reg(MPU_INT_STATUS, &int_status);

    uint8_t cnt_buf[2];
    if (!mpu_read_regs(MPU_FIFO_COUNT_H, cnt_buf, 2))
        return;

    uint16_t fifo_bytes   = ((uint16_t)cnt_buf[0] << 8) | cnt_buf[1];
    uint16_t fifo_samples = fifo_bytes / MPU_BYTES_PER_SAMPLE;
    bool     overflowed   = (int_status & MPU_INT_FIFO_OVF) != 0;

    /* Only drain when watermark reached or overflow */
    if (!overflowed && fifo_samples < MPU_FIFO_WM_SAMPLES)
        return;

    if (overflowed)
    {
        /* Reset FIFO on overflow */
        mpu_write_reg(MPU_USER_CTRL, MPU_USER_CTRL_VAL);
        return;
    }

    /* Read samples one at a time (DA14531 I2C RX FIFO constraint) */
    uint16_t to_read = fifo_samples;
    if (to_read > MPU_FIFO_MAX_SAMPLES)
        to_read = MPU_FIFO_MAX_SAMPLES;

    uint16_t i;
    for (i = 0; i < to_read; i++)
    {
        uint8_t buf[MPU_BYTES_PER_SAMPLE];
        if (!mpu_read_regs(MPU_FIFO_R_W, buf, MPU_BYTES_PER_SAMPLE))
            break;

        int16_t sample;
        /* 16-bit big-endian: [HI][LO] per axis */
#if defined(CFG_IMU_AXIS_X)
        sample = (int16_t)((uint16_t)buf[0] << 8 | buf[1]);
#elif defined(CFG_IMU_AXIS_Y)
        sample = (int16_t)((uint16_t)buf[2] << 8 | buf[3]);
#elif defined(CFG_IMU_AXIS_Z)
        sample = (int16_t)((uint16_t)buf[4] << 8 | buf[5]);
#endif
        pp_sample_store_push(sample);
    }
}

bool pp_imu_mpu6050_is_running(void)
{
    return s_running;
}

#endif /* CFG_IMU_MPU6050 */

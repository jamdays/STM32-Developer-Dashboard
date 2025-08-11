#include "lsm6dsl_step.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define INT1_PIN 11
#define INT1_PORT "GPIOD"


#define FUNC_CFG_ACCESS      0x01
#define CTRL1_XL             0x10
#define CTRL10_C             0x19
#define INT1_CTRL            0x0D
#define CONFIG_PEDO_THS_MIN  0x0F
#define STEP_COUNTER_L       0x4B
#define STEP_COUNTER_H       0x4C

#define FUNC_CFG_ACCESS 0x01
#define CTRL1_XL 0x10
#define CTRL10_C 0x19
#define MD1_CFG 0x5E
#define TAP_CFG 0x58
#define TAP_THS_6D 0x59
#define INT_DUR2 0x5a
#define WAKE_UP_THS 0x5b
#define INT1_CTRL 0x0D
#define TAP_SRC 0x1C
#define FUNC_SRC1 0x53
#define FUNC_SRC2 0x54
#define WAKE_UP_SRC 0x1B
#define STEP_COUNTER_L 0x4B
#define STEP_COUNTER_H 0x4C


static int lsm6dsl_write_reg(const struct device *i2c, uint8_t reg, uint8_t val) {
    return i2c_reg_write_byte(i2c, LSM6DSL_I2C_ADDR, reg, val);
}

static int lsm6dsl_read_reg(const struct device *i2c, uint8_t reg, uint8_t *val) {
    return i2c_reg_read_byte(i2c, LSM6DSL_I2C_ADDR, reg, val);
}

int lsm6dsl_init(lsm6dsl_ctx_t *ctx, gpio_callback_handler_t handler)
{
    if (!device_is_ready(ctx->i2c_dev)) {
        printk("I2C device not ready\n");
        return -ENODEV;
    }

    static const struct gpio_dt_spec int1_gpio = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpiod)),
    .pin = INT1_PIN,
    .dt_flags = GPIO_INPUT | GPIO_INT_EDGE_TO_ACTIVE
    };

    int ret = gpio_pin_configure_dt(&int1_gpio, GPIO_INPUT | GPIO_INT_EDGE_RISING);

    static struct gpio_callback cb_data;
    gpio_init_callback(&cb_data, handler, BIT(INT1_PIN));
    gpio_add_callback(int1_gpio.port, &cb_data);
    ret = gpio_pin_interrupt_configure_dt(&int1_gpio, GPIO_INT_EDGE_RISING);
    return ret;
}


int lsm6dsl_enable_step_detection(lsm6dsl_ctx_t *ctx) {
    int ret;

    ret = lsm6dsl_write_reg(ctx->i2c_dev, FUNC_CFG_ACCESS, 0x80);
    k_msleep(10);
    ret |= lsm6dsl_write_reg(ctx->i2c_dev, CONFIG_PEDO_THS_MIN, 0x8E);
    ret |= lsm6dsl_write_reg(ctx->i2c_dev, FUNC_CFG_ACCESS, 0x00);
    k_msleep(10);
    ret |= lsm6dsl_write_reg(ctx->i2c_dev, CTRL1_XL, 0x28);
    ret |= lsm6dsl_write_reg(ctx->i2c_dev, CTRL10_C, 0x14);
    ret |= lsm6dsl_write_reg(ctx->i2c_dev, INT1_CTRL, 0x80);

    return ret;
}

void lsm6dsl_enable_tap_sensor(lsm6dsl_ctx_t *ctx_dev) {
    uint8_t val;
    int32_t ret;

    const struct device *ctx = ctx_dev->i2c_dev;

    ret = lsm6dsl_read_reg(ctx, CTRL1_XL, &val); // CRTL1_XL
    val = 0x60;
    ret = lsm6dsl_write_reg(ctx, CTRL1_XL, &val);
    
    ret = lsm6dsl_read_reg(ctx, TAP_CFG, &val); // TAP_CFG
    val = 0x8e;
    ret = lsm6dsl_write_reg(ctx, TAP_CFG, &val);

    ret = lsm6dsl_read_reg(ctx, TAP_THS_6D, &val); // TAP_THS_6D
    val = 0x8c;
    ret = lsm6dsl_write_reg(ctx, TAP_THS_6D, &val);

    ret = lsm6dsl_read_reg(ctx, INT_DUR2, &val); // INT_DUR2
    val = 0x7f; // Set duration for tap detection
    ret = lsm6dsl_write_reg(ctx, INT_DUR2, &val);

    ret = lsm6dsl_read_reg(ctx, WAKE_UP_THS, &val); // WAKE_UP_THS
    val = 0x80; // Set wake-up threshold
    ret = lsm6dsl_write_reg(ctx, WAKE_UP_THS, &val); // Set wake-up threshold

    ret = lsm6dsl_read_reg(ctx, MD1_CFG, &val);
    val |= 0x08;
    ret = lsm6dsl_write_reg(ctx, MD1_CFG, &val); //MD1_CFG

}


int lsm6dsl_read_step_count(lsm6dsl_ctx_t *ctx, uint16_t *steps) {
    uint8_t low, high;
    int ret = lsm6dsl_read_reg(ctx->i2c_dev, STEP_COUNTER_L, &low);
    ret |= lsm6dsl_read_reg(ctx->i2c_dev, STEP_COUNTER_H, &high);
    if (ret == 0) {
        *steps = ((uint16_t)high << 8) | low;
    }
    return ret;
}

void lsm6dsl_clear_trigger(lsm6dsl_ctx_t *ctx) {
    if (ctx->trigger_flag) {
        *ctx->trigger_flag = false;
    }
}

void example_lsm6dsl_int1_handler(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    ARG_UNUSED(port);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    printk("INT1 triggered (step detected)\n");
    // You should externally set the `trigger_flag` here
    // But since we can't reach the context in ISR directly, user must do this in main
}

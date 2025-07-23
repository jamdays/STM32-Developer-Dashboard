#ifndef LSM6DSL_STEP_H
#define LSM6DSL_STEP_H

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <stdint.h>
#include <stdbool.h>

#define LSM6DSL_I2C_ADDR 0x6A

typedef struct {
    const struct device *i2c_dev;
    volatile bool *trigger_flag;
} lsm6dsl_ctx_t;

int lsm6dsl_init(lsm6dsl_ctx_t *ctx, gpio_callback_handler_t handler);
int lsm6dsl_enable_step_detection(lsm6dsl_ctx_t *ctx);
void lsm6dsl_enable_tap_sensor(lsm6dsl_ctx_t *ctx);
int lsm6dsl_read_step_count(lsm6dsl_ctx_t *ctx, uint16_t *steps);
void lsm6dsl_clear_trigger(lsm6dsl_ctx_t *ctx);
void lsm6dsl_int1_handler(const struct device *port, struct gpio_callback *cb, uint32_t pins);

#endif // LSM6DSL_STEP_H


#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>

#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>

#include <zephyr/shell/shell.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

#include <zephyr/posix/fcntl.h>
#include <zephyr/posix/sys/select.h>
#include <zephyr/net/socket.h>
#include <zephyr/kernel.h>

#include <zephyr/drivers/i2c.h>

#include <zephyr/net/dhcpv4_server.h>

#include "wifi.h"
#include "filesys.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define NUM_SENSORS 6
// INTERRUPTS
#define I2C_NODE    DT_NODELABEL(i2c2)
#define I2C_ADDR 0x6A

#define INT1_PORT   "GPIOD"
#define INT1_PIN 0xb

#define WHO_AM_I_REG 0x0F
#define LSM6DSL_WHO_AM_I_EXPECTED 0x6A

typedef struct
{
    bool why_not;
} stmdev_ctx_t;

// LSM6DSL REGISTERS
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
#define CTRL1_XL 0x10
#define CTRL10_C 0x19

// BANK A
#define CONFIG_PEDO_THS_MIN 0x0F

//FUNC_CFG_ACCESS 
enum {
    FUNC_CFG_EN_B = 0x20,  // Bit 5 set
    FUNC_CFG_EN = 0x80   // Bit 7 set
};

// TAP_CFG
enum {

    LIR = 0x01,  // Bit 0 set
    TAP_Z_EN = 0x02,  // Bit 1 set
    TAP_Y_EN = 0x04,  // Bit 2 set
    TAP_X_EN = 0x08,  // Bit 3 set
    SLOPE_FDS= 0x10,  // Bit 4 set
    INACT_EN0 = 0x20,  // Bit 5 set
    INACT_EN1 = 0x40,  // Bit 6 set
    INTERRUPTS_ENABLE = 0x80   // Bit 7 set
};



// FUNC_SRC1 - 0x53
enum {
    SENSORHUB_END_OP = 0x01,  // Bit 0 set
    SI_END_OP = 0x02,  // Bit 1 set
    HI_FAIL = 0x04,  // Bit 2 set
    STEP_DETECTED = 0x08,  // Bit 3 set
    BIT_5 = 0x10,  // Bit 4 set
    BIT_6 = 0x20,  // Bit 5 set
    BIT_7 = 0x40,  // Bit 6 set
    BIT_8 = 0x80   // Bit 7 set
};

enum sensor_names {
    HTS221,
    LPS22HB,
    LIS3MDL,
    LSM6DSL,
    VL53L0X,
    BUTTON0
};

const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);

volatile int lsm6dsl_mode = 0; // 0 - normal, 1 - step, 2 - tap
volatile int lsm6dsl_action_mode = 0; // 0 - normal, 1 - step, 2 - tap
enum {
    LSM6DSL_MODE_NORMAL = 0,
    LSM6DSL_MODE_STEP = 1,
    LSM6DSL_MODE_TAP = 2
};

enum {
    MODE_FILE = 0,
    MODE_HTTP = 1
};
// INTERRUPTS

// Sensor device nodes
static const struct device *const hts221 = DEVICE_DT_GET_ANY(st_hts221);
static const struct device *const lps22hb = DEVICE_DT_GET_ANY(st_lps22hb_press);
static const struct device *const lis3mdl = DEVICE_DT_GET_ANY(st_lis3mdl_magn);
static const struct device *const lsm6dsl = DEVICE_DT_GET_ANY(st_lsm6dsl);
static const struct device *const vl53l0x = DEVICE_DT_GET_ANY(st_vl53l0x);

// LED and button nodes
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec button0 = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb_data;


// Filesystem
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(cstorage);
static struct fs_mount_t littlefs_mnt = {
    .type = FS_LITTLEFS,
    .fs_data = &cstorage,
    .storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
    .mnt_point = "/lfs"
};

// INTERRUPTS
int32_t lsm6dsl_read_reg(uint8_t reg, uint8_t *data, uint16_t len) {
    return i2c_burst_read(i2c_dev, I2C_ADDR, reg, data, len);
}

int32_t lsm6dsl_write_reg(uint8_t reg, const uint8_t *data, uint16_t len) {
    return i2c_burst_write(i2c_dev, I2C_ADDR, reg, data, len);    
}

static void platform_delay(uint32_t ms) {
    k_msleep(ms);
}

static const struct gpio_dt_spec int1_gpio = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpiod)),
    .pin = INT1_PIN,
    .dt_flags = GPIO_INPUT | GPIO_INT_EDGE_TO_ACTIVE
};

static struct gpio_callback gpio_cb;




// INTERRUPTS

// Sensor Info
enum sensor_type {
    TYPE_DEV,
    TYPE_GPIO
};

struct axes_list {
    enum sensor_channel chan;
    const char *name;
};

struct sensor_info {

    bool dev_or_gpio;
    const struct device *dev;
    const struct gpio_dt_spec *gpio;
    const char *name;
    struct k_timer timer;
    struct k_timer http_timer;
    void * timer_callback;
    void * http_timer_callback;
    const char *cb_filename;
    const char *interrupt_cb_filename;
    int num_axes;
    struct axes_info *axes;
    struct k_work work; // File client
    struct k_work http_work; // For HTTP client
    struct k_work interrupt_work; // For interrupt file client 
    struct k_work interrupt_http_work; // For interrupt HTTP client
    const char * url; // For timer HTTP client
    const char * interrupt_url; // For interrupt HTTP client
};

struct sensor_save_work {
    struct k_work work;
    char sensor_name[32]; // Sensor name
    char file_name[64]; // File name to save data
};



static struct axes_list hts221_axes[] = {
    { .chan = SENSOR_CHAN_AMBIENT_TEMP, .name = "temperature" },
    { .chan = SENSOR_CHAN_HUMIDITY, .name = "humidity" }
};

static struct axes_list lps22hb_axes[] = {
    { .chan = SENSOR_CHAN_PRESS, .name = "pressure" }
};

static struct axes_list lis3mdl_axes[] = {
    { .chan = SENSOR_CHAN_MAGN_X, .name = "magnetic_x" },
    { .chan = SENSOR_CHAN_MAGN_Y, .name = "magnetic_y" },
    { .chan = SENSOR_CHAN_MAGN_Z, .name = "magnetic_z" }
};

static struct axes_list lsm6dsl_axes[] = {
    { .chan = SENSOR_CHAN_ACCEL_X, .name = "accel_x" },
    { .chan = SENSOR_CHAN_ACCEL_Y, .name = "accel_y" },
    { .chan = SENSOR_CHAN_ACCEL_Z, .name = "accel_z" },
    { .chan = SENSOR_CHAN_GYRO_X, .name = "gyro_x" },
    { .chan = SENSOR_CHAN_GYRO_Y, .name = "gyro_y" },
    { .chan = SENSOR_CHAN_GYRO_Z, .name = "gyro_z" }
};

static struct axes_list vl53l0x_axes[] = {
    { .chan = SENSOR_CHAN_PROX, .name = "proximity" }
};

void sensor_timer_callback(struct k_timer *timer_id);

// HTTP Client
void sensor_timer_http_callback(struct k_timer *timer_id);

struct sensor_info sensors[NUM_SENSORS] = {
    {
        .dev_or_gpio = TYPE_DEV,
        .dev = hts221,
        .gpio = NULL,
        .name = "hts221",
        .timer_callback = sensor_timer_callback,
        .http_timer_callback = sensor_timer_http_callback,
        .cb_filename = NULL,
        .num_axes = 2,
        .axes = hts221_axes
    },
    {
        .dev_or_gpio = TYPE_DEV,
        .dev = lps22hb,
        .gpio = NULL,
        .name = "lps22hb",
        .timer_callback = sensor_timer_callback,
        .http_timer_callback = sensor_timer_http_callback,
        .cb_filename = NULL,
        .num_axes = 1,
        .axes = lps22hb_axes
    },
    {
        .dev_or_gpio = TYPE_DEV,
        .dev = lis3mdl,
        .gpio = NULL,
        .name = "lis3mdl",
        .timer_callback = sensor_timer_callback,
        .http_timer_callback = sensor_timer_http_callback,
        .cb_filename = NULL,
        .num_axes = 3,
        .axes = lis3mdl_axes
    },
    {
        .dev_or_gpio = TYPE_DEV,
        .dev = lsm6dsl,
        .gpio = NULL,
        .name = "lsm6dsl",
        .timer_callback = sensor_timer_callback,
        .http_timer_callback = sensor_timer_http_callback,
        .cb_filename = NULL,
        .num_axes = 6,
        .axes = lsm6dsl_axes
    },
    {
        .dev_or_gpio = TYPE_DEV,
        .dev = vl53l0x,
        .gpio = NULL,
        .name = "vl53l0x",
        .timer_callback = sensor_timer_callback,
        .http_timer_callback = sensor_timer_http_callback,
        .cb_filename = NULL

    },
    {
        .dev_or_gpio = TYPE_GPIO,
        .dev = NULL, 
        .gpio = &button0,
        .name = "button0",
        .timer_callback = sensor_timer_callback,
        .http_timer_callback = sensor_timer_http_callback,
        .cb_filename = NULL
    }
};

int get_sensor_index(char *sensor_name) {
    for (int i = 0; i < NUM_SENSORS; i++) {
        if (strcmp(sensors[i].name, sensor_name) == 0) {
            return i;
        }
    }
    return -1; 
}

static void http_client_work_handler(struct k_work *work);

void int1_handler(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    //printk("INT1 triggered (step or free fall)\n");
    switch (lsm6dsl_mode) {
        case LSM6DSL_MODE_STEP:
            uint8_t step_count_l, step_count_h;
            lsm6dsl_read_reg(STEP_COUNTER_L, &step_count_l, 1);
            lsm6dsl_read_reg(STEP_COUNTER_H, &step_count_h, 1);
            uint16_t step_count = (step_count_h << 8) | step_count_l;
            printk("Step count: %d\n", step_count);
            break;

        case LSM6DSL_MODE_TAP:
            switch (lsm6dsl_action_mode) {
                case MODE_FILE: {
                    // Write tap data to file
                    struct sensor_info *sensor = &sensors[LSM6DSL];
                    k_work_submit(&sensor->interrupt_work);
                    printk("Tap data written to file\n");
                }
                break;

                case MODE_HTTP: {
                    // Send tap data via HTTP
                    struct sensor_info *sensor = &sensors[LSM6DSL];
                    k_work_submit(&sensor->interrupt_http_work);
                    printk("Tap data written to file\n");
                }
                break;

                default:
                    printk("No action for current mode\n");
            }
            break;

        default:
            //printk("No action for current mode\n");
    }
}


// Sensor Reading command
static int cmd_read_sensor(const struct shell *shell, size_t argc, char **argv);

// EXPERIMENTAL
#ifdef CONFIG_HTTP_SERVER
void http_server_thread_orig(void)
{
    printk("HTTP server thread started\n");
    // Basic HTTP server implementation
    while(1) {
        k_msleep(1000); // Wait for init event
        if (wifi_is_ready) {
            break;
        }
    }

    int serv_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8081);
    //addr.sin_addr.s_addr = inet_addr("192.168.3.35");
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // Listen on all interfaces

    bind(serv_sock, (struct sockaddr *)&addr, sizeof(addr));
    listen(serv_sock, 2);

    while (1) {
        int client_sock = accept(serv_sock, NULL, NULL);
        if (client_sock < 0) {
            k_sleep(K_SECONDS(1)); // Wait before retrying
            continue;
        }

        char buf[256];
        int len = recv(client_sock, buf, sizeof(buf) - 1, 0);
        buf[len] = 0;

        // Simple request parsing
        if (strstr(buf, "GET /sensors/hts221")) {
            struct sensor_value temp, hum;
            sensor_sample_fetch(hts221);
            sensor_channel_get(hts221, SENSOR_CHAN_AMBIENT_TEMP, &temp);
            sensor_channel_get(hts221, SENSOR_CHAN_HUMIDITY, &hum);
            snprintf(buf, sizeof(buf), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"temp\":%d.%06d, \"humidity\":%d.%06d}", temp.val1, temp.val2, hum.val1, hum.val2);
        } else {
            snprintf(buf, sizeof(buf), "HTTP/1.1 404 Not Found\r\n\r\n");
        }

        send(client_sock, buf, strlen(buf), 0);
        close(client_sock);
    }
}
#endif

// Toggle LED 1 command
static void cmd_toggle_led1 (const struct shell *shell, size_t argc, char **argv)
{
    if (gpio_pin_get_dt(&led1) > 0) {
        gpio_pin_set_dt(&led1, 0);
        shell_print(shell, "LED0 turned OFF");
    } else {
        gpio_pin_set_dt(&led1, 1);
        shell_print(shell, "LED0 turned ON");
    }
}

// Work Handlers
void http_client_work_handler(struct k_work *work) {
    struct sensor_info *sensor = CONTAINER_OF(work, struct sensor_info, http_work);

    char buf[128];           
    struct http_request req;
    //static uint8_t recv_buf[512];
    int sock;
    struct addrinfo *res;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_flags = AI_NUMERICHOST,
    };
    printk("http_client_work_handler for sensor at %p, name = %s, url = %s\n", sensor, sensor->name, sensor->url);
    int ret = sensor_reading(sensor->name, buf, sizeof(buf));
    if (ret < 0) {
        printk("Sensor read failed: %d\n", ret);
        return;
    }

    // Make sure the result is a full line
    size_t len = strlen(buf);
    if (len < sizeof(buf) - 1) {
        buf[len] = '\0';
    }
    char _url[128];
    strncpy(_url, sensor->url, sizeof(_url) - 1);
    _url[sizeof(_url) - 1] = '\0';

    // Basic URL parsing
    char *host = _url;
    char *path = strchr(host, '/');
    if (path) {
        *path = 0;
        path++;
    } else {
        path = "";
    }

    if (getaddrinfo(host, "80", &hints, &res) != 0) {
        printk("Failed to resolve hostname: %s\n", host);
        return;
    }
    
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    connect(sock, (struct sockaddr *)res->ai_addr, sizeof(res->ai_addr));
    char _buf[256]; // Buffer for the HTTP request
    char * msg_template = "POST /%s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%s";
    snprintf(_buf, sizeof(_buf), msg_template, path, host, len, buf);
    write(sock, _buf, strlen(_buf));
    close(sock);
    freeaddrinfo(res);
}

void sensor_work_handler(struct k_work *work) {

    struct sensor_info *sensor = CONTAINER_OF(work, struct sensor_info, work);

    char buf[128];           // Larger buffer to ensure full string fits
    char full_path[128];

    int ret = sensor_reading(sensor->name, buf, sizeof(buf));
    if (ret < 0) {
        printk("Sensor read failed: %d\n", ret);
        return;
    }

    // Make sure the result is a full line
    size_t len = strlen(buf);
    if (len < sizeof(buf) - 1) {
        buf[len] = '\0';
    }

    snprintf(full_path, sizeof(full_path), "/lfs/%s", sensor->cb_filename);

    struct fs_file_t file;
    fs_file_t_init(&file);
    ret = fs_open(&file, full_path, FS_O_CREATE | FS_O_APPEND);
    if (ret < 0) {
        return;
    }

    ret = fs_write(&file, buf, len);
    if (ret < 0) {
    }
    printk("Writing to file %s->%s<-END\n", full_path, buf);
    fs_close(&file);
}

void interrupt_work_handler(struct k_work *work) {
    //struct sensor_info *sensor = CONTAINER_OF(work, struct sensor_info, interrupt_work);
    struct sensor_info *sensor = &sensors[LSM6DSL];
    printk("Interrupt work handler for sensor %s\n", sensor->name);
    char full_path[128];
    snprintf(full_path, sizeof(full_path), "/lfs/%s", sensor->interrupt_cb_filename);
    struct fs_file_t file;
    fs_file_t_init(&file);
    int ret = fs_open(&file, full_path, FS_O_CREATE | FS_O_APPEND);
    if (ret < 0) {
        printk("Failed to open file %s: %d\n", full_path, ret);
        return;
    }
    // Write the interrupt data to the file
    char buf[128];
    ret = sensor_reading(sensor->name, buf, sizeof(buf));
    if (ret < 0) {
        printk("Sensor read failed: %d\n", ret);
        fs_close(&file);
        return;
    }
    // Make sure the result is a full line
    size_t len = strlen(buf);
    if (len < sizeof(buf) - 1) {
        buf[len] = '\0';
    }
    ret = fs_write(&file, buf, len);
    if (ret < 0) {
        printk("Failed to write to file %s: %d\n", full_path, ret);
        fs_close(&file);
        return;
    }
    printk("Sensor %s interrupt data: %s\n", sensor->name, buf);
    fs_close(&file);
}

void interrupt_http_work_handler(struct k_work *work) {
    //struct sensor_info *sensor = CONTAINER_OF(work, struct sensor_info, interrupt_work);
    struct sensor_info *sensor = &sensors[LSM6DSL];
    printk("Interrupt http work handler for sensor %s\n", sensor->name);

    char buf[128];           
    struct http_request req;
    //static uint8_t recv_buf[512];
    int sock;
    struct addrinfo *res;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_flags = AI_NUMERICHOST,
    };
    printk("http_client_work_handler for sensor at %p, name = %s, url = %s\n", sensor, sensor->name, sensor->url);
    int ret = sensor_reading(sensor->name, buf, sizeof(buf));
    if (ret < 0) {
        printk("Sensor read failed: %d\n", ret);
        return;
    }

    // Make sure the result is a full line
    size_t len = strlen(buf);
    if (len < sizeof(buf) - 1) {
        buf[len] = '\0';
    }
    char _url[128];
    strncpy(_url, sensor->interrupt_url, sizeof(_url) - 1);
    _url[sizeof(_url) - 1] = '\0';

    // Basic URL parsing
    char *host = _url;
    char *path = strchr(host, '/');
    if (path) {
        *path = 0;
        path++;
    } else {
        path = "";
    }

    if (getaddrinfo(host, "80", &hints, &res) != 0) {
        printk("Failed to resolve hostname: %s\n", host);
        return;
    }
    
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    connect(sock, (struct sockaddr *)res->ai_addr, sizeof(res->ai_addr));
    char _buf[256]; // Buffer for the HTTP request
    char * msg_template = "POST /%s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\nTAP EVENT:\r\n%s";
    snprintf(_buf, sizeof(_buf), msg_template, path, host, len, buf);
    write(sock, _buf, strlen(_buf));
    close(sock);
    freeaddrinfo(res);

    
}

// Timer Callbacks
void sensor_timer_callback(struct k_timer *timer_id) {
    struct sensor_info *sensor = CONTAINER_OF(timer_id, struct sensor_info, timer);
    k_work_submit(&sensor->work);
}

void sensor_timer_http_callback(struct k_timer *timer_id) {
    struct sensor_info *sensor = CONTAINER_OF(timer_id, struct sensor_info, http_timer);
    k_work_submit(&sensor->http_work);
}

// Sensor Timer HTTP Stop Command
static void cmd_sensor_timer_http_stop (const struct shell *shell, size_t argc, char **argv) {
    if (argc < 2) {
        shell_error(shell, "Usage: sensor_timer_stop <sensor_name>");
        return;
    }
    const char *sensor_name = argv[1];
    int sensor_index = get_sensor_index(sensor_name);
    struct sensor_info *sensor = &sensors[sensor_index];

    k_timer_stop(&(sensor->http_timer));
    shell_print(shell, "Stopped timer for %s", sensor_name);
}

// Sensor Timer HTTP Start Command
static void cmd_sensor_timer_http_start (const struct shell *shell, size_t argc, char **argv){
    if (argc < 3) {
        shell_error(shell, "Usage: sensor_timer_cb <sensor_name> <url> <timing>");
        return;
    }
    const char *sensor_name = argv[1];
    const char * url = argv[2];
    const char * timing_str = argv[3]; 
    int time = atoi(timing_str);
    int sensor_index = get_sensor_index(sensor_name);
    struct sensor_info *sensor = &sensors[sensor_index];
    void * cb;
    
    sensor->url = url;
    k_timer_init(&(sensor->http_timer), sensor->http_timer_callback, NULL);
    k_timer_start(&(sensor->http_timer), K_SECONDS(time), K_SECONDS(time));
}

// Sensor Timer Start Command
static void cmd_sensor_timer_start(const struct shell *shell, size_t argc, char **argv){
    if (argc < 3) {
        shell_error(shell, "Usage: sensor_timer <sensor_name> <file_name> <timing>");
        return;
    }
    const char *sensor_name = argv[1];
    const char * file_name = argv[2];
    const char * timing_str = argv[3]; 
    int time = atoi(timing_str);
    int sensor_index = get_sensor_index(sensor_name);
    struct sensor_info *sensor = &sensors[sensor_index];
    void * cb;
    
    sensor->cb_filename = file_name;
    k_timer_init(&(sensor->timer), sensor->timer_callback, NULL);
    k_timer_start(&(sensor->timer), K_SECONDS(time), K_SECONDS(time));
}

void enable_tap_sensor() {
    uint8_t val;
    int32_t ret;
    
    ret = lsm6dsl_read_reg(CTRL1_XL, &val, 1); // CRTL1_XL
    printk("CTRL1_XL: %02x\n", val);
    val = 0x60;
    ret = lsm6dsl_write_reg(CTRL1_XL, &val, 1);
    
    ret = lsm6dsl_read_reg(TAP_CFG, &val, 1); // TAP_CFG
    printk("TAP_CFG: %02x\n", val);
    val = 0x8e;
    ret = lsm6dsl_write_reg(TAP_CFG, &val, 1);

    ret = lsm6dsl_read_reg(TAP_THS_6D, &val, 1); // TAP_THS_6D
    printk("TAP_THS_6D: %02x\n", val); 
    val = 0x8c;
    ret = lsm6dsl_write_reg(TAP_THS_6D, &val, 1);

    ret = lsm6dsl_read_reg(INT_DUR2, &val, 1); // INT_DUR2
    printk("INT_DUR2: %02x\n", val);
    val = 0x7f; // Set duration for tap detection
    ret = lsm6dsl_write_reg(INT_DUR2, &val, 1);

    ret = lsm6dsl_read_reg(WAKE_UP_THS, &val, 1); // WAKE_UP_THS
    printk("WAKE_UP_THS: %02x\n", val);
    val = 0x80; // Set wake-up threshold
    ret = lsm6dsl_write_reg(WAKE_UP_THS, &val, 1); // Set wake-up threshold

    ret = lsm6dsl_read_reg(MD1_CFG, &val, 1);
    printk("MD1_CFG: %02x\n", val);
    val = 0x08;
    ret = lsm6dsl_write_reg(MD1_CFG, &val, 1); //MD1_CFG
    
    ret = lsm6dsl_read_reg(INT1_CTRL, &val, 1);
    printk("INT1_CTRL: %02x\n", val);
    val = 0x80; // Set wake-up threshold
    ret = lsm6dsl_write_reg(INT1_CTRL, &val, 1); 
}

void enable_single_tap_sensor() {
    uint8_t val;
    int32_t ret;
    
    ret = lsm6dsl_read_reg(CTRL1_XL, &val, 1); // CRTL1_XL
    printk("CTRL1_XL: %02x\n", val);
    val = 0x60;
    ret = lsm6dsl_write_reg(CTRL1_XL, &val, 1);
    
    ret = lsm6dsl_read_reg(TAP_CFG, &val, 1); // TAP_CFG
    printk("TAP_CFG: %02x\n", val);
    val = 0x8e;
    ret = lsm6dsl_write_reg(TAP_CFG, &val, 1);

    ret = lsm6dsl_read_reg(TAP_THS_6D, &val, 1); // TAP_THS_6D
    printk("TAP_THS_6D: %02x\n", val); 
    val = 0x89;
    ret = lsm6dsl_write_reg(TAP_THS_6D, &val, 1);

    ret = lsm6dsl_read_reg(INT_DUR2, &val, 1); // INT_DUR2
    printk("INT_DUR2: %02x\n", val);
    val = 0x06; // Set duration for tap detection
    ret = lsm6dsl_write_reg(INT_DUR2, &val, 1);

    ret = lsm6dsl_read_reg(WAKE_UP_THS, &val, 1); // WAKE_UP_THS
    printk("WAKE_UP_THS: %02x\n", val);
    val = 0x00; // Set wake-up threshold
    ret = lsm6dsl_write_reg(WAKE_UP_THS, &val, 1); // Set wake-up threshold

    ret = lsm6dsl_read_reg(MD1_CFG, &val, 1);
    printk("MD1_CFG: %02x\n", val);
    val = 0x40;
    ret = lsm6dsl_write_reg(MD1_CFG, &val, 1); //MD1_CFG

        ret = lsm6dsl_read_reg(INT1_CTRL, &val, 1);
    printk("INT1_CTRL: %02x\n", val);
    val = 0x80; // Set wake-up threshold
    ret = lsm6dsl_write_reg(INT1_CTRL, &val, 1); 
}

void enable_step_sensor() {
    uint8_t val;
    int32_t ret;

    ret = lsm6dsl_read_reg(FUNC_CFG_ACCESS, &val, 1); // CRTL1_XL
    printk("CTRL1_XL: %02x\n", val);
    val = 0x80; // Set accelerometer to 104 Hz
    ret = lsm6dsl_write_reg(FUNC_CFG_ACCESS, &val, 1);

    ret = lsm6dsl_read_reg(CONFIG_PEDO_THS_MIN, &val, 1); // CRTL1_XL
    printk("CONFIG_PEDO_THS_MIN: %02x\n", val);
    val = 0x8E; // Set accelerometer to 104 Hz
    ret = lsm6dsl_write_reg(CONFIG_PEDO_THS_MIN, &val, 1);

    ret = lsm6dsl_read_reg(FUNC_CFG_ACCESS, &val, 1); // CRTL1_XL
    printk("FUNC_CFG_ACCESS: %02x\n", val);
    val = 0x00; // Set accelerometer to 104 Hz
    ret = lsm6dsl_write_reg(FUNC_CFG_ACCESS, &val, 1);

    ret = lsm6dsl_read_reg(CTRL1_XL, &val, 1); // CRTL1_XL
    printk("CTRL1_XL: %02x\n", val);
    val = 0x28; // Set accelerometer to 104 Hz
    ret = lsm6dsl_write_reg(CTRL1_XL, &val, 1);

    ret = lsm6dsl_read_reg(CTRL10_C, &val, 1); // CRTL1_XL
    printk("CTRL10_C: %02x\n", val);
    val = 0x14; // Set accelerometer to 104 Hz
    ret = lsm6dsl_write_reg(CTRL10_C, &val, 1);

    ret = lsm6dsl_read_reg(INT1_CTRL, &val, 1); // CRTL1_XL
    printk("INT1_CTRL: %02x\n", val);
    val = 0x80; // Set accelerometer to 104 Hz
    ret = lsm6dsl_write_reg(INT1_CTRL, &val, 1);
}

static void cmd_lsm6dsl_tap_http_start(const struct shell *shell, size_t argc, char **argv) {
    if (argc < 2) {
        shell_error(shell, "Usage: lsm6dsl_tap_http_start <url>");
        return;
    }
    sensors[LSM6DSL].interrupt_url = strdup(argv[1]);

    enable_single_tap_sensor();
    lsm6dsl_mode = LSM6DSL_MODE_TAP;
    lsm6dsl_action_mode = MODE_HTTP;

    shell_print(shell, "Started LSM6DSL tap detection with HTTP mode");
}

static void cmd_lsm6dsl_step_http_start(const struct shell *shell, size_t argc, char **argv) {
    if (argc < 2) {
        shell_error(shell, "Usage: lsm6dsl_tap_http_start <url>");
        return;
    }

    sensors[LSM6DSL].interrupt_url = strdup(argv[1]);

    enable_step_sensor();
    lsm6dsl_mode = LSM6DSL_MODE_STEP;
    lsm6dsl_action_mode = MODE_HTTP;

    shell_print(shell, "Started LSM6DSL tap detection with HTTP mode");
}

static void cmd_lsm6dsl_tap_start(const struct shell *shell, size_t argc, char **argv) {
    if (argc < 2) {
        shell_error(shell, "Usage: lsm6dsl_step_start <filename>");
        return;
    }
    sensors[LSM6DSL].interrupt_cb_filename = argv[1];

    enable_single_tap_sensor();
    lsm6dsl_mode = LSM6DSL_MODE_TAP;
    lsm6dsl_action_mode = MODE_FILE;
}

static void cmd_lsm6dsl_step_start(const struct shell *shell, size_t argc, char **argv) {
   if (argc < 2) {
        shell_error(shell, "Usage: lsm6dsl_step_start <filename>");
        return;
    }
    sensors[LSM6DSL].interrupt_cb_filename = argv[1];

    enable_step_sensor();
    lsm6dsl_mode = LSM6DSL_MODE_STEP;
    lsm6dsl_action_mode = MODE_FILE;    
}

static void cmd_lsm6dsl_step_stop(const struct shell *shell, size_t argc, char **argv) {
    // Stop the LSM6DSL step detection timer
    //k_timer_stop(&sensors[LSM6DSL].timer);
    shell_print(shell, "Stopped LSM6DSL step detection");
}

// Sensor Timer Stop Command
static void cmd_sensor_timer_stop (const struct shell *shell, size_t argc, char **argv) {
    if (argc < 2) {
        shell_error(shell, "Usage: sensor_timer_stop <sensor_name>");
        return;
    }
    const char *sensor_name = argv[1];
    int sensor_index = get_sensor_index(sensor_name);
    struct sensor_info *sensor = &sensors[sensor_index];

    k_timer_stop(&(sensor->timer));
    shell_print(shell, "Stopped timer for %s", sensor_name);
}

// Sensor Reading (Returns formatted string of sensor data)
// TODO we should use the sensors struct instead of hardcoding
int sensor_reading(const char *sensor_name, char *buf, size_t buf_len)
{
    struct sensor_value val[3];
    int rc, used = 0;

    if (!sensor_name || !buf || buf_len == 0) {
        return -EINVAL;
    }
    buf[0] = '\0';

    if (strcmp(sensor_name, "hts221") == 0) {
        if ((rc = sensor_sample_fetch(hts221)) != 0) {
            return rc;
        }
        sensor_channel_get(hts221, SENSOR_CHAN_AMBIENT_TEMP, &val[0]);
        sensor_channel_get(hts221, SENSOR_CHAN_HUMIDITY,     &val[1]);

        used = snprintf(buf, buf_len,
                        "HTS221: Temp %d.%06d C, Hum %d.%06d %%\n",
                        val[0].val1, val[0].val2,
                        val[1].val1, val[1].val2);
    }

    else if (strcmp(sensor_name, "lps22hb") == 0) {
        if ((rc = sensor_sample_fetch(lps22hb)) != 0) {
            return rc;
        }
        sensor_channel_get(lps22hb, SENSOR_CHAN_PRESS, &val[0]);

        used = snprintf(buf, buf_len,
                        "LPS22HB: Pressure %d.%06d kPa\n",
                        val[0].val1, val[0].val2);
    }

    else if (strcmp(sensor_name, "lis3mdl") == 0) {
        if ((rc = sensor_sample_fetch(lis3mdl)) != 0) {
            return rc;
        }
        sensor_channel_get(lis3mdl, SENSOR_CHAN_MAGN_XYZ, val);

        used = snprintf(buf, buf_len,
                        "LIS3MDL: X %d.%06d, Y %d.%06d, Z %d.%06d uT\n",
                        val[0].val1, val[0].val2,
                        val[1].val1, val[1].val2,
                        val[2].val1, val[2].val2);
    }

    else if (strcmp(sensor_name, "lsm6dsl") == 0) {
        if ((rc = sensor_sample_fetch(lsm6dsl)) != 0) {
            return rc;
        }

        /* Accel */
        /*sensor_channel_get(lsm6dsl, SENSOR_CHAN_ACCEL_XYZ, val);
        used = snprintf(buf, buf_len,
                        //"LSM6DSL Accel: X %d.%06d, Y %d.%06d, Z %d.%06d m/s^2\n",
                        "LSM6DSL Accel: X %d.%d, Y %d.%d, Z %d.%d m/s^2\n",
                        val[0].val1, val[0].val2,
                        val[1].val1, val[1].val2,
                        val[2].val1, val[2].val2);

        if ((size_t)used < buf_len) {
            sensor_channel_get(lsm6dsl, SENSOR_CHAN_GYRO_XYZ, val);
            used += snprintf(buf + used, buf_len - used,
                             "LSM6DSL Gyro: X %d.%06d, Y %d.%06d, Z %d.%06d deg/s\n",
                             val[0].val1, val[0].val2,
                             val[1].val1, val[1].val2,
                             val[2].val1, val[2].val2);
        }*/
       static struct sensor_value accel_x, accel_y, accel_z;
       static struct sensor_value gyro_x, gyro_y, gyro_z;
       sensor_sample_fetch_chan(lsm6dsl, SENSOR_CHAN_ACCEL_XYZ);
	    sensor_channel_get(lsm6dsl, SENSOR_CHAN_ACCEL_X, &accel_x);
	    sensor_channel_get(lsm6dsl, SENSOR_CHAN_ACCEL_Y, &accel_y);
	    sensor_channel_get(lsm6dsl, SENSOR_CHAN_ACCEL_Z, &accel_z);

	/* lsm6dsl gyro */
	    sensor_sample_fetch_chan(lsm6dsl, SENSOR_CHAN_GYRO_XYZ);
	    sensor_channel_get(lsm6dsl, SENSOR_CHAN_GYRO_X, &gyro_x);
	    sensor_channel_get(lsm6dsl, SENSOR_CHAN_GYRO_Y, &gyro_y);
	    sensor_channel_get(lsm6dsl, SENSOR_CHAN_GYRO_Z, &gyro_z);

       used = sprintf(buf, "accel x:%f,y:%f,z:%f\n gyro x:%f,y:%f,z:%f",
							  sensor_value_to_float(&accel_x),
							  sensor_value_to_float(&accel_y),
							  sensor_value_to_float(&accel_z),
                              sensor_value_to_float(&gyro_x),
							   sensor_value_to_float(&gyro_y),
							   sensor_value_to_float(&gyro_z));

    }

    else if (strcmp(sensor_name, "vl53l0x") == 0) {
        if ((rc = sensor_sample_fetch(vl53l0x)) != 0) {
            return rc;
        }
        sensor_channel_get(vl53l0x, SENSOR_CHAN_DISTANCE, &val[0]);

        used = snprintf(buf, buf_len,
                        "VL53L0X: Raw Distance %d\n",
                        val[0].val2);
    }

    else if (strcmp(sensor_name, "button0") == 0) {
        int state = gpio_pin_get_dt(&button0);
        if (state < 0) {
            return -EIO;
        }
        used = snprintf(buf, buf_len,
                        "Button %s\n", state ? "pressed" : "released");
    }

    else {
        used = snprintf(buf, buf_len,
                        "Unknown sensor: %s\n", sensor_name);
    }

    buf[buf_len - 1] = '\0';
    if (used >= (int)buf_len) {
        return -ENOSPC;
    }

    return used;
}

// Use sensor_reading to read a sensor and print the result
static int cmd_read_sensor(const struct shell *shell, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(shell, "Usage: read <sensor_name>");
        return -EINVAL;
    }
    const char *sensor_name = argv[1];
    char reading[256];
    int rc = sensor_reading(sensor_name, reading, sizeof(reading));
    if (rc < 0) {
        if (rc == -ENOSPC) {
            shell_error(shell, "Output truncated (buffer too small)");
        } else {
            shell_error(shell, "Failed to read %s (err %d)", sensor_name, rc);
        }
        return rc;
    }
    shell_print(shell, "%s", reading);
    return 0;
}

SHELL_CMD_REGISTER(read, NULL, "Read sensor data", cmd_read_sensor);
SHELL_CMD_REGISTER(toggle_led1, NULL, "Toggle LED1", cmd_toggle_led1);

SHELL_CMD_REGISTER(sensor_timer_start, NULL, "Start sensor timer", cmd_sensor_timer_start);
SHELL_CMD_REGISTER(sensor_timer_stop, NULL, "Stop sensor timer", cmd_sensor_timer_stop);

SHELL_CMD_REGISTER(sensor_timer_http_start, NULL, "Start sensor HTTP timer", cmd_sensor_timer_http_start);
SHELL_CMD_REGISTER(sensor_timer_http_stop, NULL, "Stop sensor HTTP timer", cmd_sensor_timer_http_stop);

SHELL_CMD_REGISTER(lsm6dsl_step_start, NULL, "Start LSM6DSL event handler", cmd_lsm6dsl_step_start);
SHELL_CMD_REGISTER(lsm6dsl_tap_start, NULL, "Start LSM6DSL event handler", cmd_lsm6dsl_tap_start);
SHELL_CMD_REGISTER(lsm6dsl_tap_http_start, NULL, "Start LSM6DSL event handler", cmd_lsm6dsl_tap_http_start);
SHELL_CMD_REGISTER(lsm6dsl_step_stop, NULL, "Stop LSM6DSL event handler", cmd_lsm6dsl_step_stop);

void init_sensors() {
    for (int i = 0; i < NUM_SENSORS; i++) {
        if (sensors[i].dev_or_gpio == TYPE_DEV) {
            if (!device_is_ready(sensors[i].dev)) {
                printk("Sensor %s is not ready\n", sensors[i].name);
            } else {
                printk("Sensor %s initialized\n", sensors[i].name);
            }
        } else if (sensors[i].dev_or_gpio == TYPE_GPIO) {
            if (!gpio_is_ready_dt(sensors[i].gpio)) {
                printk("GPIO %s is not ready\n", sensors[i].name);
            } else {
                printk("GPIO %s initialized\n", sensors[i].name);
            }
        }

        sensors[i].timer_callback = sensor_timer_callback;
        k_timer_init(&sensors[i].timer, sensors[i].timer_callback, NULL);
        k_work_init(&sensors[i].work, sensor_work_handler);
        k_work_init(&sensors[i].http_work, http_client_work_handler);
        k_work_init(&sensors[i].interrupt_work, interrupt_work_handler);
        k_work_init(&sensors[i].interrupt_http_work, interrupt_http_work_handler);
        sensors[i].cb_filename = k_malloc(64);
        sensors[i].url = k_malloc(128);
        struct sensor_value odr_attr;
        odr_attr.val1 = 104; // Set ODR to 100 Hz
        odr_attr.val2 = 0;

        if (sensor_attr_set(lsm6dsl, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr) < 0) {
            printk("Failed to set LSM6DSL ODR\n");
        }
        if (sensor_attr_set(lsm6dsl, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr) < 0) {
            printk("Failed to set LSM6DSL Gyro ODR\n");
        }
    }
}

#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_config.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>


void self_assign_ip() {
	int idx = 1;
	struct in_addr addr = {0};
	struct net_if *iface = net_if_get_default();
    const char * ip_str = "192.168.1.1";
	net_addr_pton(AF_INET, ip_str, &addr);

	struct net_if_addr *ifaddr;
	struct in_addr netmask;
    const char * nmask_str = "255.255.255.0";

	ifaddr = net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);
	net_addr_pton(AF_INET, nmask_str, &netmask);

	net_if_ipv4_set_netmask_by_addr(iface, &addr, &netmask);
	return;
}


static int cmd_wifi_ap(const struct shell *shell, size_t argc, char **argv)
{
    struct net_if *iface = net_if_get_default();
    self_assign_ip();

    // 1. Set up the AP parameters
    struct wifi_connect_req_params ap_params = {
        .ssid = "ADAM",
        .ssid_length = strlen("ADAM"),
        .psk = "ADAM1234",
        .psk_length = strlen("ADAM1234"),
        .channel = 6,
        .security = WIFI_SECURITY_TYPE_PSK,
        .band = WIFI_FREQ_BAND_2_4_GHZ,
    };

    // 2. Start the AP
    int ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &ap_params, sizeof(ap_params));
    if (ret) {
        shell_error(shell, "Failed to start AP: %d", ret);
        return ret;
    }
    shell_print(shell, "Wi-Fi AP started: SSID=%s, password=%s", ap_params.ssid, ap_params.psk);

    // // 3. Assign static IP address
    struct in_addr ipaddr, netmask, gw;

    net_addr_pton(AF_INET, "192.168.1.1", &ipaddr); 
    // ||
    //     !net_addr_pton(AF_INET, "255.255.255.0", &netmask) ||
    //     !net_addr_pton(AF_INET, "192.168.1.1", &gw)) {
    //     shell_error(shell, "Failed to parse static IP settings");
    //     return -EINVAL;
    // }

    // net_if_ipv4_addr_add(iface, &ipaddr, NET_ADDR_MANUAL, 0);
    // net_if_ipv4_set_netmask(iface, &netmask);
    // net_if_ipv4_set_gw(iface, &gw);

    // shell_print(shell, "Static IP set: 192.168.1.1/24");

    // 4. Start DHCP server
    ret = net_dhcpv4_server_start(iface, &ipaddr);
    if (ret) {
        shell_error(shell, "Failed to start DHCP server: %d", ret);
        return ret;
    }
    shell_print(shell, "DHCP server started");

    return 0;
}

SHELL_CMD_REGISTER(wifi_ap, NULL,
    "Start Wi-Fi AP with SSID=ADAM and password=ADAM1234",
    cmd_wifi_ap);



void main(void)
{
    printk("Starting application\n");

    // Initialize LEDs and Button
    printk("Initializing LEDs and button...\n");
    gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&button0, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&button0, GPIO_INT_EDGE_TO_ACTIVE);
    
    printk("LEDs and button initialized\n");

    // Initialize Filesystem
    printk("Initializing filesystem...\n");
    int rc = fs_mount(&littlefs_mnt);
    if (rc < 0) {
        printk("Failed to mount littlefs: %d\n", rc);
    }
    printk("Filesystem initialized\n");
    
    // Initialize WiFi
    printk("Initializing WiFi...\n");
    net_mgmt_init_event_callback(&wifi_cb, wifi_mgmt_event_handler, NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&wifi_cb);
    printk("WiFi initialized\n");
    wifi_connect_to_saved_network();

    // Initialize Sensors and Triggers
    init_sensors();

    // INTERRUPTS
    int ret = gpio_pin_configure_dt(&int1_gpio, GPIO_INPUT | GPIO_INT_EDGE_RISING);
    if (ret != 0) {
        printk("Failed to configure INT1 GPIO: %d\n", ret);
        return -1;
    }
    gpio_init_callback(&gpio_cb, int1_handler, BIT(int1_gpio.pin));
    ret = gpio_add_callback(int1_gpio.port, &gpio_cb);
    if (ret != 0) {
        printk("Failed to add GPIO callback: %d\n", ret);
        return -1;
    }

    ret = gpio_pin_interrupt_configure_dt(&int1_gpio, GPIO_INT_EDGE_RISING);
    if (ret != 0) {
        printk("Failed to enable INT1 interrupt: %d\n", ret);
        return -1;
    }

    printk("INT1 interrupt configured on GPIOD pin %d\n", int1_gpio.pin);
    // INTERRUPTS

    printk("System Initialized. Entering main loop.\n");

    while (1) {
        // TODO SLEEP HARDER
        gpio_pin_toggle_dt(&led0);
        k_msleep(1000);
    }
}
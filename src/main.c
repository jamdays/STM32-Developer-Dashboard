
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

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define NUM_SENSORS 6
#define SCHEDULE_FILE "/lfs/schedule.txt"

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
    int num_axes;
    struct axes_info *axes;
    struct k_work work;
    struct k_work http_work; // For HTTP client
    const char * url; // For HTTP client
};

// Filesystem
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(cstorage);
static struct fs_mount_t littlefs_mnt = {
    .type = FS_LITTLEFS,
    .fs_data = &cstorage,
    .storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
    .mnt_point = "/lfs"
};

// Wifi
#define WIFI_CONFIG_FILE "/lfs/wifi.conf"
static struct net_if *iface;
static struct wifi_connect_req_params wifi_params;
static struct net_mgmt_event_callback wifi_cb;
volatile char *ssid = NULL;
volatile char *password = NULL;

volatile bool wifi_is_ready = false;

// Sensor Info

// REST client
#define MAX_CALLBACKS 5
struct sensor_callback {
    const char *sensor_name;
    char url[128];
};
static struct sensor_callback callbacks[MAX_CALLBACKS];
static int callback_count = 0;



// Work queue for handling http interrupts
#ifdef CONFIG_HTTP_CLIENT
K_THREAD_STACK_DEFINE(http_work_q_stack, 2048);
static struct k_work_q http_work_q;
struct http_work {
    struct k_work work;
    char url[128];
    char data[128];
};
#endif


#ifdef CONFIG_HTTP_CLIENT
static void http_client_work_handler(struct k_work *work);
#endif

// Forward declarations for shell commands
static int cmd_read_sensor(const struct shell *shell, size_t argc, char **argv);

static void http_response_cb(struct http_response *rsp, enum http_final_call final_data, void *user_data)
{
    if (final_data == HTTP_DATA_FINAL) {
        printk("REST call finished with status: %s\n", rsp->http_status);
    }
}

// Make HTTP Request
#ifdef CONFIG_HTTP_CLIENT
static void http_client_work_handler_old2(struct k_work *work)
{
    //struct http_work *http_work_item = CONTAINER_OF(http_work, struct http_work, http_work);
    struct http_work *http_work_item;
    struct http_request req;
    static uint8_t recv_buf[512];
    int sock;
    struct addrinfo *res;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };

    // Basic URL parsing
    char *host = http_work_item->url;
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
    if (sock < 0) {
        printk("Failed to create socket\n");
        freeaddrinfo(res);
        return;
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        printk("Failed to connect to server\n");
        close(sock);
        freeaddrinfo(res);
        return;
    }

    memset(&req, 0, sizeof(req));
    req.method = HTTP_POST;
    req.url = path;
    req.host = host;
    req.protocol = "HTTP/1.1";
    req.payload = http_work_item->data;
    req.payload_len = strlen(http_work_item->data);
    const char *headers = "Content-Type: application/json\r\n";
    req.header_fields = headers;
    req.response = http_response_cb;
    req.recv_buf = recv_buf;
    req.recv_buf_len = sizeof(recv_buf);

    if (http_client_req(sock, &req, 5000, NULL) < 0) {
        printk("HTTP client request failed\n");
    }

    close(sock);
    freeaddrinfo(res);
}


static void http_client_work_handler_old(struct k_work *work)
{
    printk("Executing HTTP client work\n");
    struct http_work *http_work_item = CONTAINER_OF(work, struct http_work, work);
    struct http_request req;
    static uint8_t recv_buf[512];
    int sock;
    struct addrinfo *res;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };

    // Basic URL parsing
    char *host = http_work_item->url;
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
    if (sock < 0) {
        printk("Failed to create socket\n");
        freeaddrinfo(res);
        return;
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        printk("Failed to connect to server\n");
        close(sock);
        freeaddrinfo(res);
        return;
    }

    memset(&req, 0, sizeof(req));
    req.method = HTTP_POST;
    req.url = path;
    req.host = host;
    req.protocol = "HTTP/1.1";
    req.payload = http_work_item->data;
    req.payload_len = strlen(http_work_item->data);
    const char *headers = "Content-Type: application/json\r\n";
    req.header_fields = headers;
    req.response = http_response_cb;
    req.recv_buf = recv_buf;
    req.recv_buf_len = sizeof(recv_buf);

    if (http_client_req(sock, &req, 5000, NULL) < 0) {
        printk("HTTP client request failed\n");
    }

    close(sock);
    freeaddrinfo(res);
}
#endif

// HTTP Trigger Handler
#ifdef CONFIG_HTTP_CLIENT
static void trigger_handler(const struct device *dev, const struct sensor_trigger *trig)
{
    //printk("Sensor interrupt triggered for %s\n", dev->name);
    for (int i = 0; i < callback_count; i++) {
        if (strcmp(callbacks[i].sensor_name, dev->name) == 0) {
            struct http_work *new_work = k_malloc(sizeof(struct http_work));
            if (new_work) {
                k_work_init(&new_work->work, http_client_work_handler);
                strcpy(new_work->url, callbacks[i].url);
                snprintf(new_work->data, sizeof(new_work->data), "{\"sensor\":\"%s\",\"value\":\"triggered\"}", dev->name);
                k_work_submit_to_queue(&http_work_q, &new_work->work);
            }
        }
    }
    if (sensor_sample_fetch(dev) < 0) {
        printk("Failed to fetch sensor sample for %s\n", dev->name);
        return;
    }

    // Re-enable the trigger
}
#endif

// Example Button Pressed HTTP Callback Event
#ifdef CONFIG_HTTP_CLIENT
void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    printk("Button pressed\n");
    for (int i = 0; i < callback_count; i++) {
        if (strcmp(callbacks[i].sensor_name, "BUTTON0") == 0) {
            struct http_work *new_work = k_malloc(sizeof(struct http_work));
            if (new_work) {
                k_work_init(&new_work->work, http_client_work_handler);
                strcpy(new_work->url, callbacks[i].url);
                snprintf(new_work->data, sizeof(new_work->data), "{\"button\":\"pressed\"}");
                k_work_submit_to_queue(&http_work_q, &new_work->work);
            }
            return;
        }
    }
}
#endif

// Connect Wifi passing ssid and password (todo add key)
static int cmd_wifi_connect(const struct shell *shell, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(shell, "Usage: wifi_connect <ssid> <password>");
        return -EINVAL;
    }
    wifi_params.ssid = (const uint8_t *)argv[1];
    wifi_params.ssid_length = strlen(argv[1]);
    wifi_params.psk = (const uint8_t *)argv[2];
    wifi_params.psk_length = strlen(argv[2]);
    wifi_params.security = WIFI_SECURITY_TYPE_PSK;
    wifi_params.channel = WIFI_CHANNEL_ANY;

    iface = net_if_get_default();
    if (!iface) {
        shell_error(shell, "Could not get default network interface");
        return -ENODEV;
    }

    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &wifi_params, sizeof(struct wifi_connect_req_params));
    if (ret) {
        shell_error(shell, "Failed to connect to WiFi: %d", ret);
    } else {
        shell_print(shell, "Connecting to WiFi...");
    }
    return ret;
}

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

// Toggle LED 1
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

// Print contents of root dir (/lfs) to Shell
static int cmd_ls (const struct shell *shell, size_t argc, char **argv) {
    int rc;
    struct fs_dir_t dir = {}; 
    static struct fs_dirent entry;


    rc = fs_opendir(&dir, "/lfs");
    if (rc < 0) {
        shell_error(shell, "Failed to open directory \"/\": %d", rc);
        return rc;
    }

    while(1) {
        rc = fs_readdir(&dir, &entry);
        if (rc < 0) {
            shell_error(shell, "Failed to read directory \"/\": %d", rc);
            fs_closedir(&dir);
            return rc;
        } else if (rc == 0 && entry.name[0] == '\0') {
            break; // No more entries
        } else if (rc == 0) {
            shell_print(shell, "%s", entry.name);
        } else {
            shell_print(shell, "Unexpected return value from fs_readdir: %d", rc);
            break;
        }
    }
    fs_closedir(&dir);
}

// Print contents of a file to Shell
static int cmd_cat(const struct shell *shell, size_t argc, char **argv)
{
    struct fs_file_t file;
    fs_file_t_init(&file);
    int rc;
    char * filepath = argv[1];

    rc = fs_open(&file, filepath, FS_O_READ);
    if (rc == -ENOENT) {
        shell_print(shell, "File doesn't exist: %s", filepath);
        return 0;
    } else if (rc < 0) {
        shell_error(shell, "Failed to open file %s: %d", filepath, rc);
        return rc;
    } else {
        shell_print(shell, "Contents of %s:", filepath);
        char buf[64];
        while(1) { // reading 64 bytes at a time for now. maybe bigger later
            int got = fs_read(&file, buf, sizeof(buf) - 1);
            if (got < 0) { //error
                shell_error(shell, "Failed to read file %s: %d", filepath, got);
                fs_close(&file);
                return got;
            } else if (got == 0) {
                break;
            } 
            buf[got] = '\0'; // null-terminate the string
            //shell_print(shell, "%s", buf); 
            shell_fprintf(shell, SHELL_NORMAL, "%s", buf);
        }
        fs_close(&file);
        return 0;
    }
}

void cmd_rm(const struct shell *shell, size_t argc, char **argv) {
    if (argc < 2) {
        shell_error(shell, "Usage: rm <file_name>");
        return;
    }
    const char *file_name = argv[1];
    int rc = fs_unlink(file_name);
    if (rc < 0) {
        shell_error(shell, "Failed to remove file %s: %d", file_name, rc);
    } else {
        shell_print(shell, "File %s removed successfully", file_name);
    }
}

static void cmd_wifi_save (const struct shell *shell, size_t argc, char **argv){
    struct fs_file_t file;
    fs_file_t_init(&file);
    int rc = fs_open(&file, WIFI_CONFIG_FILE, FS_O_CREATE | FS_O_WRITE);
    if (rc < 0) {
        printk("Failed to open WiFi config file: %d\n", rc);
        return;
    }

    if (argc == 3) {
        fs_write(&file, argv[1], strlen(argv[1]));
        fs_write(&file, "\n", 1);
        fs_write(&file, argv[2], strlen(argv[2]));
        fs_write(&file, "\n", 1);
        printk("WiFi credentials saved to %s\n", WIFI_CONFIG_FILE);
    } else {
        printk("No WiFi credentials to save.\n");
    }
    fs_close(&file);
}

static void cmd_wifi_reconnect(const struct shell *shell, size_t argc, char **argv) {
    wifi_connect_to_saved_network();
}

struct sensor_save_work {
    struct k_work work;
    char sensor_name[32]; // Sensor name
    char file_name[64]; // File name to save data
};

enum sensor_names {
    HTS221,
    LPS22HB,
    LIS3MDL,
    LSM6DSL,
    VL53L0X,
    BUTTON0
};

void sensor_timer_callback(struct k_timer *timer_id);

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
        // Button 0 as GPIO
        .dev_or_gpio = TYPE_GPIO,
        .dev = NULL, // No device, just GPIO
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
    return -1; // Not found
}

void http_client_work_handler(struct k_work *work) {
    struct sensor_info *sensor = CONTAINER_OF(work, struct sensor_info, http_work);

    char buf[128];           // Larger buffer to ensure full string fits
    struct http_request req;
    static uint8_t recv_buf[512];
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
    }/**/
/*
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        printk("Failed to create socket\n");
        freeaddrinfo(res);
        return;
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        printk("Failed to connect to server\n");
        close(sock);
        freeaddrinfo(res);
        return;
    }*/
    
    struct sockaddr_in addr = {0};
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    //inet_pton(AF_INET, "192.168.3.14", &addr.sin_addr);
    
    //inet_pton(AF_INET, host, &addr.sin_addr);
    addr.sin_addr = *((struct in_addr *)res->ai_addr->data + 2); // Copy the resolved address
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    char _buf[256]; // Buffer for the HTTP request
    char * msg_template = "POST /%s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%s";
    snprintf(_buf, sizeof(_buf), msg_template, path, host, len, buf);
    write(sock, _buf, strlen(_buf));
    close(sock);

    /*memset(&req, 0, sizeof(req));
    req.method = HTTP_POST;
    req.url = path;
    req.host = host;
    req.protocol = "HTTP/1.1";
    req.payload = buf;
    req.payload_len = strlen(buf);
    static const char *headers = "Content-Type: application/json\r\n";
    req.header_fields = headers;
    req.response = http_response_cb;
    req.recv_buf = recv_buf;
    req.recv_buf_len = sizeof(recv_buf);


    if (http_client_req(sock, &req, 5000, NULL) < 0) {
        printk("HTTP client request failed\n");
    }

    close(sock);*/
    //freeaddrinfo(res);
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
        printk("Failed to open file: %s\n", full_path);
        return;
    }

    //fs_seek(&file, 0, FS_SEEK_END);
    ret = fs_write(&file, buf, len);
    if (ret < 0) {
        printk("Failed to write to file: %d\n", ret);
    }

    printk("Writing to file %s->%s<-END\n", full_path, buf);
    fs_close(&file);
}

void sensor_timer_callback(struct k_timer *timer_id) {
    struct sensor_info *sensor = CONTAINER_OF(timer_id, struct sensor_info, timer);
    k_work_submit(&sensor->work);
}

void sensor_timer_http_callback(struct k_timer *timer_id) {
    struct sensor_info *sensor = CONTAINER_OF(timer_id, struct sensor_info, http_timer);
    k_work_submit(&sensor->http_work);
}

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
        sensor_channel_get(lsm6dsl, SENSOR_CHAN_ACCEL_XYZ, val);
        used = snprintf(buf, buf_len,
                        "LSM6DSL Accel: X %d.%06d, Y %d.%06d, Z %d.%06d m/s^2\n",
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
        }
    }

    else if (strcmp(sensor_name, "vl53l0x") == 0) {
        if ((rc = sensor_sample_fetch(vl53l0x)) != 0) {
            return rc;
        }
        sensor_channel_get(vl53l0x, SENSOR_CHAN_DISTANCE, &val[0]);

        used = snprintf(buf, buf_len,
                        "VL53L0X: Distance %d mm\n",
                        val[0].val1);
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

    /* Enforce null termination and check for truncation */
    buf[buf_len - 1] = '\0';
    if (used >= (int)buf_len) {
        return -ENOSPC;
    }

    return used;
}

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
SHELL_CMD_REGISTER(wifi_connect, NULL, "Connect to WiFi", cmd_wifi_connect);
SHELL_CMD_REGISTER(toggle_led1, NULL, "Toggle LED1", cmd_toggle_led1);

SHELL_CMD_REGISTER(ls, NULL, "List items on FS", cmd_ls);
SHELL_CMD_REGISTER(cat, NULL, "Display contents of a file", cmd_cat);
SHELL_CMD_REGISTER(rm, NULL, "Remove a file", cmd_rm);

SHELL_CMD_REGISTER(wifi_save, NULL, "Save WiFi credentials to file", cmd_wifi_save);
SHELL_CMD_REGISTER(wifi_reconnect, NULL, "Reconnect to saved WiFi network", cmd_wifi_reconnect);

SHELL_CMD_REGISTER(sensor_timer_start, NULL, "Start sensor timer", cmd_sensor_timer_start);
SHELL_CMD_REGISTER(sensor_timer_stop, NULL, "Stop sensor timer", cmd_sensor_timer_stop);

SHELL_CMD_REGISTER(sensor_timer_http_start, NULL, "Start sensor HTTP timer", cmd_sensor_timer_http_start);
SHELL_CMD_REGISTER(sensor_timer_http_stop, NULL, "Stop sensor HTTP timer", cmd_sensor_timer_http_stop);

// TESTING
//SHELL_CMD_REGISTER(schedule, NULL, "Schedule a sensor reading", cmd_schedule_reading);
//SHELL_CMD_REGISTER(register_callback, NULL, "Register a REST callback for a sensor", cmd_register_callback);
//K_THREAD_DEFINE(scheduler, 2048, scheduler_thread, NULL, NULL, NULL, 7, 0, 0);

// Handle DHCP assigning IP (set state variable)
static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        char buf[NET_IPV4_ADDR_LEN];
        wifi_is_ready = true; 
        //printk("IPv4 address: %s\n", net_addr_ntop(AF_INET, &iface->config.ip.ipv4->unicast[0].address.in_addr, buf, sizeof(buf)));
    }

}

void read_wifi_config() {
    struct fs_file_t file;
    fs_file_t_init(&file);
    int rc = fs_open(&file, WIFI_CONFIG_FILE, FS_O_READ);
    if (rc < 0) {
        printk("Failed to open WiFi config file: %d\n", rc);
        return;
    }

    char _ssid[128];
    char _password[128];
    
    uint8_t _ssid_len;
    uint8_t _password_len;

    char buf[64];
    char * b = &buf;
    int len = 0;
    bool next = false;
    while ((len = fs_read(&file, b, 1)) > 0) {
        if (*b == '\n') {
            *b = '\0'; // Null-terminate the string
            if (!next) {
                _ssid_len = strlen(buf);
                if (_ssid_len > 0) {
                    strncpy(_ssid, buf, sizeof(_ssid) - 1);
                    _ssid[_ssid_len] = '\0';
                    next = true;
                    b = &buf[0]; // Reset buffer pointer
                    continue;
                }
            } else {
                _password_len = strlen(buf);
                if (_password_len > 0) {
                    strncpy(_password, buf, sizeof(_password) - 1);
                    _password[_password_len] = '\0';
                }
                break; 
            }
        }
        b++;

    }
    ssid = k_malloc(_ssid_len + 1);
    password = k_malloc(_password_len + 1);
    strcpy(ssid, _ssid);
    strcpy(password, _password);
    fs_close(&file);
    return; 
}

void wifi_connect_to_saved_network() {
    read_wifi_config();
    if (ssid && password) {
        wifi_params.ssid = (const uint8_t *)ssid;
        wifi_params.ssid_length = strlen(ssid);
        wifi_params.psk = (const uint8_t *)password;
        wifi_params.psk_length = strlen(password);
        wifi_params.security = WIFI_SECURITY_TYPE_PSK;
        wifi_params.channel = WIFI_CHANNEL_ANY;

        iface = net_if_get_default();
        if (!iface) {
            //shell_error(shell, "Could not get default network interface");
            return -ENODEV;
        }

        int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &wifi_params, sizeof(struct wifi_connect_req_params));
        if (ret) {
            //shell_error(shell, "Failed to connect to WiFi: %d", ret);
        } else {
            //shell_print(shell, "Connecting to WiFi...");
        }
        return;
        //return ret;
    } else {
        printk("No saved WiFi credentials found.\n");
    }
}

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
        sensors[i].cb_filename = k_malloc(64);
        sensors[i].url = k_malloc(128);
    }
}

void main(void)
{
    printk("Starting application\n");

    // Initialize LEDs and Button
    printk("Initializing LEDs and button...\n");
    gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&button0, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&button0, GPIO_INT_EDGE_TO_ACTIVE);
    
    #ifdef CONFIG_HTTP_CLIENT
    // Setup Button HTTP Callback for testing (disable later)
    gpio_init_callback(&button_cb_data, button_pressed, BIT(button0.pin));
    gpio_add_callback(button0.port, &button_cb_data);
    #endif

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

    // Initialize Work Queue
    #ifdef CONFIG_HTTP_CLIENT
    printk("Initializing work queue...\n");
    k_work_queue_start(&http_work_q, http_work_q_stack, K_THREAD_STACK_SIZEOF(http_work_q_stack), K_PRIO_COOP(7), NULL);
    printk("Work queue initialized\n");
    #endif

    // Initialize Sensors and Triggers
    init_sensors();

    struct sensor_value odr_attr;
    odr_attr.val1 = 1; // Set ODR to 100 Hz
    odr_attr.val2 = 0;

    if (sensor_attr_set(lsm6dsl, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr) < 0) {
        printk("Failed to set LSM6DSL ODR\n");
    }
    if (sensor_attr_set(lsm6dsl, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr) < 0) {
        printk("Failed to set LSM6DSL Gyro ODR\n");
    }
    
    printk("System Initialized. Entering main loop.\n");

    while (1) {
        // TODO SLEEP HARDER
        gpio_pin_toggle_dt(&led0);
        k_msleep(1000);
    }
}
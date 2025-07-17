#include "wifi.h"
#include <zephyr/net/wifi_mgmt.h>


#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

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

SHELL_CMD_REGISTER(wifi_connect, NULL, "Connect to WiFi", cmd_wifi_connect);
SHELL_CMD_REGISTER(wifi_save, NULL, "Save WiFi credentials to file", cmd_wifi_save);
SHELL_CMD_REGISTER(wifi_reconnect, NULL, "Reconnect to saved WiFi network", cmd_wifi_reconnect);
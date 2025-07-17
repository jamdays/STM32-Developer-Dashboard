#include <stddef.h>
#include <zephyr/shell/shell.h>

#ifndef WIFI_H
#define WIFI_H

// Wifi
#ifndef WIFI_CONFIG_FILE
#define WIFI_CONFIG_FILE "/lfs/wifi.conf"
#endif

static struct net_if *iface;
static struct wifi_connect_req_params wifi_params;
static struct net_mgmt_event_callback wifi_cb;
volatile char *ssid;
volatile char *password;
volatile bool wifi_is_ready;

int cmd_wifi_connect(const struct shell *shell, size_t argc, char **argv);

void cmd_wifi_save(const struct shell *shell, size_t argc, char **argv);

void cmd_wifi_reconnect(const struct shell *shell, size_t argc, char **argv);

#endif
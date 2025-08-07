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
extern volatile char ssid[32];
extern volatile char password[64];
extern volatile bool wifi_is_ready;

static int cmd_wifi_connect(const struct shell *shell, size_t argc, char **argv);

static void cmd_wifi_save(const struct shell *shell, size_t argc, char **argv);

static void cmd_wifi_reconnect(const struct shell *shell, size_t argc, char **argv);

void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface);
void read_wifi_config();
void wifi_connect_to_saved_network();

#endif
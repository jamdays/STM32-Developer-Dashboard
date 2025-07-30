#include "bluetooth.h"
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/services/nus.h>


int bt_main()
{
    struct bt_nus_cb nus_listener = {
        .notif_enabled = notif_enabled,
        .received = received,
    };
	int err;

	printk("Sample - Bluetooth Peripheral NUS\n");

	err = bt_nus_cb_register(&nus_listener, NULL);
	if (err) {
		printk("Failed to register NUS callback: %d\n", err);
		return err;
	}

	err = bt_enable(NULL);
	if (err) {
		printk("Failed to enable bluetooth: %d\n", err);
		return err;
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		printk("Failed to start advertising: %d\n", err);
		return err;
	}

	printk("Initialization complete\n");

	while (true) {
		const char *hello_world = "Hello World!\n";

		k_sleep(K_SECONDS(3));

		err = bt_nus_send(NULL, hello_world, strlen(hello_world));
		printk("Data send - Result: %d\n", err);

		if (err < 0 && (err != -EAGAIN) && (err != -ENOTCONN)) {
			return err;
		}
	}

	return 0;
}
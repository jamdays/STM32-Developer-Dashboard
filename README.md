# Guide
## Guide: using the UART shell
First, build and flash the developer dashboard onto your discovery board.
Within VSCode (with the PlatformIO plugin installed), open the PlatformIO: Serial Monitor (it looks like a little plug icon on the bottom bar of the screen)
Enter the following keystrokes, exactly as they appear. `> control + T` refers to pressing both the control and T keys at the same time. `> return` refers to pressing the enter/return key on your keyboard. Any input that is not preceded with a `>` refers to individually typing out that combination of characters.
```
> control + T
b
115200
> return
> control + T
> control + F
direct
> return
```

Now, you can interact with the UART shell! Try out some of the following commands:
`help`
`read lsm6ds`

## Guide: storing periodic sensor readings to onboard storage
All sensor readings can be stored onto the board's flash storage!
Use the command sensor_timer_start `<sensor_name> <file_name> <timing>` to begin reading from a sensor (specified by `sensor_name`) to a file (specified by `file_name`) every `timing` seconds.

This continues indefinitely until you use `sensor_timer_stop <sensor_name>` to stop the sensor readings from being saved.

Note that in order to read the sensor data, you must use the `cat` command alongside the directory your file is stored in, which is the `/lfs/` directory. Ex: `cat /lfs/sensordata.txt`

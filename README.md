# Guide
## Guide: using the UART shell
First, build and flash the developer dashboard onto your discovery board.
Within VSCode (with the PlatformIO plugin installed), open the PlatformIO: Serial Monitor (it looks like a little plug icon on the bottom bar of the screen)

Now, you can interact with the UART shell! Try out some of the following commands:
`help`
`read lsm6dsl`
`read lis3mdl`
`read hts221`
`read lps22hb`
`read vl53l0x`
`read button0`

## Guide: storing periodic sensor readings to onboard storage
All sensor readings can be stored onto the board's flash storage!
Use the command `sensor_timer_start <sensor_name> <file_name> <timing>` to begin reading from a sensor (specified by `sensor_name` like the examples above) to a file (specified by `file_name`) every `timing` seconds.

This continues indefinitely until you use `sensor_timer_stop <sensor_name>` to stop the sensor readings from being saved.

Note that in order to read the sensor data, you must use the `cat` command. Ex: `cat sensordata.txt`.

When file storage is full, use the `rm` command to delete files, which you can see with the `ls` command.

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

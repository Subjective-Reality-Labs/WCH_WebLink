# WCH WebLink
A simple programmer for CH32V003 with web UI for ESP32 MCUs. Based on [minichlink](https://github.com/cnlohr/ch32v003fun/tree/master/minichlink) and [ESP32-S2 programmer](https://github.com/cnlohr/esp32s2-cookbook/tree/master/ch32v003programmer) by cnlohr.

<img src="https://github.com/Subjective-Reality-Labs/WCH_WebLink/assets/6649967/d0215b41-6f35-4595-a3f9-290cf214be91" width="500"/>

# Usage
Compile and flash using [Platformio](https://platformio.org/), don't forget to flash LittleFS partition. You can specify WiFi name/password with defines, or simply connect to AP "WebLink" (password is ``ch32v003isfun``) and connect to your network via UI.

Add 10K pull-up resistor for the chosen GPIO pin. ESP32-S2 code has glitch [option](https://github.com/Subjective-Reality-Labs/WCH_WebLink/blob/ffc90cf8fdfdbe9d19141bdeac7199d08fb240ac/src/ch32v003_swio.h#L34) to skip this resistor, but I couldn't make it work.

Open http://weblink.local/ in your browser. Select the compiled binary for CH32V003 with the file menu, or just drag and drop it in the file input area. Press "Flash" and hopefully it will be flashed in a few seconds.

To access the terminal and more flashing options press a little dot. If you hate animations you can disable minimalist version of the UI in the Settings menu.

By default Terminal uses SWIO debug interface for print in/out. If you prefer to use UART you can switch to it in the Settings menu. It will use the specified in ``platformio.ini`` Serial port at 115200 baud rate.

You can also upload firmware with a simple HTTP POST multipart request. For example: ``curl -F 'offset=134217728' -F 'size=4300' -F 'firmware=@color_lcd.bin' weblink.local/flash``. Then you can use a GET request to ``weblink.local/status`` to get the result of the last operation. Using other functions like ``/unbrick`` and ``/reset`` is also possible of course.

# WebSocket API
WebLink can also act as a minichlink replacement/addition. At the endpoint ``/wsflash`` you can use WebSocket to perform most of minichlink's [functions](https://github.com/cnlohr/ch32v003fun/tree/master/minichlink).
Format your message like this: ``#command;argument;second argument``. WebLink uses the same command/argument pattern as minichlink. The exceptions are the write ``#w`` and read ``#r`` commands.

Write command: ``#w;offset;size;retries;name``. All integers should be in decimal format. "Retries" is how many times to try to write to flash if it fails. "Name" is currently unused. "Retries" and "name" arguments are optional and can be omitted.

Write command requires two messages. First one is a text message for example ``#w;134217728;4300;3;color_lcd``, then you have to send a binary message which contains an actual program that will be flashed to an MCU.
All commands should start with ``#`` other messages will be ignored. All commands sent to WebLink will receive a reply in the format ``#reply code;reply message`` for example this is what binary upload would look like:
```
> #w;134217728;4300;3;color_lcd
> #0;Ready for upload
> binary message
> #0;Will flash
> #0;Flashed successfully
```
Read command: ``r;offset;ammount;``. Response will be like this:
```
> #0;Ready to download
> binary message with flash content
```
Response codes are:
```
#0 - command successful
#1 - flasher busy
#2 - link init failed
#3 - command argument bad/missing
#4 - command failed
#8 - command unimplemented
#9 - unknown command
```
**Note:** when a client sends a command to ``/wsflash`` all connections to ``/terminal`` are closed.
# Limitations and known issues
- Tested on ESP32-C3 and base ESP32 only, other version _should_ work, but untested. If you will use one please add a suitable entry to ``platformio.ini`` if there is a need for any additional options.
- Base ESP32 better handles terminal connection but may have some trouble while flashing, ESP32-C3 seems to be much more stable with flashing but sometimes skips characters in the terminal.
Maybe running AsyncTCP on the same core as Arduino and SWIO code can help with flashing, untested.
- The glitch trick with GPIO that helps to avoid the need of 10K pull-up resistor doesn't work with my setup, you are welcome to help to get this working.
- UI for scanning and connecting to WiFi network can be buggy when ESP32 is in AP mode, seems to be hardware related.
- Unbrick mode is copied from minichlink but untested, you need to be able to control VCC of the 003 with ESP32's GPIO pin, so ideally use a mosfet for this.
- Latest versions of minichlink support other WCH chips, but I've implemented only 003. Don't see any obstacles for it to work with the corresponding code added. As of now, I have no plans to implement this because I don't have any other MCUs apart from CH32V003.
- Terminal updates in batches to mitigate character skips on recieve. Delay can be set in the Settings menu.
- Reading flash and chip data is not implemented yet, but can be added later.
- UART terminal's baud rate is hardcoded as 115200, may add a setting for it in the UI later.
- SWIO pin should be chosen in the range of 0-31 or you can change GPIO functions in ``ch32v003_swio.h``
- Programming custom HEX and/or to other memory regions (other than 0x08000000) was not tested but _should_ work.

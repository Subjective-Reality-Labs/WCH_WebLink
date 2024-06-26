# WCH WebLink
A simple programmer for CH32V003 with web UI for ESP32 MCUs. Based on [minichlink](https://github.com/cnlohr/ch32v003fun/tree/master/minichlink) and [ESP32-S2 programmer](https://github.com/cnlohr/esp32s2-cookbook/tree/master/ch32v003programmer) by cnlohr.

<img src="https://github.com/Subjective-Reality-Labs/WCH_WebLink/assets/6649967/d0215b41-6f35-4595-a3f9-290cf214be91" width="500"/>

# Usage
Compile and flash using [Platformio](https://platformio.org/), don't forget to flash LittleFS partition. You can specify WiFi name/password with defines, or just connect to AP "WebLink" (password is ``ch32v003isfun``) and connect to your network via UI.

Add 10K pull-up resistor for chosen GPIO pin. ESP32-S2 code has glitch [option](https://github.com/Subjective-Reality-Labs/WCH_WebLink/blob/ffc90cf8fdfdbe9d19141bdeac7199d08fb240ac/src/ch32v003_swio.h#L34) to skip this resistor, but I couldn't make it work.

Open http://weblink.local/ in your browser. Select compiled binary for CH32V003 with the file menu, or just drag and drop it in the file input area. Press "Flash" and hopefully it will be flashed in few seconds.

To access terminal and more flashing options press a little dot. If you hate animations you can disable minimalist version of the UI in the Settings menu.

By default Terminal uses SWIO debug interface for print in/out. If you prefer to use UART you can switch to it in the Settings menu. It will use the specified in ``platformio.ini`` Serial port at 115200 baud rate.

# Limitations and knownn issues
- Tested on ESP32-C3 and base ESP32 only, other version _should_ work, but untested. If you will use one please add suitable entry to ``platformio.ini`` if there is need of any additional options.
- Base ESP32 better handles terminal connection, but may have some trouble while flashing, ESP32-C3 seems to be much more stable with flashing, but sometimes skips characters in the terminal.
Maybe running AsyncTCP on the same core as Arduino and SWIO code can help with flashing, not tested.
- Glitch trick with GPIO that helps to avoid the need of 10K pull-up resistor doesn't work with my setup, you are welcomed to help to get this working.
- UI for scanning and connecting to WiFi network can be buggy when ESP32 is in AP mode, seems to be hardware related.
- Unbrick mode is copied from minichlink but untested, you need to be able to control VCC of the 003 with ESP32's GPIO pin, so ideally use a mosfet for this.
- Latest versions of minichlink support other WCH chips, but I've implemented only 003. Don't see any obstacles for it to work with corresponding code added. As of now, I have no plans to implemet this because I don't have any other MCUs apart from CH32V003.
- Terminal updates in batches to mitigate charecter skips on recieve. Delay can be set in the Settings menu.
- Reading flash and chip data is not implemented yet, but can be added later.
- UART terminal baud rate is hardcoded as 115200, may add setting for it in the UI later.
- SWIO pin should be chosen in the range of 0-31 or you can change GPIO functions in ``ch32v003_swio.h``
- Programming custom HEX and/or to other memory regions (other than 0x08000000) was not tested but _should_ work.

Raspberry Pi Pico W WiFi to CAN Bus Bridge
========================================================================================================================
This project provides a WiFi access point to which devices such as tablets or PCs can connect to gain access to a physical CAN bus.

This project makes use of the [can2040](https://github.com/KevinOConnor/can2040) for the RP2040 microcontroller.

A NeoPixel LED is used to convey status and for easy the [Adafruit NeoPixel library](https://github.com/adafruit/Adafruit_NeoPixel) is used to control the LED.  That library has a wonderful mechanism to automatically allocate a PIO state machine for use.  Unfortunately, it tramples all over the PIO peripheral used by can2040.  Therefor, the version of the NeoPixel library in this project has been altered to default to the pio1 module rather than pio0, leaving pio0 available for CAN.



WiFi Connection
------------------------------------------------------------------------------------------------------------------------
This device can work in either client mode or access point mode.  That is to say, it can either connect to an existing WiFi network as a "client" or it can serve it's own station in access point (AP) mode for other devices, such as tablets, to connect to.

Regardless of the mode used, the port will be the same - 10001 by default, but changeable in the configuration settings of the project.


### AP Mode ############################################################################################################
If AP mode is selected (by setting the WIFI_MODE to AP_MODE in the code) then the `WIFI_SSID` and `WIFI_PASS` are used to set the name and credentials for the network served by the device.  To connect a device such as a tablet, first connect to the WiFi network in the device settings.  Then, a socket connection can be opened using the IP address `192.168.42.1` and port 10001 (unless changed).


### Station Mode #######################################################################################################
In this mode, the device will connect to an existing WiFi network specified using the `WIFI_SSID` and `WIFI_PASS` config options.  The device will use DHCP to acquire an IP address which will be printed to the debug serial terminal over the USB connection.  That IP address must be used to connect to the device.




Drivers
------------------------------------------------------------------------------------------------------------------------
Drivers on Windows are a bit of a pain.  The best option is to use [zadig](https://zadig.akeo.ie/) to set the drivers for both run mode and boot mode.

Start with boot mode - hold the boot button on the Pico when plugging in to PC.  Launch Zadig and locate the "RP2 Boot (Interface 1)" device.  You may need to enabled the List All Devices option.  Select "libusb-win32" from the list of available driver options and hit the "Install dirver" button.  Wait for completion.

You can now flash the device with firmware using the platformio upload command.  Alternatively, if still in boot mode, drag the .uf2 file from the .pio build directory onto the device.

Once out of the bootloader and in run mode locate the "Pico W" device in Zadig and install the "USB Serial (CDC)" driver.  Wait for completion.

You should now be able to launch a Monitor window from platformio and view the debug output from the device.  The upload button should now also work reliably and without the need to press the boot select button to get back to the bootloader before reflashing.

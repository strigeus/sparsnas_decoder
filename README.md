**This repository has been retired. Please use alternative forks.**

*******************************************************************


A BIG thanks to the https://www.facebook.com/groups/SHgruppen on Facebook and a special superduper thanks to the core coder @strigeus!
https://github.com/strigeus/sparsnas_decoder

This is a fork is adapted for output in JSON format to a MQTT broker.

[![buy me a coffee](https://www.buymeacoffee.com/assets/img/custom_images/yellow_img.png)](https://www.buymeacoffee.com/tubalainen)

If you want to implement the same thing as below into an ESP8266 please see the following project: https://github.com/bphermansson/EspSparsnasGateway

If you want to get into the bits and bytes of the system please see:
https://github.com/kodarn/Sparsnas

Asif just made a HASSIO Addon based on this code, check out his repository here: https://github.com/asifma/hassio-addons

This is a decoder for IKEA SPARSNÄS.
===================================

It uses RTL-SDR running on a Raspberry Pi to demodulate the FSK signal, and decodes the packet.

The packet data is encrypted using your sender's ID. The sender ID is the last 6 digits of the serial number located under the battery.

Prerequisites:
-------------
Tested on a Raspberry Pi 3 with Raspbian Stretch with a NooElec Nano 3 USB SDR dongle. You should be able to use any RTL-SDR compatible dongle. If you live in Sweden/EU the NooElec Micro 3 dongle can be bought from www.amazon.de (free shipping on orders over 25 EUR).
Of get your self the "real deal", an original RTL-SDR v3 from https://www.lohelectronics.se/kommunikation/amatorradio/radio/rtl-sdr-r820t2-rtl2832u-1ppm-tcxo-sma-sdr . Dont forget to make your self an antenna, the IKEA Sparsnäs transmits on 868 MHz so a 1/2 wave lenght wire connected to the center pin in the antenna port should do the trick. Make the wire approximently 16 cm long.

An IKEA Sparsnäs unit (duuh)

An MQTT broker installed, see this video for support/help to get it going: https://www.youtube.com/watch?v=VaWdvVVYU3A

You can also pipe your MQTT data via node-red to InfluxDB for diplaying in for example Grafana, @naestrom have done a great job creating a node-red flow, please find more info here: https://github.com/Naesstrom/sparsnas_mqtt_nodered_influxdb

The installation:
-------------
It is highly recommended to use the [docker](https://docker.com) version described [here](docker.md).

The following instructions are for you that for some reason don't want to use docker, and assumes a debian or ubuntu based environment.

```sh
sudo apt-get update && sudo apt-get install -y git g++ libmosquitto-dev libmosquitto1 rtl-sdr zsh busybox
cd ~
git clone https://github.com/tubalainen/sparsnas_decoder
cd sparsnas_decoder
git checkout dockerVersion

# Build sparsnas_decode and copy the binary to /usr/bin
g++ -o sparsnas_decode -std=gnu++11  -O2 sparsnas_decode.cpp -lmosquitto
sudo cp sparsnas_decode /usr/bin/
```
The decoder is now cloned, compiled and copied to `/usr/bin/sparsnas_decoder`


Using your favorite text editor, create a new file named /etc/modprobe.d/no-rtl.conf and put the following text in the file. You need to run that text editor as sudo (i.e. 'sudo vi' or 'sudo nano' etc) to write to the modprobe.d directory:

```sh
sudo nano /etc/modprobe.d/no-rtl.conf
```

```
blacklist dvb_usb_rtl28xxu
blacklist rtl2832
blacklist rtl2830
```

Reboot with the USB dongle installed in one of the USB ports.

Test your dongle
```
rtl_test
```
Outcome should look something like this:
```
pi@raspberrypi:~ $ rtl_test
Found 1 device(s):
  0:  Realtek, RTL2838UHIDIR, SN: 00000001

Using device 0: Generic RTL2832U OEM
Found Rafael Micro R820T tuner
Supported gain values (29): 0.0 0.9 1.4 2.7 3.7 7.7 8.7 12.5 14.4 15.7 16.6 19.7 20.7 22.9 25.4 28.0 29.7 32.8 33.8 36.4 37.2 38.6 40.2 42.1 43.4 43.9 44.5 48.0 49.6
[R82XX] PLL not locked!
Sampling at 2048000 S/s.

Info: This tool will continuously read from the device, and report if
samples get lost. If you observe no further output, everything is fine.

Reading samples in async mode...
lost at least 24 bytes
```

How to configure the decoder:
-------------
Take a photo/make a note of your transmitter ID number underneeth the battery lid of the transmitter (not the display. Obmit the 400 and take the remaining six numbers and make a note of them.

Edit sparsnas.sh to setup your sender id. You are allowed to have unlimitied number of senders and the format is `transmitter_id pulses/kWh` followed by a space.

This command will do it for you :smile: (adding the transmitters 722270 and 602064 with 1000/10000 pulses/kWh respectively).

```sh
SENSORS="722270 1000 602064 10000"; sed -i "s/^SENSORS=.*/SENSORS=(${SENSORS})/" sparsnas.sh
```

Test the script:
```sh
./sparsnas.sh
```

To make the decoder report values over MQTT the MQTT variables needs to be configured as follows:

```sh
MQTT_HOST=192.168.x.x MQTT_PORT=1883 MQTT_USERNAME=username MQTT_PASSWORD=password ./sparsnas.sh
```


To make `sparsnas_decoder` run on startup (modify the `MQTT_*` variables according to your configuration and make sure you get the right path (`/home/pi/`) to `sparsnas.sh`)

```sudo nano /etc/rc.local```

```sh
#!/bin/sh -e
#
# rc.local
#
# This script is executed at the end of each multiuser runlevel.
# Make sure that the script will "exit 0" on success or any other
# value on error.
#
# In order to enable or disable this script just change the execution
# bits.
#
# By default this script does nothing.

# Print the IP address
_IP=$(hostname -I) || true
if [ "$_IP" ]; then
  printf "My IP address is %s\n" "$_IP"
fi

MQTT_HOST=192.168.x.x MQTT_PORT=1883 MQTT_USERNAME=username MQTT_PASSWORD=password /home/pi/sparsnas_decoder/sparsnas.sh

exit 0
```

Check status and traffic on your MQTT broker:
```sh
mosquitto_sub -v -h 192.168.x.x -u username -P password -t '#'
```
Example of outcome:
```json
core-ssh:~# mosquitto_sub -v -h 192.168.x.x -u username -P password -t '#'
sparsnas/602064 {"Sequence": 47318,"Watt": 2424.00,"kWh": 4207.933,"battery": 100,"FreqErr": 0.70,"CRC":"ok","Sensor":602064}
sparsnas/722270 {"Sequence":    77,"Watt": 3245.07,"kWh":    0.483,"battery": 100,"FreqErr": 0.40,"CRC":"ok","Sensor":722270}
```

To configure Home Assistant have a look in the [home-assistant](home_assistant) folder.

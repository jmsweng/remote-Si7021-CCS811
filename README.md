# remote-Si7021-CCS811

Use an ESP8266 to read Si7021 and CCS811 sensors and send data via MQTT in JSON format.

This branch connects to an open WiFi network and uses AdafruitIO as the broker using TLS.

## Instructions

### Set up Adafruit IO

Go to [https://io.adafruit.com](https://io.adafruit.com) and set up a new feed (note the name).

### Download code

#### Using Git

Clone the repo and checkout the `AdafruitIO` branch:

```shell
git clone https://github.com/seanauff/remote-Si7021-CCS811.git
cd remote-Si7021-CCS811
git checkout AdafruitIO
git pull origin AdafruitIO
```

#### Or download the zip

Click "Clone or download", "Download ZIP", and extract to folder of your choice.

### Create secrets file

Create a copy of the `secrets_example.h` file and name it `secrets.h`. Add your secrets to the newly created `secrets.h` file, keeping the double quotes:

* `SECRET_WIFI_SSID` SSID of the wireless network to connect to
* `SECRET_WIFI_PASSWORD` Wifi password, if needed
* `SECRET_AIO_USERNAME` Adafruit IO username (case sensitive!)
* `SECRET_AIO_KEY` Adafruit IO key
* `SECRET_AIO_FEEDNAME` Name of the Adafruit IO feed you set up earlier (case sensitive!)

### Choose WiFi type

If using a WiFi network with a password, go to the `remote_Si7021_CCS811.ino` file and uncomment the line in the `WiFi Access Point` section and the line in the `setup_wifi()` function. Comment out the `WiFi.begin(WLAN_SSID, NULL);` line.

### Flash board

Upload the code to your board of choice.

### Verify it's working

Use the serial monitor to verify that the connection to WiFi and Adafruit IO is successful, and that sensor readings are being published. Go to your Adafruit IO feed page and verify that messages are received. Note that no data will be plotted due to the messages being in JSON format.

### Connect to the Adafruit IO MQTT server with another client

An example telegraf config is provided.

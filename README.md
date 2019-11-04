# remote-Si7021-CCS811

Use an ESP8266 to read Si7021 and CCS811 sensors and send data via MQTT in JSON format.

This branch connects to an open WiFi network and uses AdafruitIO as the broker using TLS.

## Instructions

Clone the repo and checkout the `AdafruitIO` branch:

```shell
git clone https://github.com/seanauff/remote-Si7021-CCS811.git
cd remote-Si7021-CCS811
git checkout AdafruitIO
git pull origin AdafruitIO
```

Create a copy of the `secrets_example.h` file and name it `secrets.h`. Add your secrets to the newly created `secrets.h` file.
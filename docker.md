# Docker support instructions


1.  Build docker container as follows. The `SENSORS` is required and is the unique
    id of your sensors in the form `sensor_id pulses_per_kwh`. The various `MQTT_`args are
    optional (but highly recommended to enable communication to your mqtt_broker).

```
docker build -t sparsnas --build-arg SENSORS="1234 1000" \
  --build-arg MQTT_HOST=192.168.x.x --build-arg MQTT_PORT \
  --build-arg=MQTT_USERNAME=username --build-arg=MQTT_PASSWORD \
  https://github.com/fredrike/sparsnas_decoder.git#dockerVersion
```

2. Run the container (it is possible to set the arguments above at runtime as
   `-e MQTT_HOST=192.168.1.2` if you like).

```
docker run --device=/dev/bus/usb --name=sparsnas --restart=always sparsnas:latest
```

3. Sit back and enjoy :smile:.

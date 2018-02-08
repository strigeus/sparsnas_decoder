# Docker support instructions


1. Build docker container as follows. The `SENSOR_ID` is required and is the unique
   id of your sensor, `PULSES_PER_KWH` and `MQTT_ARG` are optional (but highly recommended).

```
docker build -t sparsnas --build-arg SENSOR_ID=1234 --build-arg PULSES_PER_KWH=10000 \
  --build-arg MQTT_ARG="-h 192.168.x.x -u username -P password -i sparsnas" \
  https://github.com/fredrike/sparsnas_decoder.git#dockerVersion
```

2. Run the container.

```
docker run --device=/dev/bus/usb --name=sparsnas --restart=always sparsnas:latest
```

3. Sit back and enjoy :smile:.

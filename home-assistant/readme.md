# Example package for [Home Assistant](https://home-assistant.io/)

The file `sparsnas_mqtt.yaml` is a package for Home Assistant's [package configuration feature](https://home-assistant.io/docs/configuration/packages/). Requires [Custom UI](https://github.com/andrey-git/home-assistant-custom-ui) for optimal layout.

Once loaded the package will create the following panel in Home Assistant.

<img src="panel.png?raw=true" />
<img src="panel-focus.png?raw=true" />

## Configuration
Either replace all occurances of `!secret sparsnas_sensor` with the mqtt topic the decoder is sending on (typically `sparsnas/SENSOR_ID`), or (preferrable) configure Home Assistant to use [secrets](https://home-assistant.io/docs/configuration/secrets/) and set the secret `sparsnas_sensor` to the right mqtt topic (typically `sparsnas/SENSOR_ID`).

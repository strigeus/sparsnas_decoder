# docker build --build-arg SENSOR_ID=1234 --build-arg PULSES_PER_KWH=10000 \
#   --build-arg MQTT_ARG="-h 192.168.x.x -u username -P password -i sparsnas".

FROM alpine:edge as BUILD_ENV

COPY ./sparsnas_decode.cpp /build/

RUN apk add --no-cache g++ && \
    g++ -o /build/sparsnas_decode -static-libgcc -static-libstdc++ -O2 /build/sparsnas_decode.cpp

FROM alpine:edge

ARG SENSOR_ID
ENV SPARSNAS_SENSOR_ID=$SENSOR_ID
ARG PULSES_PER_KWH=1000
ENV SPARSNAS_PULSES_PER_KWH=$PULSES_PER_KWH
ARG SPARSNAS_FREQ_MIN
ARG SPARSNAS_FREQ_MAX
ARG MQTT_ARG
ENV MQTT_ARG=$MQTT_ARG

COPY --from=BUILD_ENV /build/sparsnas_decode /usr/bin/
COPY sparsnas.sh /

RUN apk add --no-cache --repository http://dl-3.alpinelinux.org/alpine/edge/testing/ --allow-untrusted \
      rtl-sdr \
      mosquitto-clients && \
    \
    if [ "$SPARSNAS_FREQ_MIN" -a "$SPARSNAS_FREQ_MAX" ]; then \
      echo "export SPARSNAS_FREQ_MIN=${SPARSNAS_FREQ_MIN}" >> /etc/sparsnas.conf && \
      echo "export SPARSNAS_FREQ_MAX=${SPARSNAS_FREQ_MAX}" >> /etc/sparsnas.conf; \
   fi

ENTRYPOINT ["/sparsnas.sh"]

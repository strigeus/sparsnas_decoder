#!/bin/sh

set -ae

SPARSNAS="/usr/bin/sparsnas_decode"
RTL_SDR="rtl_sdr -f 868000000 -s 1024000 -g 40 -"

if [ -e /etc/sparsnas.conf ]; then
  . /etc/sparsnas.conf
fi

for i in `seq 2`; do
  if [ "$SPARSNAS_FREQ_MIN" -a "$SPARSNAS_FREQ_MAX" ]; then
    $RTL_SDR | $SPARSNAS 2>&1 | \
      /usr/bin/mosquitto_pub -l -t 'home/sparsnas' ${MQTT_ARG:--h 127.0.0.1 -i sparsnas}
  else 
    echo "We have not configured sparsnas, try to find frequencies."
    $RTL_SDR > /tmp/sparsnas.raw & TASK_PID=$!; sleep 25; kill $TASK_PID &>/dev/null
    $SPARSNAS  /tmp/sparsnas.raw --find-frequencies > /tmp/sparsnas_freq
    rm /tmp/sparsnas.raw
    . /tmp/sparsnas_freq
    if [ "$SPARSNAS_FREQ_MIN" -a "$SPARSNAS_FREQ_MAX" ]; then
       echo "export SPARSNAS_FREQ_MIN=${SPARSNAS_FREQ_MIN}" >> /etc/sparsnas.conf && \
       echo "export SPARSNAS_FREQ_MAX=${SPARSNAS_FREQ_MAX}" >> /etc/sparsnas.conf;
    else
      echo "Could not find frequencies."
      exit 1
    fi
  fi
done

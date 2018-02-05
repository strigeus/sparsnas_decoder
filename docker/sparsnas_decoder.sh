#!/bin/bash

echo "Work in progress."
exit 0

SPARSNAS=/usr/bin/sparsnas_decode
RTL_SDR=rtl_sdr -f 868000000 -s 1024000 -g 10 -
SENDER_ID=$1
HOST=$2
USER=$3
PASS=$4

while true; do
  if [ -e /etc/sparsnas.conf ]; then
    source /etc/sparsnas.conf
    $RTL_SDR | $SPARSNAS -i $SENDER_ID -l $LOWER_ FREQUENCY -u $UPPER_FREQUENCY 2>&1 |\
      /usr/bin/mosquitto_pub -h $HOST -u $USER -P $PASSWORD -i sparsnas -l -t "home/sparsnas
  else 
    # We have not configured sparsnas, try to find frequencies.
    $RTL_SDR > /tmp/sparsnas.raw &
    TASK_PID=$!
    sleep 30
    kill $TASK_PID

    # sparsnas_decode.cpp should write FREQUENCES to stdout not stderr.
    $SPARSNAS  /tmp/sparsnas.raw --find-frequencies > /tmp/sparsnas_freq
    source /tmp/sparsnas_freq
    if [ -z "$UPPER_FREQUENCY" ]; then
      echo "Could not find frequencies."
      exit 0
    else
      echo "SENDER_ID=$1
HOST=$2
USER=$3
PASS=$4
LOWER_ FREQUENCY=$LOWER_FREQUENCY
UPPER_FREQUENCY=$UPPER_FREQUENCY" > /etc/sparsnas.conf
    fi
  fi
done

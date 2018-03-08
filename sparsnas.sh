#!/bin/zsh

set -o pipefail

#Associatve array
typeset -A SENSORS
SENSORS=(id_1 pulses_1 id_2 pulses_2)

RTL_SDR=(/usr/bin/rtl_sdr -f 868000000 -s 1024000 -g 40 -)
#RTL_SDR=(cat /sparsnas.raw)
SPARSNAS_DECODE="/usr/bin/sparsnas_decode"

FIND_FREQ="SPARSNAS_SENSOR_ID=_SENSE_ $SPARSNAS_DECODE /tmp/sparsnas.raw --find-frequencies > /tmp/_SENSE_.freq"
DECODE="> >(SPARSNAS_SENSOR_ID=_SENSE_ SPARSNAS_PULSES_PER_KWH=_pulse_ SPARSNAS_FREQ_MIN=_min_ SPARSNAS_FREQ_MAX=_max_ /usr/bin/sparsnas_decode)"

[ "${(k)SENSORS}" ] || exit 0

#Build find freq-cmd..
function findfreq () {
  CMD=""
  for i in ${(k)SENSORS}; do
    CMD="$CMD ${CMD:+ &} `echo $FIND_FREQ | sed "s/_SENSE_/$i/g"`"
  done

  echo $CMD
}

#Build decode-cmd.
function decode () {
  CMD=""
  for i in ${(k)SENSORS}; do
    _c=$(echo $DECODE | \
      sed -e "s/_SENSE_/$i/g" -e "s/_pulse_/$SENSORS[$i]/" \
      -e "s/_min_/$(awk -F= '/MIN/ {print $2}' < /tmp/$i.freq)/" \
      -e "s/_max_/$(awk -F= '/MAX/ {print $2}' < /tmp/$i.freq)/")
    CMD="$CMD $_c"
  done

  COMMAND="$RTL_SDR $CMD"
  echo $COMMAND
}

function frequencies () {
  for sensor in ${(k)SENSORS}; do
     grep -e SPARSNAS_FREQ_MAX -e SPARSNAS_FREQ_MIN /tmp/${sensor}.freq -q || return 1
  done
  return 0
}


if ( frequencies ); then
  echo "Frequencies have already been detected"
  CMD=`decode`
  # echo $CMD
  eval $CMD
else
  echo "Need to find frequencies"
  busybox timeout -t 45 $RTL_SDR > /tmp/sparsnas.raw
  #ls -l /tmp/sparsnas.raw
  F=`findfreq`
  # echo $F
  eval $F
  sleep 1
  rm /tmp/sparsnas.raw
  if ( frequencies ); then
    CMD=`decode`
    # echo $CMD
    eval $CMD
  else
    if [ ${#SENSORS[@]} -gt 1 ]; then
      echo "Could not find the sensors please verify that the sensor ids are: ${(k)SENSORS}"
    else
      echo "Could not find the sensor please verify that the sensor id is: ${(k)SENSORS}"
    fi
    exit -1
  fi
fi

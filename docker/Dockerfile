# derive from debian jessie image
FROM debian:jessie

MAINTAINER Fredrik Erlandsson

RUN apt-get update && \
    apt-get install -y rtl-sdr g++ git mosquitto-clients nano && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /usr/share/doc* /usr/share/man

WORKDIR /tmp

RUN echo 'blacklist dvb_usb_rtl28xxu' > /etc/modprobe.d/raspi-blacklist.conf && \
    git clone git://github.com/tubalainen/sparsnas_decoder && \
    g++ -o sparsnas_decoder/sparsnas_decode -O3 sparsnas_decoder/sparsnas_decode.cpp

WORKDIR /

RUN sed -i "14 d" /etc/rc.local && \
    echo 'rtl_sdr -f 868000000 -s 1024000 -g 10 - | sparsnas_decode 2>&1 | /usr/bin/mosquitto_pub -h 127.0.0.1 -u username -P password -i sparsnas -l -t "home/sparsnas"' >> /etc/rc.local




# Docker support

This dockerfile makes it possible to run `rtl-sdr` and `sparsnas_decode` as a docker (currently only tested on a x86 Synology NAS).

## Usage

Due to the way `sparsnas_decode` is constructed each of the steps of configuration requires a new compile of `sparsnas_decode`.

1. Build the docker environment:
   `sudo docker build --rm -t rtlsdr-sparsnas .`

2. Run the newly created docker image (this will spawn a shell in the new container):

   `sudo docker run -it --device=/dev/bus/usb --name=sparsnas rtlsdr-sparsnas:latest /bin/bash`

3. From the shell in the container follow the guidance from the main README
   until you have a working version that outputs values via mqtt. Copy the compiled
   `sparsnas_decode` to `/usr/bin` (`cp /tmp/sparsnas_decoder/sparsnas_decode /usr/bin`)
   and modify `/etc/rc.local` to include ip, username and password of your running
   mqtt-broker.

4. Remove the /tmp/sparsnas_decoder folder, including the file `sparsnas.raw` (`rm -rf /tmp/sparsnas_decoder`).

5. Exit the shell in the docker (`ctrl-d` or `exit`) and save the state of the image as a new docker-image

    ```
	sudo docker commit $(docker ps -a |grep sparsnas |awk '{print $1}') rtlsdr-sparsnas:configured
	```

6. Start the newly created image and enjoy :smile:

    ```
	sudo docker rm sparsnas
	sudo docker run -d --device=/dev/bus/usb --security-opt seccomp=unconfined \
		--name=sparsnas --restart=always rtlsdr-sparsnas:configured /etc/rc.local
	```


### Todo

* Rewrite `sparsnas-decode.cpp` to support argument of senderId and frequencies.
* Make the process more automatic (perhaps by only requiring the sender-id).
* Switch to using `alpine` as the base for the container (to keep it smaller).

# Container images for OpenCPN

## Builder image

Can be used to build OpenCPN in a container. All the dependencies are installed in the image.

### Building the image

From the root directory of OpenCPN source tree execute `podman build -f docker/Dockerfile.builder -t opencpn-builder .`

To build the image for a set of different architectures

```sh
podman build -f docker/Dockerfile.builder -t opencpn-builder --platform=linux/amd64,linux/arm64 .
```

### Using the image

Clone the OpenCPN source to your machine and mount it to the container as a volume

```sh
podman run -v <Path to your OpenCPN source tree>:/src:Z -it opencpn-builder:latest bash
```

To use the image for builds targetting other architecture, use the `--arch` parameter

```sh
podman run --arch arm64 -v <Path to your OpenCPN source tree>:/src:Z -it opencpn-builder:latest bash
```

Now you can follow the normal Linux build instructions

# Manually build OpenCPN in a Docker Desktop container

Here is a set of commands to manually build opencpn in a docker linux container. It assumes you have wsl2 installed and working. Also, windows docker desktop is installed but no other tools.
Start Docker Desktop and open a powershell termainal (click >_ Terminal at lower right of the docker window).

Run this command in the PowerShell terminal:
```sh
docker run -it --rm debian:latest bash
```
You can substitute debian:latest for any linux docker container you like such as: ubuntu,

In the resulting docker bash terminal that replaces the PowerShell CLI do this:
```sh
apt update && \
apt install -y \
 build-essential \
 ninja-build \
 devscripts \
 git \
 ca-certificates && \
mkdir /home/src && \
cd /home/src && \
git clone https://github.com/opencpn/opencpn.git --recurse-submodules && \
cd ./opencpn && \
cp ./ci/control /tmp && \
mk-build-deps -i -r -t 'apt-get -y' /tmp/control && \
rm /tmp/control && \
apt-get --allow-unauthenticated install -f -y && \
apt clean && \
cmake -S . -B ./build && \
cmake --build ./build -j$(nproc)
```
If all goes well the opencpn app for Linux will be built without errors. You can commit this image before you exit if you want to continue development or testing in the container you have populated with all the depdencies and the git repo.

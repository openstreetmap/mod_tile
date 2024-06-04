# Docker-based building & testing

For your convenience, we have provided a Docker-based building and testing method to help get started with development & testing.

### The following distributions are currently supported:

- archlinux _(Arch Linux)_ [[Dockerfile](/docker/archlinux/Dockerfile)]
- centos-stream-9 _(CentOS Stream 9)_ [[Dockerfile](/docker/centos/stream/Dockerfile)]
- debian-10 _(Debian 10)_ [[Dockerfile](/docker/debian/Dockerfile)]
- debian-11 _(Debian 11)_ [[Dockerfile](/docker/debian/Dockerfile)]
- debian-12 _(Debian 12)_ [[Dockerfile](/docker/debian/Dockerfile)]
- debian-unstable _(Debian Unstable)_ [[Dockerfile](/docker/debian/Dockerfile)]
- fedora-34 _(Fedora 34)_ [[Dockerfile](/docker/fedora/Dockerfile)]
- fedora-35 _(Fedora 35)_ [[Dockerfile](/docker/fedora/Dockerfile)]
- fedora-36 _(Fedora 36)_ [[Dockerfile](/docker/fedora/Dockerfile)]
- fedora-37 _(Fedora 37)_ [[Dockerfile](/docker/fedora/Dockerfile)]
- fedora-38 _(Fedora 38)_ [[Dockerfile](/docker/fedora/Dockerfile)]
- fedora-39 _(Fedora 39)_ [[Dockerfile](/docker/fedora/Dockerfile)]
- fedora-40 _(Fedora 40)_ [[Dockerfile](/docker/fedora/Dockerfile)]
- fedora-rawhide _(Fedora Rawhide)_ [[Dockerfile](/docker/fedora/Dockerfile)]
- opensuse-leap-15 _(openSUSE Leap 15)_ [[Dockerfile](/docker/opensuse/Dockerfile)]
- opensuse-tumbleweed _(openSUSE Tumbleweed)_ [[Dockerfile](/docker/opensuse/Dockerfile)]
- ubuntu-20.04 _(Ubuntu 20.04)_ [[Dockerfile](/docker/ubuntu/Dockerfile)]
- ubuntu-22.04 _(Ubuntu 22.04)_ [[Dockerfile](/docker/ubuntu/Dockerfile)]
- ubuntu-24.04 _(Ubuntu 24.04)_ [[Dockerfile](/docker/ubuntu/Dockerfile)]
- ubuntu-devel _(Ubuntu Devel)_ [[Dockerfile](/docker/ubuntu/Dockerfile)]

### Requirements:

- [Docker](https://docs.docker.com/get-docker/)
- [Docker Compose](https://github.com/docker/compose)

### Usage:

```shell
$ # Use any "{distribution}" value from the list above
$ # I.E. docker-compose up --build ubuntu-devel
$ cd docker
$ docker-compose up --build {distribution}
```

Then you can visit: `http://localhost:8081/renderd-example-map`

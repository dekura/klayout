# Build KLayout in docker

Four step, with 4 commands to build klayout in docker.



## Step one, install docker

1. docker is already installed on proj12
2. docker mac install [click here](https://docs.docker.com/docker-for-mac/install/)

## Step two, pull docker image by using my dockerfile

my dockerfile:

```docker
FROM fedora:30

WORKDIR /persist
RUN mkdir -p /persist && yum update -y \
    && yum install -y make git qt5-devel gcc-c++ \
    && yum install -y ruby ruby-devel python3 python3-devel \
    && echo "export PATH=/persist/bin/klayout:$PATH" >> /etc/bashrc
```

save this two line as `dockerfile.fedora`

then run

```bash
docker build --network=host -t myklayout:test -f dockerfile.fedora .
```

Please note the flag `--network=host` is required, otherwise the network in docker is unreachable.



### You will get

a docker image named `myklayout:test`

```bash
docker images
REPOSITORY                 TAG                 IMAGE ID            CREATED             SIZE
myklayout                  test                6b780d7e7423        23 minutes ago      1.12GB
```



## Step three:Data persistence in docker images

Because docker will not save the data for you, create a volume and mount it to docker, by run:

**!Important**: because `docker volume` is not convenient for the data transfer, so it is deprecated.

~~`docker volume create klayout-persist`~~



direct create a folder to store your data, such as

`/Users/dekura/chen/docker-persist/klayout-persist`



## Step four: Run docker image with data volume

Deprecated:


~~`docker run --net=host --name klayout --mount source=klayout-persist,target=/persist -it myklayout:test /bin/bash`~~



Please use:

```bash
docker run --net=host --name kmd -v /Users/dekura/chen/docker-persist/klayout-persist:/persist -it klayout:make /bin/bash


# on proj12
docker run --net=host --name kmd -v /home/glchen/docker-persist/klayout-persist:/persist -it klayout:make /bin/bash
```






## Be happy with building KLayout

the file tree is

```bash
persist/
    bin/
        klayout/      # -- to save the klayout binarary
    klayout/          # -- the klayout source code
```


```bash
cd /persist
mkdir bin && cd bin && mkdir klayout && cd ..

git clone https://github.com/dekura/klayout.git
cd klayout/
./build.sh -j4 -prefix /persist/bin/klayout
```



# Summary

```bash
docker build --network=host -t myklayout:test -f dockerfile.fedora .
# or mount directly
docker run --net=host --name kmd -v /Users/dekura/chen/docker-persist/klayout-persist:/persist -it klayout:make /bin/bash
# docker run --net=host --name kmd -v /home/glchen/docker-persist/klayout-persist:/persist -it klayout:make /bin/bash
cd /persist
mkdir bin && cd bin && mkdir klayout && cd ..
git clone https://github.com/dekura/klayout.git
cd klayout/
./build.sh -j4 -prefix /persist/bin/klayout
# for debug, you don't need it when you only need to build.
./build.sh -j4 -debug -prefix /persist/bin/klayout

# for non-gui build
./build.sh -j4 -debug -prefix /persist/bin/klayout-non-gui
```



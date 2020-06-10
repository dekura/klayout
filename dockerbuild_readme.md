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
    && yum install -y git qt5-devel gcc-c++ \
    && yum install -y ruby ruby-devel python3 python3-devel \
    && echo "export PATH=/persist/bin/klayout:$PATH" >> /etc/bashrc
```

save this two line as `dockerfile.fedora`

then run

```bash
docker build --network=host -t myklayout:test -f dockerfile.fedora .
```

Please note the the flag `--network=host` is required, otherwise the network in docker is unreachable.



### you will get

a docker image named `myklayout:test`

```bash
docker images
REPOSITORY                 TAG                 IMAGE ID            CREATED             SIZE
myklayout                  test                6b780d7e7423        23 minutes ago      1.12GB
```



## Step three:Data persistence in docker images

Because docker will not save the data for you, create a volume and mount it to docker, by run:

```bash
docker volume create klayout-persist
```

Check your volume by run:

```bash
docker volume inspect klayout-persist


[
    {
        "CreatedAt": "2020-05-31T05:47:13Z",
        "Driver": "local",
        "Labels": {},
        "Mountpoint": "/var/lib/docker/volumes/klayout-persist/_data",
        "Name": "klayout-persist",
        "Options": {},
        "Scope": "local"
    }
]
```



## Step four: Run docker image with data volume

```bash
docker run --net=host --name klayout --mount source=klayout-persist,target=/persist -it myklayout:test /bin/bash
```



## Be happy with building KLayout

```bash
cd /persist
git clone https://github.com/dekura/klayout.git

cd klayout/
./build.sh -j4 -prefix /persist/bin/klayout
```



# Summary

```bash
docker build --network=host -t myklayout:test -f dockerfile.fedora .
docker volume create klayout-persist
docker run --net=host --name klayout --mount source=klayout-persist,target=/persist -it myklayout:test /bin/bash


cd /persist
git clone https://github.com/dekura/klayout.git
cd klayout/
./build.sh -j4 -prefix /persist/bin/klayout
```



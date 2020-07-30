# Build KLayout in docker

Four step, with 4 commands to build klayout in docker.



## 1. Step one, install docker

1. docker is already installed on proj12
2. docker mac install [click here](https://docs.docker.com/docker-for-mac/install/)


> Tip!:
if the network of your docker is extremely slow, you can consider change the mirror of docker following [this article](https://zhuanlan.zhihu.com/p/109485448). My VPN is quite good, so I don't have to.

## 2. Step two, pull docker image by using my docker-file


### 2.1 git clone my modified KLayout


```bash
git clone https://github.com/dekura/klayout
cd klayout
```


### 2.2 build the environment using docker

my dockerfile:

```docker
FROM fedora:30

WORKDIR /persist
RUN mkdir -p /persist && yum update -y \
    && yum install -y make git qt5-devel gcc-c++ \
    && yum install -y ruby ruby-devel python3 python3-devel \
    && echo ". /persist/bin/mybashrc" >> /etc/bashrc
```


save this two line as `dockerfile.fedora`. **I have already saved this, so you don't have to.**

Just go to the docker file folder.

```bash
cd klayout/ci-scripts/docker/development_notes

# you will see dockerfile.fedora here.
```
then run

```bash
docker build --network=host -t myklayout:bash -f dockerfile.fedora .
```

Please note the flag `--network=host` is required, otherwise the network in docker is unreachable.



### You will get

a docker image named `myklayout:bash`

check it using `docker images`, and you will see.

```bash
docker images
REPOSITORY                 TAG                 IMAGE ID            CREATED             SIZE
myklayout                  bash                6b780d7e7423        23 minutes ago      1.12GB
```



## Step three:Data persistence in docker images

Because docker will not save the data for you, create a volume and mount it to docker, by run:

**!Important**: because `docker volume` is not convenient for the data transfer, so it is deprecated.

~~`docker volume create klayout-persist`~~



direct create a folder to store your data, such as

`/Users/dekura/chen/docker-persist/klayout-persist`

Then create a bin folder and a `mybasrc` file to store the `env` path.


```bash
cd /Users/dekura/chen/docker-persist/klayout-persist
mkdir bin
cd bin
touch mybashrc
```



## Step four: Run docker image with data volume

Deprecated:
~~`docker run --net=host --name klayout --mount source=klayout-persist,target=/persist -it myklayout:test /bin/bash`~~



Please use:

```bash
# please change the path to your own data-persist path

docker run --net=host --name kmd -v /Users/dekura/chen/docker-persist/klayout-persist:/persist -it myklayout:bash /bin/bash
```
After this command you will be in the docker images.

Then set the `env` path for docker

```bash
# in docker image
cd /persist/bin
echo "export PATH=/persist/bin/klayout:$PATH" >> mybashrc
```


## Be happy with building KLayout

the file tree is

```bash
persist/
    bin/
        mybashrc
        klayout/      # -- to save the klayout binarary
    klayout/          # -- the klayout source code
```


### clone Klayout first, clone in docker again~

```bash
cd /persist
mkdir bin && cd bin && mkdir klayout && cd ..

git clone https://github.com/dekura/klayout.git
```

### Build debug version without GUI

```bash
cd klayout/

# change to my working branch

git checkout -b guojin0.26
./build.sh -j4 -debug -without-qt -prefix /persist/bin/klayout
```


# Summary

```bash
docker build --network=host -t myklayout:bash -f dockerfile.fedora .
# mount directly
docker run --net=host --name kmd -v /Users/dekura/chen/docker-persist/klayout-persist:/persist -it myklayout:bash /bin/bash

# attach into docker images
cd /persist
mkdir bin && cd bin && mkdir klayout && cd ..

cd bin
echo "export PATH=/persist/bin/klayout:$PATH" >> mybashrc

cd /persist
# clone klayout
git clone https://github.com/dekura/klayout.git
cd klayout/

# check out to my working branch
git checkout -b guojin0.26

# for release build
./build.sh -j4 -prefix /persist/bin/klayout-release

# for debug, you don't need it when you only need to build.
./build.sh -j4 -debug -prefix /persist/bin/klayout

# for non-gui build
./build.sh -j4 -debug -without-qt -prefix /persist/bin/klayout-non-gui
```



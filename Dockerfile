FROM ubuntu:17.10

RUN apt-get update
RUN apt-get install -y qt5-default libjack-jackd2-dev cmake libboost-dev libsndfile1-dev libmad0-dev libvorbisfile3 liblo-dev libtagc0-dev liblilv-dev vamp-plugin-sdk libvamp-hostsdk3v5 qtcreator



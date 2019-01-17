FROM ubuntu:18.10 as n2k-ubuntu-dev

WORKDIR /app

# Tests does work on ascii if not set
env PYTHONIOENCODING=utf_8

RUN apt-get update && apt-get install --no-install-recommends -y \
	ca-certificates \
	bsdtar \
	clang \
	libev-dev \
	libjansson-dev \
	libmicrohttpd-dev \
	librdkafka-dev \
	libyajl-dev \
	make \
	python3-distutils \
	wget \
	zlib1g-dev \
	&& apt-get clean

RUN wget https://bootstrap.pypa.io/get-pip.py -O get-pip.py && \
    python3 get-pip.py && \
    rm -f get-pip.py && \
    pip3 install \
	colorama \
	ijson \
	pykafka \
	pytest \
	pytest-xdist \
	requests \
	timeout-decorator

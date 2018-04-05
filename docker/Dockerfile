FROM alpine:3.7 as n2k-base

# Common C runtime libraries
RUN apk add --no-cache librdkafka jansson zlib

# n2k libraries
RUN apk add --no-cache yajl libmicrohttpd libev

WORKDIR /app

#
# Devel
#
FROM n2k-base as n2k-dev

# ca-certificates: for wget bootstrapping
# {ncurses,slang,expat}-dev: deps for xml-coreutils
RUN apk add --no-cache \
	bash \
	bsd-compat-headers \
	build-base \
	ca-certificates \
	cgdb \
	cmake \
	expat-dev \
	git \
	jansson-dev \
	libarchive-tools \
	libev-dev \
	libmicrohttpd-dev \
	librdkafka-dev \
	ncurses-dev \
	openssl \
	python3-dev \
	slang-dev \
	yajl-dev \
	zlib-dev

RUN	apk add --no-cache \
		--repository http://dl-cdn.alpinelinux.org/alpine/edge/testing/ \
		lcov \
		valgrind

RUN	update-ca-certificates
RUN	pip3 install \
		requests \
		pykafka \
		pytest

#
# RELEASE
#
FROM n2k-base

# envsubst (gettext is very big! install only binary & dependency)
RUN apk add --no-cache libintl gettext && \
        cp /usr/bin/envsubst /usr/local/bin/envsubst && \
        apk del gettext

COPY \
	docker/release/config.json.env \
	docker/release/n2k_setup.sh \
	n2kafka \
	/app/

ENTRYPOINT /app/n2k_setup.sh

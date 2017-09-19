FROM alpine:3.6

# envsubst (gettext is very big! install only binary & dependency)
RUN apk add --no-cache libintl gettext && \
        cp /usr/bin/envsubst /usr/local/bin/envsubst && \
        apk del gettext

# Common C runtime libraries
RUN apk add --no-cache librdkafka jansson zlib

# n2k libraries
RUN apk add --no-cache yajl libmicrohttpd libev

define(builddeps,bash build-base ca-certificates librdkafka-dev \
		jansson-dev zlib-dev libarchive-tools openssl bsd-compat-headers cmake \
		cgdb valgrind ncurses-dev slang-dev expat-dev yajl-dev \
		libmicrohttpd-dev libev-dev curl-dev git)dnl
dnl
ifelse(version,devel,
RUN apk add --no-cache builddeps && \
	apk add --no-cache \
		--repository http://dl-cdn.alpinelinux.org/alpine/edge/testing/ lcov && \
	update-ca-certificates,
COPY releasefiles /app/
ENTRYPOINT /app/n2k_setup.sh)


# ca-certificates: for wget bootstrapping
# ncurses - expat: deps for xml-coreutils

WORKDIR /app

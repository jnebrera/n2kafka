FROM alpine:3.6

# Common C runtime libraries
RUN apk add --no-cache librdkafka jansson zlib

# n2k libraries
RUN apk add --no-cache yajl libmicrohttpd libev


# ca-certificates: for wget bootstrapping
# ncurses - expat: deps for xml-coreutils
RUN apk add --no-cache bash build-base ca-certificates librdkafka-dev \
		jansson-dev zlib-dev libarchive-tools openssl bsd-compat-headers cmake \
		cgdb valgrind ncurses-dev slang-dev expat-dev yajl-dev \
		libmicrohttpd-dev libev-dev curl-dev && \
	apk add --no-cache \
		--repository http://dl-3.alpinelinux.org/alpine/edge/testing/ lcov && \
	update-ca-certificates

WORKDIR /app

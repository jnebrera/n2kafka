#FROM alpine:3.8 as n2k-base
FROM alpine@sha256:46e71df1e5191ab8b8034c5189e325258ec44ea739bba1e5645cff83c9048ff1 as n2k-base

RUN apk add --no-cache \
	jansson=2.11-r0 \
	libev=4.24-r0 \
	yajl=2.1.0-r0 \
	zlib=1.2.11-r1

WORKDIR /app

#
# Devel
#
FROM n2k-base as n2k-dev

# Need to install python-dev before or it will be installed from edge repository
RUN	apk add --no-cache python3-dev

# Need to install these before or they will be upgraded from base repository
# gcc dependencies: binutils, gmp, isl, mpfr3, mpc1
RUN	apk add --no-cache \
		--repository http://dl-cdn.alpinelinux.org/alpine/edge/main/ \
		binutils=2.31.1-r2 \
		cgdb \
		fortify-headers=1.0-r0 \
		gdb=8.2-r1 \
		g++=8.2.0-r2 \
		gcc=8.2.0-r2 \
		gmp=6.1.2-r1 \
		valgrind=3.14.0-r0

# ca-certificates: for wget bootstrapping
# g++ dependencies: libc-dev
# lcov dependencies: perl
RUN apk add --no-cache \
	bash \
	bsd-compat-headers=0.7.1-r0 \
	ca-certificates \
	file \
	git \
	gnutls-dev=3.6.2-r0 \
	isl=0.18-r0 \
	jansson-dev=2.11-r0 \
	libarchive-tools \
	libc-dev=0.7.1-r0 \
	libev-dev=4.24-r0 \
	make \
	mpc1=1.0.3-r1 \
	mpfr3=3.1.5-r1 \
	musl-dev=1.1.19-r10 \
	openssl=1.0.2q-r0 \
	perl \
	py3-yaml \
	yajl-dev=2.1.0-r0 \
	zlib-dev=1.2.11-r1

# alpine edge main/testing lcov does not support gcc-8 coverage format (See
# a5dd9529f9232b8d901a4d6eb9ae54cae179e5b3 lcov commit).
RUN	wget -O lcov.tgz\
		https://api.github.com/repos/linux-test-project/lcov/tarball/28675dc7564aaa1ad231a7ac23106512a3956d68 && \
	printf '7385563633347c7d6bcc055d118109e6ab1414bba4fcf17a109bbdaa92f46f51  lcov.tgz' | sha256sum -c && \
	tar xvzpf lcov.tgz && \
	(cd linux-test-project-lcov-28675dc/; \
		sed -i 's%PREFIX  := /usr/local%PREFIX  := /usr%' Makefile && \
		make install; ) && \
	rm -f lcov.tgz

RUN	update-ca-certificates
RUN	pip3 install --upgrade pip && pip3 install \
		colorama \
		pykafka \
		pytest \
		pytest-xdist \
		requests

#
# RELEASE
#
FROM n2k-base AS release

# envsubst (gettext is very big! install only binary & dependency)
RUN apk add --no-cache \
	libintl=0.19.8.1-r2 \
	gettext=0.19.8.1-r2 \
	&& \
	cp /usr/bin/envsubst /usr/local/bin/envsubst \
	&& \
	apk del gettext

COPY \
	docker/release/config.json.env \
	docker/release/n2k_setup.sh \
	n2kafka \
	/app/

ENTRYPOINT ["/app/n2k_setup.sh"]

FROM release AS bin_wrapper
ARG INSTALL_PKGS
ARG EXEC_WRAPPER

WORKDIR /app
COPY src Makefile.config /app/

RUN apk add --no-cache cgdb && \
	sed -i 's%^exec \./n2kafka%exec ${EXEC_WRAPPER} ./n2kafka%' /app/n2k_setup.sh

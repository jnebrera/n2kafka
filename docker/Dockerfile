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

# ca-certificates: for wget bootstrapping
# gcc dependencies: binutils, isl, libgomp, libatomic, mpfr3, mpc1
# g++ dependencies: libc-dev
RUN apk add --no-cache \
	bash \
	binutils=2.30-r5 \
	bsd-compat-headers=0.7.1-r0 \
	ca-certificates \
	cgdb \
	file \
	fortify-headers=0.9-r0 \
	gcc=6.4.0-r9 \
    g++=6.4.0-r9 \
	git \
	gnutls-dev=3.6.2-r0 \
	isl=0.18-r0 \
	jansson-dev=2.11-r0 \
	libarchive-tools \
	libatomic=6.4.0-r9 \
	libc-dev=0.7.1-r0 \
	libev-dev=4.24-r0 \
	libgomp=6.4.0-r9 \
	make \
	mpc1=1.0.3-r1 \
	mpfr3=3.1.5-r1 \
	musl-dev=1.1.19-r10 \
	openssl=1.0.2q-r0 \
	py3-yaml \
	python3-dev \
	yajl-dev=2.1.0-r0 \
	zlib-dev=1.2.11-r1

# Only in edge testing repo
RUN	apk add --no-cache \
		--repository http://dl-cdn.alpinelinux.org/alpine/edge/testing/ \
		lcov

RUN	apk add --no-cache \
		--repository http://dl-cdn.alpinelinux.org/alpine/edge/main/ \
		valgrind=3.14.0-r0

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

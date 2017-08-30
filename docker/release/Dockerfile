FROM alpine:3.6

# envsubst (gettext is very big! install only binary & dependency)
RUN apk add --no-cache libintl gettext && \
        cp /usr/bin/envsubst /usr/local/bin/envsubst && \
        apk del gettext

# Common C runtime libraries
RUN apk add --no-cache librdkafka jansson zlib

# n2k libraries
RUN apk add --no-cache yajl libmicrohttpd libev

COPY n2kafka docker/release/config.json.env docker/release/n2k_setup.sh /app/
ENTRYPOINT /app/n2k_setup.sh


# ca-certificates: for wget bootstrapping
# ncurses - expat: deps for xml-coreutils

WORKDIR /app

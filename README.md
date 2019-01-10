# n2kafka

[![CircleCI](https://circleci.com/gh/wizzie-io/n2kafka/tree/master.svg?style=svg&circle-token=2cfc3260f560b7757d7d9b0e91105816de4cc5d0)](https://circleci.com/gh/wizzie-io/n2kafka/tree/master)

Network to kafka translator. It (currently) support conversion from tcp/udp raw
sockets and HTTP POST to kafka messages, doing message-processing if you need
to.

# Setup
To use it, you only need to do a typical `./configure && make && make install`

# Usage
## Basic usage

In order to send raw tcp messages from port `2056`, to `mymessages` topic, using
`40` threads, you need to use this config file:
```json
{
	"listeners":[
		{"proto":"tcp","port":2056,"num_threads":40}
	],
	"brokers":"localhost",
	"topic":"mymessages"
}
```

And launch `n2kafka` using `./n2kafka <config_file>`. You can also use `udp` and
`http` as proto values. If you want to listen in different ports, you can add as
many listeners as you want.

## Recommended config parameters
You can also use this parameters in config json root to improve n2kafka
behavior:
- `"blacklist":["192.168.101.3"]`, that will ignore requests of this directions
  (useful for load balancers)
- All
  [librdkafka](https://github.com/edenhill/librdkafka/blob/master/CONFIGURATION.md).
  options. If a config option starts with `rdkafka.<option>`, `<option>` will be
  passed directly to librdkafka config, so you can use whatever config you need.
  Recommended options are:
  * `"rdkafka.socket.max.fails":"3"`
  * `"rdkafka.socket.keepalive.enable":"true"`

You can also set librdkafka configurations using environment variables of the
form `RDKAFKA_<variable_key>=<variable_value>`, replacing dots (`.`) with
underscores (`_`), like:

```bash
$ env RDKAFKA_QUEUE_BUFFERING_MAX_MS=10 RDKAFKA_SOCKET_MAX_FAILS=3 n2kafka`
```

## Listeners
If the information does not come via raw sockets, different listeners can read
from different protocols. The other listener currently supported is the HTTP
listener that, by default, will just read raw POSTs or GETs and will forward
them to the decoder.

### Socket listener.
This will open a raw Internet socket and will send all the data to the kafka
broker. Take into account that the data will be spliced at random points in
streams sockets, like TCP, because of the nature of the connection (not
message-oriented). A decoder can fit it in a later stage.

This listener support the next options:
- proto (string): Protocol to listen (tcp or udp).
- port (integer): Port to listen.
- num_threads (integer): Number of threads to process messages.
- tcp_keepalive (bool): n2kafka will send TCP keepalives probes to not to
  close the connection because of timeout (and will close it if it can't reach
  the other end).
- mode (string): Client multiplexing mode. See
  [Client multiplexing](client-multiplexing).

### HTTP listener
HTTP listener admits the next configuration:
- port (integer): Port in what listen
- mode (string): Client multiplexing mode. See
  [Client multiplexing](client-multiplexing).
- num_threads (integer): Number of threads to multiplex connections.
- connection_memory_limit (integer): Memory limit of one connection.
- connection_limit (integer): Connections limit
- connection_timeout (integer): Idle timeout for a connection, in seconds.
- per_ip_connection_limit (integer): Limit the number of connections per IP.
- https_cert_filename (string): Certificate to export. Needs to come with
  `https_key_filename`. The file permissions must not include other's
  read/write (i.e., needs to be XX0).
- https_key_filename (string): Private key to encrypt HTTP connection. Needs
  to come with `https_cert_filename`. The file permissions must not include
  other's read/write (i.e., needs to be XX0).
- https_key_password (string): Password to use to decrypt the private key.
- https_clients_ca_filename (string): CA that the clients uses in the
  client side certificate to autenticate themselves.

For a deeper understanding of each value's implication, you can go to
[libmicrohttpd reference manual](https://www.gnu.org/software/libmicrohttpd/manual/html_node/microhttpd_002dconst.html).

#### SSL/TLS
To configure https server, you need to specify both `https_cert_filename` and
and `https_key_filename`. On the other hand, to authenticate clients, all
client's certificate must be signed by the same CA, and you need to specify it
with `https_clients_ca_filename` parameter.

These same parameters can be set using `HTTP_TLS_KEY_FILE`,
`HTTP_TLS_CERT_FILE`, `HTTP_TLS_KEY_PASSWORD` and `HTTP_TLS_CLIENT_CA_FILE`.

For a quick test, you can generate both using:
```bash
openssl req \
        -newkey rsa:2048 -nodes -keyout key.pem \
        -x509 -days 3650 -subj '/CN=localhost/' -extensions SAN \
        -config <(cat /etc/ssl/openssl.cnf - <<-EOF
          [req]
          distinguished_name = req_distinguished_name

          [req_distinguished_name]

          [ SAN ]
          subjectAltName=DNS:localhost
          EOF
                ) \
        -out certificate.pem
```

And use the next config file:
```json
{
  "brokers": "kafka",
  "listeners": [{
    "proto": "http",
    "port": 7980,
    "decode_as": "zz_http2k",
    "https_key_filename": "key.pem",
    "https_cert_filename": "certificate.pem",
  }]
}
```

Or set the next environment variables:
```bash
$ env HTTP_TLS_CERT_FILE=certificate.pem HTTP_TLS_KEY_PASSWORD=key.pem ./n2kafka <n2kafka_config_filename>
```

After that, connections with plain HTTP must be rejected:
```bash
$ curl  -d '{"test":1}' http://localhost:7980/v1/data/abc
curl: (52) Empty reply from server
```

And connection with https should be accepted:
```bash
curl  -vd '{"test":1}' https://localhost:7980/v1/data/abc
...
< HTTP/1.1 200 OK
```

If you want to enable Client CA verification too, you can generate as many
clients key/certificate pairs as you need changing `-keyout` and `-out`
parameters of the openssl command. After that, use the previously specified
configuration options or environments. Curl command can be used to test client
certificate verification with:

```bash
curl -vk --key client-key.pem --cert client-certificate.pem -d '{"test":1}' https://localhost:7980/v1/data/abc
...
< HTTP/1.1 200 OK
```

Curl requests with no `--key` and `--cert` should be rejected by the servers.

#### Content-encoding
You can send 'gzipped' and 'deflated' data to n2kafka http listener as long as
the corresponding Content-Encoding http header comes with it:

```bash
$ gzip<<<'{"test":1}'|curl -H 'Content-Encoding: gzip' --data-binary @- 'http://localhost:40093/v1/data/abc'
```

or:

```bash
$ zpipe<<<'{"test":1}'|curl -H 'Content-Encoding: deflate' --data-binary @- 'http://localhost:40093/v1/data/abc'
```

You can download `zpipe` executable from
[zlib project](https://github.com/madler/zlib/blob/master/examples/zpipe.c).

Actual encoding (`gzip` vs `deflate`) will be detected as long as one of the
headers is present in the HTTP request, i.e., they are interchangeable.

### Clients multiplexing
Current listeners support the next client's multiplexing methods:
  * "thread_per_connection": One thread is created for every connection. It
    looks like a good idea, but the thread creation cost can be bigger than
    process costs, especially for short-lived connections.
  * "select": Use select syscall.
  * "poll": Use poll syscall. Recommended if you have few clients and
    long-lived connections.
  * "epoll": Use epoll syscall. Recommended if you have a lot of different
    clients.

The multiplexing modes are set using `mode` listener option

## Decoders
In order to treat received data, you can use one of the provided decoders. The
most useful is http2k decoder, that allows to send to many topics. You can get
more info about it [here](src/decoder/zz_http2k/README.md).

Another useful decoder is [Meraki](src/decoder/meraki/README.md). You can send
your meraki location reports to n2kafka and it will forward to a kafka broker.

# Docker setup
If you want an easy setup, you can use n2kafka docker image provided at
gcr.io/wizzie-registry/n2kafka. This container provides default
[http2k](src/decoder/zz_http2k/README.md) decoder
listening in port `7980`, and send data to `kafka` bootstrap broker.

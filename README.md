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
  * `"delivery.report.only.error":"true"`

## Decoders
In order to treat received data, you can use one of the provided decoders. The
most useful is http2k decoder, that allows to send to many topics. You can get
more info about it [here](src/decoder/zz_http2k/README.md).

# Docker setup
If you want an easy setup, you can use n2kafka docker image provided at
gcr.io/wizzie-registry/n2kafka. This container provides default http2k decoder
listening in port `7980`, and send data to `kafka` bootstrap broker.

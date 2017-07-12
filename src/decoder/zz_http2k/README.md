# Wizzie decoder

ZZ decoder allows you to inject many JSON/HTTP in the same HTTP POST, and send
them to many different topics using the same n2kafka instance.

## HTTP POST format
HTTP POST format has the next restrictions:
- URL needs to be `v1/data/<topic>`
- It reads the next POST parameters:
  * `X-Consumer-ID`: Client used. It's NOT a kafka consumer id, but wizzie
    client ID. Output topic will be ${X-Consumer-ID}_${last URL part}. If not
    included, it will be sent to the last part of URL.

That way, you can send this POST to zz decoder:

```
POST /v1/data/topic1 HTTP/1.1
X-Consumer-ID:abc
Content-Length: 10
Content-Type: text/json

{"test":1}
```

Using
`curl -v http://<n2kafka host>/v1/data/topic1 -H 'X-Consumer-ID:abc' -d '{"test":1}'`

And message `{"test":1}` will be sent to topic `abc_topic1`. Similarly, you can
send `{"test":1}{"test":2}` in POST body and two different messages,
`{"test":1}` and `{"test":2}` will be sent to topic `abc_topic1`.

If you do not use HTTP `X-Consumer-ID` header:
`curl -v http://<n2kafka host>/v1/data/topic1 -d '{"test":1}'`

Output will be sent to topic `topic1`.

## Deflate compression

You can send compressed messages to ZZ decoder using `Content-Encoding: deflate` in
POST header. Messages has to be compressed with zlib library (http://zlib.net/).

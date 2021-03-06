# Wizzie decoder

ZZ decoder allows you to inject many JSON or XML objects in the same HTTP
POST, and send them to many different topics using the same n2kafka instance.

## HTTP POST format
### JSON over HTTP
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

### XML over HTTP
Similarly, http2k decoder does support XML, as long as proper HTTP content
type is present in request: http2k transforms it into a JSON message. With
curl, you can add it with `-H 'Content-type: xml` (actually, anything that
ends with `xml` is treated as an XML, allowing to use typical `application/xml`
and `text/xml`). Since XML is a little bit more complex than JSON, the
following transformation rules apply:

*The XML root object is the JSON root object:*

These two simple XML objects:

```xml
<simple />'
'<simple></simple>'
```

Produce the next JSON output:
```json
{"tag":"simple"}
```

With only the tag name in them.

*XML attributes:*

Attributes add an `attributes` array:
```xml
<attributes attr1="one"></attributes>
```

```json
{"tag":"attributes","attributes":{"attr1":"one"}}
```

*XML text*

Text inside a XML tag adds a `text` JSON key:
```xml
'<ttext>text1 text1</ttext>'
```

```json
{"tag":"ttext","text":"text1 text1"}
```

*XML text after a tag*
Text after tag is appended at its `tail` key:

```xml
<root><child1 a="r">t1</child1>tt<child2>t2</child2></root>
```

```json
{"tag":"root","children":[
  {"tag":"child1","attributes":{"a":"r"},"text":"t1","tail":"tt"},
  {"tag":"child2","text":"t2"}
]}
```

## Deflate compression

You can send compressed messages to ZZ decoder using `Content-Encoding: deflate` in
POST header. Messages has to be compressed with zlib library (http://zlib.net/).

## HTTPS (HTTP over TLS)
You can enable HTTPs with the proper key and cert file with the next
parameters:

https_key_filename
: Private key to use. It can't be readable by "others" group.

https_key_password
: Password to decode private key (optional). If the password is contained in a
file, you can use "@/file/path" format.

https_cert_filename
: Certificate to export.

You can use `HTTP_TLS_KEY_FILE`, `HTTP_TLS_CERT_FILE` and
`HTTP_TLS_KEY_PASSWORD` for configure these options too.

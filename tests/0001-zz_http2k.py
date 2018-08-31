#!/usr/bin/env python3

import itertools
import json
import requests
import random
import pytest
import zlib
from n2k_test_json import FuzzyJSON
from n2k_test import \
                     HTTPMessage, \
                     HTTPPostMessage, \
                     main, \
                     TestN2kafka, \
                     valgrind_handler


@pytest.fixture(params=[None, 'deflate', 'unknown_content_encoding'])
def content_encoding(request):
    return request.param


def strip_apart(base, min_pieces=10, max_pieces=30):
    ''' Strip apart a string at random positions in random number of pieces
    > min_pieces'''
    base_len = len(base)
    cut_positions = sorted([0] + random.sample(
        range(base_len),
        random.randint(min_pieces, max_pieces))
        + [base_len])
    return [base[cut_positions[i]:cut_positions[i + 1]]
            for i in range(len(cut_positions) - 1)]


class TestHTTP2K(TestN2kafka):
    def _base_http2k_test(self,
                          child,
                          messages,
                          kafka_handler,
                          valgrind_handler,
                          base_config_add={}):
        ''' Base n2kafka test

        Arguments:
          - child: Child string to execute
          - messages: Messages to test
          - kafka_handler: Kafka handler to use
          - valgrind_handler: Valgrind handler if any
          - base_config_add: Config to add (override).
        '''
        base_config = {
            **{
              "listeners": [{
                  'proto': 'http',
                  'decode_as': 'zz_http2k'
              }]
            },
            **base_config_add,
        }

        t_locals = locals()
        self.base_test(base_config=base_config,
                       child_argv_str=child,
                       **{key: t_locals[key]
                          for key in ['messages',
                                      'kafka_handler',
                                      'valgrind_handler']})

    def test_http2k_url(self, kafka_handler, valgrind_handler, child):
        ''' Test URL behavior '''
        TEST_MESSAGE = '{"test":1}'
        used_topic = TestN2kafka.random_topic()

        # From HTTP RFC, separators that URL can use for parameters
        url_tokens = ';?:@=&'
        url_params = [tok + 'param' for tok in url_tokens]
        valid_uris = ['/v1/data/' + used_topic + param
                      for param in [''] + url_params]
        invalid_uris = ["", "/v1/data", "/v1/data/", "/", "/noversion",
                        "/v2/topic", "/v1/", "?v1/topic"]

        test_messages = [
            HTTPPostMessage(uri=t_uri, data=TEST_MESSAGE,
                            expected_response_code=200,
                            expected_kafka_messages=[
                                {'topic': used_topic,
                                    'messages': [TEST_MESSAGE]}
                            ]
                            )
            for t_uri in valid_uris
        ] + [
            # TODO connection should be answered properly, with a bad method
            # response
            HTTPPostMessage(uri=t_uri, data=TEST_MESSAGE,
                            expected_exception_type=requests.exceptions.ConnectionError
                            )
            for t_uri in invalid_uris
        ]

        self._base_http2k_test(child=child,
                               messages=test_messages,
                               kafka_handler=kafka_handler,
                               valgrind_handler=valgrind_handler)

    def test_http2k_client(self, kafka_handler, valgrind_handler, child):
        ''' Test ZZ client behavior. http2k expect client as X-CONSUMER-ID http
        header, and it needs to forward messages to that client '''
        TEST_MESSAGE = '{"test":1}'
        used_topic = TestN2kafka.random_topic()
        used_client = TestN2kafka.random_topic()  # Let's use the same format

        # Consumer ID parameter should be case insensitive
        consumer_id_post_key = 'X-Consumer-ID'
        consumer_id_post_keys = [
            consumer_id_post_key,
            # TODO consumer_id_post_key.upper(),
            # TODO consumer_id_post_key.lower(),
            # TODO consumer_id_post_key.title()
        ]

        test_messages = [
            HTTPPostMessage(
                uri='/v1/data/' + used_topic,
                headers={t_consumer_id_post_key: used_client},
                data=TEST_MESSAGE,
                expected_response_code=200,
                expected_kafka_messages=[
                    {'topic': used_client + '_' + used_topic,
                     'messages': [TEST_MESSAGE]}
                ]
            )
            for t_consumer_id_post_key in consumer_id_post_keys]

        self._base_http2k_test(child=child,
                               messages=test_messages,
                               kafka_handler=kafka_handler,
                               valgrind_handler=valgrind_handler)

    def test_http2k_invalid_request(self,
                                    kafka_handler,
                                    valgrind_handler,
                                    child):
        ''' Test ZZ client behavior. http2k expect client as X-CONSUMER-ID http
        header, and it needs to forward messages to that client '''
        test_messages = [
            HTTPMessage(http_method,
                        uri='/v1/data/unused_topic',
                        data='',
                        expected_response_code=405,
                        expected_response='',
                        )
            for http_method in (requests.get, requests.put, requests.delete)]
        # TODO for data in ('', '{"test":1}')]

        self._base_http2k_test(child=child,
                               messages=test_messages,
                               kafka_handler=kafka_handler,
                               valgrind_handler=valgrind_handler)

    def test_http2k_unexpected_close(self,
                                     kafka_handler,
                                     valgrind_handler,
                                     child):
        used_topic = TestN2kafka.random_topic()
        test_message = HTTPPostMessage(uri='/v1/data/' + used_topic,
                                       data=[{'chunk': '{"test":1}{"te',
                                              'kafka_messages': {'topic': used_topic,
                                                                 'messages': ['{"test":1}']}},
                                             {'raise': TestN2kafka.CloseConnectionException},
                                             ],
                                       expected_exception_type=TestN2kafka.CloseConnectionException,
                                       )

        self._base_http2k_test(child=child,
                               messages=[test_message],
                               kafka_handler=kafka_handler,
                               valgrind_handler=valgrind_handler)

    @staticmethod
    def __http2k_decoder_response(
                                queued_messages, json_error, right_here_text):
        '''
         @brief      Creates a http2k decoder response

         @param      self             The object
         @param      queued_messages  The kafka success queued messages
         @param      right_here_text  The YAJL "right here" error string

         @return     Text response
        '''
        ret = '{{"messages_queued":{},"json_decoder_error":'.format(
                                                           queued_messages) + \
            '''"{json_error}
                               {right_here_text}
                     (right here) ------^\n"}}'''.replace("\n", "\\n").format(
                                               json_error=json_error,
                                               right_here_text=right_here_text)

        return ret

    def test_http2k_messages(self,
                             kafka_handler,
                             child,
                             valgrind_handler,
                             content_encoding):
        ''' Test http2k different messages behavior '''
        used_topic = TestN2kafka.random_topic()
        jsons = ({
            'string': 'mystr' + str(i),
            'number': i,
            'array': list(range(3)),
            'true': True,
            'false': False,
            'null': None
        } for i in itertools.count())

        two_messages = [json.dumps(j) for j in itertools.islice(jsons, 2)]
        json_object_child = json.dumps({**next(jsons), 'object': next(jsons)})

        fuzzy_jsons = [json.dumps(FuzzyJSON(10,
                                            FuzzyJSON.JsonTypes.OBJECT).value)
                       for _ in range(20)]
        fuzzy_jsons_str = ''.join(fuzzy_jsons)

        base_args = {
            'uri': '/v1/data/' + used_topic,
            'expected_response_code': 200,
            'expected_response': '',
        }

        send_garbage_expected_stdout_regex = None
        if content_encoding:
            base_args['headers'] = {'Content-Encoding': content_encoding}
            if content_encoding == 'deflate':
                base_args['deflate_request'] = True
                # TODO n2kafka should return proper HTTP error in this case
                send_garbage_expected_stdout_regex = [
                 'from client localhost:{listener_port}: '
                 '{{"error":"deflated input is not conforming to the '
                 'zlib format"}}']

        test_messages = [
            # POST with no messages
            HTTPPostMessage(**{**base_args,
                               'data': '',
                               'expected_kafka_messages': [
                                   {'topic': used_topic, 'messages': []}
                               ]}
                            ),

            # POST with two messages, using all possible JSON types except
            # object. They have to be sent as two separated kafka messages.
            HTTPPostMessage(**{**base_args,
                               'data': ''.join(two_messages),
                               'expected_kafka_messages': [
                                   {'topic': used_topic, 'messages': two_messages}
                               ]}
                            ),

            # Kafka message with a child
            HTTPPostMessage(**{**base_args,
                               'data': json_object_child,
                               'expected_kafka_messages': [
                                   {'topic': used_topic, 'messages': [
                                       json_object_child]}
                               ]}
                            ),

            # HTTP POST in chunks
            HTTPPostMessage(uri='/v1/data/' + used_topic,
                            data=[
                                {
                                    'chunk': t_json.encode(),
                                    'kafka_messages': {
                                        'messages': [t_json], 'topic':used_topic
                                    }
                                } for t_json in two_messages
                            ],
                            expected_response_code=200,
                            ),

            # HTTP POST in chunks: Split in JSON key
            HTTPPostMessage(**{**base_args,
                               'data': [
                                   {
                                       'chunk': '{"test":1}{"te',
                                       'kafka_messages': {
                                           'topic': used_topic, 'messages': ['{"test":1}']
                                       }
                                   }, {
                                       'chunk': 'st":2}{"test":3}',
                                       'kafka_messages': {
                                           'topic': used_topic,
                                           'messages': [
                                               '{"test":2}', '{"test":3}'
                                           ]
                                       }
                                   },
                               ]}
                            ),

            # Kafka invalid message:
            HTTPPostMessage(**{
              **base_args,
              'data': '{"test":invalid}',
              'expected_response_code': 400,
              'expected_response':
              TestHTTP2K._TestHTTP2K__http2k_decoder_response(
                queued_messages=0,
                json_error='lexical error: invalid char in json text.',
                right_here_text='{\\"test\\":invalid}'),
            }),
        ] + [
            # Close JSON message earlier
            HTTPPostMessage(**{
                **base_args,
                'data': '}' * i + '{"test":"i"}',
                'expected_response_code': 400,
                'expected_response':
                TestHTTP2K._TestHTTP2K__http2k_decoder_response(
                  queued_messages=0,
                  json_error='parse error: unallowed token at this point in'
                        ' JSON text',
                  right_here_text=8*' ' + '}'*i + '{\\"test\\":\\"i\\"}'),
                  })
            for i in range(1, 4)
        ] + [
            # Fuzzy data
            HTTPPostMessage(**{**base_args, 'data': fuzzy_jsons_str,
                               'expected_kafka_messages': [
                                   {'topic': used_topic, 'messages': fuzzy_jsons}
                               ]}
                            ),

            # Chunked fuzzy data
            HTTPPostMessage(**{**base_args, 'data': [{'chunk': i}
                                                    for i in strip_apart(fuzzy_jsons_str)],
                              'expected_kafka_messages': [
               {'topic': used_topic, 'messages': fuzzy_jsons}
            ]}
            ),

            # Pure garbage!
            HTTPPostMessage(**{
                **base_args,
                'deflate_request': False,  # Send zlib garbage
                'expected_response_code': 400,
                # TODO 'expected_response': 'abc',
                'expected_stdout_regex': send_garbage_expected_stdout_regex,
                'data': bytearray(random.getrandbits(8) for _ in range(20)),
            }),

            # More zlib garbage does not raise another error to console, but it
            # should return error in console.
            HTTPPostMessage(**{
                **base_args,
                'deflate_request': False,  # Send zlib garbage
                'expected_response_code': 400,
                # TODO 'expected_response': 'abc',
                'expected_stdout_regex': None,
                'data': bytearray(random.getrandbits(8) for _ in range(20)),
            }),

            # deflate data sent with no dict
            HTTPPostMessage(**{
                **base_args,
                'deflate_request': False,  # Already deflated
                'headers': {'Content-Encoding': 'deflate'},  # Already deflated
                'data':
                    zlib.compressobj(zdict=b'hello').compress(b'{"test":1}'),
                'expected_response_code': 400,
                'expected_stdout_regex': [
                  'libz deflate error: a dictionary is need'
                ]
            }),
        ]

        self._base_http2k_test(child=child,
                               messages=test_messages,
                               kafka_handler=kafka_handler,
                               valgrind_handler=valgrind_handler)

    def test_http2k_full_queue(self,
                               kafka_handler,
                               valgrind_handler,
                               child):
        used_topic = TestN2kafka.random_topic()
        test_message = HTTPPostMessage(
            uri='/v1/data/' + used_topic,
            data=[{'chunk': '{"test":1}'*1000,
                   'kafka_messages': {'topic': used_topic,
                                      'messages': ['{"test":1}']}},
                  {'raise': TestN2kafka.CloseConnectionException},
                  ],
                expected_exception_type=TestN2kafka.CloseConnectionException,

                                       )
        self._base_http2k_test(child=child,
                               messages=[test_message],
                               kafka_handler=kafka_handler,
                               valgrind_handler=valgrind_handler,
                               base_config_add={
                                'rdkafka.queue.buffering.max.messages': '3'})

    def test_http2k_noautocreate_topic(self,
                                       kafka_handler,
                                       valgrind_handler,
                                       child):
        used_topic = TestN2kafka.random_topic()
        test_message = HTTPPostMessage(
            uri='/v1/data/' + used_topic,
            data=[{'chunk': '{"test":1}'}],
            expected_stdout_regex=[
                     'Broker: Unknown topic or partition'
                   ],
        )
        self._base_http2k_test(child=child,
                               messages=[test_message],
                               kafka_handler=kafka_handler,
                               valgrind_handler=valgrind_handler,
                               base_config_add={
                                'brokers': 'kafka_noautocreatetopic',
                                'rdkafka.queue.buffering.max.messages': '3'})

    # TODO send compressed data, and cut the connection without sending
    # Z_FINISH


if __name__ == '__main__':
    main()

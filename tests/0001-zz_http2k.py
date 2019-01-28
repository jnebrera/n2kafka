#!/usr/bin/env python3

#
# Copyright (C) 2018-2019, Wizzie S.L.
# Author: Eugenio Perez <eupm90@gmail.com>
#
# This file is part of n2kafka.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

__author__ = "Eugenio Perez"
__copyright__ = "Copyright (C) 2018-2019, Wizzie S.L."
__license__ = "AGPL"
__maintainer__ = "Eugenio Perez"
__email__ = "eperez@wizzie.io"
__status__ = "Production"

import copy
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
                     TestN2kafka

from n2k_test import valgrind_handler  # noqa: F401


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


def _random_case(s):
    return ''.join(random.random() > 0.5 and x.upper() or x for x in s)


class TestHTTP2K(TestN2kafka):
    def _base_http2k_test(self,  # noqa: F811
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

    def test_http2k_url(self,  # noqa: F811
                        kafka_handler,
                        valgrind_handler,
                        child):
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
            HTTPPostMessage(
              uri=t_uri,
              data=TEST_MESSAGE,
              expected_exception_type=requests.exceptions.ConnectionError
              )
            for t_uri in invalid_uris
        ]

        self._base_http2k_test(child=child,
                               messages=test_messages,
                               kafka_handler=kafka_handler,
                               valgrind_handler=valgrind_handler)

    def test_http2k_client(self,  # noqa: F811
                           kafka_handler,
                           valgrind_handler,
                           child):
        ''' Test ZZ client behavior. http2k expect client as X-CONSUMER-ID http
        header, and it needs to forward messages to that client '''
        TEST_MESSAGE = '{"test":1}'
        used_topic = TestN2kafka.random_topic()
        used_client = TestN2kafka.random_topic()  # Let's use the same format

        # Consumer ID parameter should be case insensitive
        consumer_id_post_key = 'X-Consumer-ID'
        consumer_id_post_keys = [
            consumer_id_post_key,
            consumer_id_post_key.upper(),
            consumer_id_post_key.lower(),
            consumer_id_post_key.title()
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

    def test_http2k_invalid_request(self,  # noqa: F811
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

    def test_http2k_unexpected_close(self,  # noqa: F811
                                     kafka_handler,
                                     valgrind_handler,
                                     child):
        used_topic = TestN2kafka.random_topic()
        test_message = HTTPPostMessage(
          uri='/v1/data/' + used_topic,
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

    @pytest.mark.parametrize('content_encoding',  # noqa: F811
                             [None,
                              'deflate',
                              'gzip',
                              'unknown_content_encoding'])
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

            wbits = zlib.MAX_WBITS  # default zlib library width bits
            use_gzip_wbits = 0x10

            if content_encoding == 'gzip':
                wbits += use_gzip_wbits

            if content_encoding in ('gzip', 'deflate'):
                base_args['compressor'] = zlib.compressobj(wbits=wbits)
                send_garbage_expected_stdout_regex = [
                 'from client localhost:{listener_port}: '
                 '{{"error":"deflated input is not conforming to the '
                 'zlib format"}}']

        test_messages_json = [
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
                                   {'topic': used_topic,
                                    'messages': two_messages}
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
                                        'messages': [t_json],
                                        'topic':used_topic
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
                                           'topic': used_topic,
                                           'messages': ['{"test":1}']
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
                                   {'topic': used_topic,
                                    'messages': fuzzy_jsons}
                               ]}
                            ),

            # Chunked fuzzy data
            HTTPPostMessage(**{**base_args,
                               'data': [{'chunk': i}
                                        for i in strip_apart(fuzzy_jsons_str)],
                               'expected_kafka_messages': [
                                {'topic': used_topic, 'messages': fuzzy_jsons}]
                               }
                            ),

            # Pure garbage!
            HTTPPostMessage(**{
                **base_args,
                'compressor': None,  # Send zlib garbage
                'expected_response_code': 400,
                # TODO 'expected_response': 'abc',
                'expected_stdout_regex': send_garbage_expected_stdout_regex,
                'data': bytearray(random.getrandbits(8) for _ in range(20)),
            }),

            # More zlib garbage does not raise another error to console, but it
            # should return error in console.
            HTTPPostMessage(**{
                **base_args,
                'compressor': None,  # Send zlib garbage
                'expected_response_code': 400,
                # TODO 'expected_response': 'abc',
                'expected_stdout_regex': None,
                'data': bytearray(random.getrandbits(8) for _ in range(20)),
            }),
        ]

        #
        # XML tests
        #

        # TODO: Try with application/json, text/json, text/xml
        xml_base_args = copy.copy(base_args)
        xml_base_args['headers'] = copy.copy(base_args['headers']) \
            if 'headers' in base_args else {}

        xml_base_args['headers']['Content-type'] = 'application/xml'

        test_messages_xml = [
          HTTPPostMessage(**{
            **xml_base_args,
            'data': data,
            'expected_response_code': 200,
            'expected_kafka_messages': [
               {'topic': used_topic,
                'messages': expected_kafka_messages},
            ]}
          ) for data, expected_kafka_messages in [
                # No actual message
                ('', []),
                (' ', []),
            ] + [
                # Simple tag
                (simple_data, ['{"tag":"simple"}'])
                for simple_data in ['<simple />', '<simple></simple>']
            ] + [
                # One attribute
                (one_attribute_data,
                 ['{"tag":"attributes","attributes":{"attr1":"one"}}'])
                for one_attribute_data in [
                    '<attributes attr1="one" />',
                    '<attributes attr1="one"></attributes>',
                ]
            ] + [
                # One attribute, two following tags
                (one_attribute_data,
                 ['{"tag":"attributes","attributes":{"attr1":"one"}}',
                  '{"tag":"attributes","attributes":{"attr2":"two"}}'])
                for one_attribute_data in [
                    '<attributes attr1="one" /><attributes attr2="two" />',
                    '<attributes attr1="one"></attributes>'
                    '<attributes attr2="two"></attributes>',
                ]
            ] + [
                # Two attributes, complex punctuation
                (two_attributes_data,
                 ['{"tag":"attributes","attributes":'
                  r'''{"attr1":"sq\"dq\"","attr2":"dq'sq'"}}'''])
                for two_attributes_data in [
                    r'''<attributes attr1='sq"dq"' attr2="dq'sq'">'''
                    '</attributes>',
                    r'''<attributes attr1='sq"dq"' attr2="dq'sq'" />'''
                 ]
            ] + [
                # Text
                ('<ttext>text1 text1</ttext>',
                 ['{"tag":"ttext","text":"text1 text1"}']),

                # Text + attr
                ('<ttext attr1="1">text1 text1</ttext>',
                 ['{"tag":"ttext","attributes":{"attr1":"1"},'
                  '"text":"text1 text1"}']),

                # Complex text
                ('<ttext>"new" año &lt; next year &amp; all that\n'
                 'newline and		tabs stuff</ttext>',
                 [r'{"tag":"ttext","text":"\"new\" año < next year & all that'
                  r'\nnewline and\t\ttabs stuff"}']),

                # Text on different tags
                ('<ttext attr1="1">text1 text1</ttext><ttext2>two</ttext2>',
                 ['{"tag":"ttext","attributes":{"attr1":"1"},'
                  '"text":"text1 text1"}',
                  '{"tag":"ttext2","text":"two"}']),
            ] + [
                # Child
                ('<root><child1></child1></root>',
                 ['{"tag":"root","children":[{"tag":"child1"}]}']),

                # Children
                ('<root><child1></child1><child2></child2></root>',
                 ['{"tag":"root","children":'
                  '[{"tag":"child1"},{"tag":"child2"}]}']),

                # Children with attributes
                ('<root><child1 a="r" /><child2 /></root>',
                 ['{"tag":"root","children":'
                  '[{"tag":"child1","attributes":{"a":"r"}},'
                  '{"tag":"child2"}]}']),

                # Children with attributes and text
                ('<root><child1 a="r">t1</child1><child2>t2</child2></root>',
                 ['{"tag":"root","children":'
                  '[{"tag":"child1","attributes":{"a":"r"},"text":"t1"},'
                  '{"tag":"child2","text":"t2"}]}']),

                # More complex tree
                ('<root>'
                 '<child1 a="r">t1<gchild1 /></child1><child2>t2</child2>'
                 '</root>',
                 ['{"tag":"root","children":'
                  '[{"tag":"child1","attributes":{"a":"r"},"text":"t1",'
                  '"children":[{"tag":"gchild1"}]},'
                  '{"tag":"child2","text":"t2"}]}']),
            ]
        ] + [
            HTTPPostMessage(**{
                **xml_base_args,
                'data': data,
                'expected_response_code': 400,
                'expected_kafka_messages': [
                   {'topic': used_topic,
                    'messages': expected_kafka_messages},
                ]})
            for (data, expected_kafka_messages) in [
                # No message should be queued
                ('>bad<simple />', [],),

                # One message is queued
                ('<simple />>bad', ['{"tag":"simple"}'],),

                # Too deep JSON
                ('<t>'*200, []),
            ]
        ]

        test_messages = test_messages_json + test_messages_xml

        if content_encoding == 'deflate':
            # deflate data sent with no dict
            test_messages.append(HTTPPostMessage(**{
                **base_args,
                'compressor': None,  # Already deflated
                'headers': {'Content-Encoding': 'deflate'},  # Already deflated
                'data':
                    zlib.compressobj(zdict=b'hello').compress(b'{"test":1}'),
                'expected_response_code': 400,
                'expected_stdout_regex': [
                  'libz deflate error: a dictionary is need'
                ]
            }))

        if content_encoding:
            # HTTP headers case-insensitive.
            case_insensitive_headers_args = {
                **base_args,
                'data': ''.join(two_messages),
                'expected_kafka_messages': [
                      {'topic': used_topic,
                       'messages': two_messages}
                  ]}

            try:
                c = case_insensitive_headers_args['compressor']
                del case_insensitive_headers_args['compressor']
            except KeyError:
                c = None

            case_insensitive_headers_args = [
                copy.deepcopy(case_insensitive_headers_args) for i in range(10)
            ]

            for m in case_insensitive_headers_args:
                del m['headers']['Content-Encoding']
                m['headers'][_random_case('Content-Encoding')] = \
                    _random_case(content_encoding)
                if c:
                    m['compressor'] = c

            test_messages.extend([HTTPPostMessage(**args) for args
                                  in case_insensitive_headers_args])

        self._base_http2k_test(child=child,
                               messages=test_messages,
                               kafka_handler=kafka_handler,
                               valgrind_handler=valgrind_handler)

    def test_http2k_full_queue(self,  # noqa: F811
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

    def test_http2k_noautocreate_topic(self,  # noqa: F811
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

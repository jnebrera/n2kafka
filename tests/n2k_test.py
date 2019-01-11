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

from tempfile import NamedTemporaryFile
import zlib
from socket import socket, AF_INET, SOCK_STREAM
from subprocess import Popen, PIPE
from concurrent.futures import ThreadPoolExecutor
from itertools import repeat
from queue import Queue
from valgrind import ValgrindHandler
import contextlib
import os
import shlex
import signal
import pytest
import requests
import json
import re


class TestChild(Popen):
    ''' Handles tested child and it's resources '''

    def __init__(self, args, exit_timeout=600, *popen_args, **popen_kwargs):
        # Public members
        self.__out_queue = Queue()

        # Config
        if isinstance(args, str):
            args = args.split()

        if len(args) == 0 or args[-1] != self._bin_path:
            args.append(self._bin_path)

        if self._config_file:
            args.append(self._config_file)

        super().__init__(args,
                         stdout=PIPE,
                         stderr=PIPE,
                         *popen_args,
                         **popen_kwargs)

        self.__exit_timeout = exit_timeout

    def readline(self, size=-1, t_timeout_seconds=5):
        ''' Obtain child stdout message. Size parameter is ignored '''
        return self.__out_queue.get(timeout=t_timeout_seconds)

    @staticmethod
    def __append_stream_to_queue(stream, queue):
        ''' Append file stream received data to queue forever. Intended use is
        to provide a reliable and non-blocking way of read from a stream object

        Due to Popen.stdout buffering, we have no way of know if readline()
        will success or not: selectors are broken in this scenario:
        - Produce 2 lines to f
        - Read 1 line of f
        - selector.select(f, READ) will return that there is no ready
          descriptor unless another line is produced.

        This way we have a reliable and non-blocking way to read a descriptor.
        '''
        for line in stream:
            try:
                queue.put(line.decode())  # Only interested if it's a clear
            except UnicodeDecodeError:
                pass

    def __enter__(self):
        # At this moment, subprocess is already launched
        queue_streams = [self.stdout, self.stderr]  # streams to queue
        self.__exit_stack = contextlib.ExitStack()
        streams_executor = self.__exit_stack.enter_context(
                            ThreadPoolExecutor(max_workers=len(queue_streams)))

        for stream in queue_streams:
            streams_executor.submit(
                                  TestChild._TestChild__append_stream_to_queue,
                                  stream, self.__out_queue)

        t_ret = super().__enter__()
        self.__exit_stack.push(super().__exit__)

        self._wait_child_ready()
        return t_ret

    def __exit__(self, exception_type, exception_value, exception_trace):
        self.send_signal(signal.SIGINT)
        self.wait(self.__exit_timeout)
        return self.__exit_stack.close()


class N2KafkaChild(TestChild):
    ''' Handles a n2kafka child and it's resources '''
    def __init__(
            self, argv, config_file, proto, port, *popen_args, **popen_kwargs):
        # super() variables
        self._bin_path = './n2kafka'
        self._config_file = config_file

        # Our state
        self.__proto = proto
        self.__port = str(port)

        super().__init__(args=argv, *popen_args, **popen_kwargs)

    @staticmethod
    def __clean_rdlog_message(message):
        LOG_INDEX = 4
        return message.split('|')[LOG_INDEX]

    def __wait_child_listener(self, t_timeout_seconds):
        # Todo make decorator for receive from kafka too
        MESSAGE_START = " Creating new "
        t_proto, t_port = [self.__proto, self.__port]

        while True:
            message = self.readline(t_timeout_seconds)
            message = N2KafkaChild._N2KafkaChild__clean_rdlog_message(message)

            if not message.startswith(MESSAGE_START):
                continue

            tokens = message[len(MESSAGE_START):].split()
            assert(tokens[0] == t_proto)
            assert(tokens[1 + len("listener on port".split())] == t_port)
            break

    # Override
    def _wait_child_ready(self):
        timeout_seconds = 60
        self.__wait_child_listener(timeout_seconds)


@pytest.fixture(scope="session")
def valgrind_handler(child):
    # the returned fixture value will be shared for all tests needing it
    if not shlex.split(child)[0] == "valgrind":
        yield None
        return

    h = ValgrindHandler()
    yield h
    h.write_xml()


class HTTPMessage(object):
    ''' Base HTTP message for testing '''

    def __init__(self, http_method, compressor=None, proto='http', **kwargs):
        ''' Honored params: uri, 'message', 'expected_response',
        'expected_response_code', 'expected_kafka_messages'
        '''
        self.http_method = http_method
        self.compressor = compressor
        self.proto = proto
        self.params = kwargs

    def deflate_dataset_chunks(compressor, http_chunks):
        for http_chunk in http_chunks:
            if 'chunk' in http_chunk:
                http_chunk['chunk'] = compressor.compress(
                                                       http_chunk['chunk']) + \
                    compressor.flush(zlib.Z_SYNC_FLUSH)

        # Add finish chunk in last valid one
        for http_hunk in reversed(http_chunks):
            if 'chunk' in http_chunk:
                http_chunk['chunk'] += compressor.flush(zlib.Z_FINISH)
                break

        return http_chunks

    def test_chunks_kafka_generator(kafka_handler, data_set):
        for data in data_set:
            # Raise exception if requested
            if 'raise' in data:
                raise data['raise']()

            # (let request library) send HTTP chunk
            yield data['chunk']

            # Wait kafka messages
            try:
                kafka_messages = data['kafka_messages']
            except KeyError:
                pass  # Not expecting any messages in this chunk
            else:
                kafka_handler.check_kafka_messages(kafka_messages['topic'],
                                                   kafka_messages['messages'])

    def http_header(http_headers, needle):
        # Case insensitive search of http header in http_headers
        content_encoding_dict = {
            k.lower(): v.lower() for k,
            v in http_headers.items() if k.lower() == needle}

        try:
            return content_encoding_dict[needle]
        except KeyError:
            return None

    def test(self,
             listener_port,
             kafka_handler,
             t_child,
             compres_handler=None):
        ''' Do the HTTP message test.

        Arguments:
          - listener_port: HTTP listener port
          - kafka handler:
        '''
        uri = self.proto + \
            '://localhost:' + \
            str(listener_port) + \
            self.params['uri']

        method_args = {
            key: self.params[key] for key in (
                'cert',
                'data',
                'headers',
                'params',
                'verify',
                ) if key in self.params}

        chunk_data = 'data' in method_args \
            and not isinstance(method_args['data'], str) \
            and not isinstance(method_args['data'], bytes) \
            and not isinstance(method_args['data'], bytearray)

        # 1st step: need everything in bytes format, not string
        if chunk_data:
            for data in method_args['data']:
                try:
                    data['chunk'] = data['chunk'].encode()
                except (AttributeError, KeyError):
                    # Already in bytes format, or not sending any data
                    pass
        else:
            try:
                method_args['data'] = method_args['data'].encode()
            except (AttributeError, KeyError):
                # Already in bytes format, or not sending any data
                pass

        # 2nd: Do we want to compress data?
        if self.compressor:
            compressor = self.compressor.copy()  # Maintain original untouched
            if chunk_data:
                # Compress each chunk individually
                method_args['data'] = HTTPMessage.deflate_dataset_chunks(
                                          compressor, method_args['data'])
            else:
                method_args['data'] = compressor.compress(
                                                       method_args['data']) + \
                    compressor.flush(zlib.Z_FINISH)

        # 3rd: Prepare kafka checking if we are sending data in chunks
        if chunk_data and 'data' in method_args:
            # If data is iterable, it will be sent as chunks, and kafka
            # messages will be checked for each chunk. In other case, all POST
            # message will be sent as a whole.
            method_args['data'] = HTTPMessage.test_chunks_kafka_generator(
                kafka_handler,
                method_args['data'])

        # Finally, send data and check kafka
        expected_exception_type = self.params.get("expected_exception_type")
        try:
            response = self.http_method(uri, **method_args)
            assert(expected_exception_type is None)
        except Exception as ex:
            if expected_exception_type and isinstance(
                    ex, expected_exception_type):
                pass  # Proper behavior
            else:
                raise

        if 'expected_stdout_regex' in self.params:
            patterns = self.params.get('expected_stdout_regex') or []

            stdout_timeout_s = 30
            locals_cache = locals()
            read_messages = []

            for pattern in patterns:
                # Complete variables
                pattern = pattern.format(**locals_cache)

                # Try to search message in previously saved messages
                if any(map(re.search, repeat(pattern), read_messages)):
                    continue  # Pattern found

                # Try to obtain new messages until timeout
                while True:
                    msg = t_child.readline(stdout_timeout_s)
                    read_messages.append(msg)
                    if re.search(pattern, msg):
                        break

        if 'expected_stdout_callback' in self.params:
            self.params['expected_stdout_callback'](t_child)

        if self.params.get('expected_response'):
            assert(response.text == self.params['expected_response'])

        if self.params.get('expected_response_code'):
            assert(response.status_code ==
                   self.params['expected_response_code'])

        for messages in self.params.get('expected_kafka_messages', []):
            topic_name = messages['topic']
            kafka_messages = messages['messages']
            kafka_handler.check_kafka_messages(topic_name, kafka_messages)


class HTTPGetMessage(HTTPMessage):
    def __init__(self, **kwargs):
        super().__init__(requests.get, **kwargs)


class HTTPPostMessage(HTTPMessage):
    def __init__(self, **kwargs):
        super().__init__(requests.post, **kwargs)


def test_module_argv(argv):
    ''' Search test module in argv'''
    return next(i for i, arg in enumerate(argv) if os.path.basename(
        arg).startswith('0') and arg.endswith('.py'))


class TestN2kafka(object):
    class CloseConnectionException(Exception):
        ''' Exception to signal that we want to terminate connection abruptly.
        '''
        pass

    def random_resource_file(t_resource_name):
        resource_name_prefix = 'n2k_' + t_resource_name + '_'
        with NamedTemporaryFile(prefix=resource_name_prefix,
                                delete=False,
                                dir='.') as f:
            return os.path.basename(f.name)

    def _random_resource(resource_name):
        return TestN2kafka.random_resource_file(
            resource_name)[len('n2k__' + resource_name):]

    def random_port(family=AF_INET, type=SOCK_STREAM):
        # Socket is closed, but child process should be able to open it while
        # TIMED_WAIT avoid other process to open it.
        with socket(family, type) as s:
            SOCKET_HOST = ''
            SOCKET_PORT = 0
            s.bind((SOCKET_HOST, SOCKET_PORT))
            name = s.getsockname()
            return name[1]

    def random_topic():
        # kafka broker complains if topic ends with __
        topic = '_'
        while '_' in topic:
            topic = TestN2kafka._random_resource('topic')
        return topic

    def _random_config_file():
        return TestN2kafka.random_resource_file('config')

    _BASE_LISTENER = {'proto': 'http', 'num_threads': 2}
    _BASE_CONFIG = {'brokers': 'kafka',
                    'rdkafka.socket.max.fails': '3',
                    'rdkafka.socket.keepalive.enable': 'true'}

    def _create_config_file(self, t_test_config):
        t_test_config['listeners'] = [{
            **TestN2kafka._BASE_LISTENER,
            'port': TestN2kafka.random_port(),
            **listener
        } for listener in t_test_config['listeners']]

        t_test_config = {**TestN2kafka._BASE_CONFIG, **t_test_config}
        t_file_name = TestN2kafka._random_config_file()

        if 'RDKAFKA_QUEUE_BUFFERING_MAX_MS' not in os.environ and \
                'rdkafka.queue.buffering.max.ms' not in t_test_config:
            # Speed up tests
            t_test_config['rdkafka.queue.buffering.max.ms'] = '0'

        with open(t_file_name, 'w') as f:
            json.dump(t_test_config, f)
            return (t_file_name, t_test_config)

    def base_test(self,
                  base_config,
                  child_argv_str,
                  messages,
                  kafka_handler,
                  valgrind_handler):
        ''' Base n2kafka test

        Arguments:
          - base_config: Base config file to use. Listener port(random) &
            proto(http), number of threads (2), brokers (kafka) and some
            rdkafka options will be added if not present.
          - child_argv_str: Child string to execute
          - messages: Messages to test
          - kafka_handler: Kafka handler to use
          - valgrind handler: Valgrind handler to use
        '''
        config_file, config = self._create_config_file(base_config)

        listener_port = config['listeners'][0]['port']

        with contextlib.ExitStack() as exit_stack:
            child_cls = N2KafkaChild
            child_kwargs = {
                'proto': 'HTTP',
                'port': listener_port,
                'config_file': config_file
            }
            if valgrind_handler:
                child = exit_stack.enter_context(valgrind_handler.run_child(
                                                    child_cls=child_cls,
                                                    child_args=child_argv_str,
                                                    **child_kwargs
                                                    ))
            else:
                child = exit_stack.enter_context(child_cls(
                                                       argv=child_argv_str,
                                                       **child_kwargs))
            for m in messages:
                m.test(listener_port=listener_port,
                       kafka_handler=kafka_handler,
                       t_child=child)


def main():
    pytest.main()

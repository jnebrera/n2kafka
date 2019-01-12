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

'''Test to provide configuration via config file or environment variable
'''

import ijson
import itertools
import json
from n2k_test import \
                     HTTPPostMessage, \
                     main, \
                     TestN2kafka

from n2k_test import valgrind_handler  # noqa: F401
import os
import pytest
import timeout_decorator


def _rdlog_thread_id(line):
    THREAD_POS = 3
    return line.split('|', THREAD_POS+1)[-2]


class GrepThread:
    def __init__(self, thread_id, t_input):
        self._thread_id = thread_id
        self._input = t_input
        self._curr_line = ''

    def read(self, size=-1):
        while not self._curr_line:
            self._curr_line = self._input.readline(t_timeout_seconds=5)
            if _rdlog_thread_id(self._curr_line) != self._thread_id:
                self._curr_line = ''
                continue

            try:
                # Remove rdlog info
                self._curr_line = self._curr_line.split('|', 4)[-1]
            except IndexError:
                # Not valid rdlog line
                self._curr_line = ''

            if self._curr_line == ' Librdkafka stats ===\n':
                self._curr_line = ''

        if size == -1:
            ret = self._curr_line
            self._curr_line = ''
        else:
            ret = self._curr_line[:size]
            self._curr_line = self._curr_line[size:]

        return ret


def _right_kafka_stats_message(m):
    try:
        for (topic_k, topic) in m['topics'].items():
            for (partition_i, partition) in topic['partitions'].items():
                if partition['msgs'] > 0:
                    return True
    except KeyError:
        pass

    return False


@timeout_decorator.timeout(5)
def _check_child_rdlog_json_stats(child):
    while True:
        line = child.readline(t_timeout_seconds=5)
        print(line)

        if line.endswith('Librdkafka stats ===\n'):
            stats_thread_id = _rdlog_thread_id(line)
            break

    while True:
        json_builder = ijson.common.ObjectBuilder()
        map_stack_i = 0
        g = GrepThread(stats_thread_id, child)
        for event, value in ijson.basic_parse(g):
            json_builder.event(event, value)

            if event == 'start_map':
                map_stack_i += 1
            elif event == 'end_map':
                map_stack_i -= 1
                if map_stack_i == 0:
                    break

                if _right_kafka_stats_message(json_builder.value):
                    return  # all OK!


@timeout_decorator.timeout(5)
def _check_kafka_consumed_message_json_stats(kafka_consumer):
    while True:
        kafka_message_bytes = next(kafka_consumer).value
        kafka_message = kafka_message_bytes.decode()
        dict_message = json.loads(kafka_message)
        if _right_kafka_stats_message(dict_message):
            break


class TestHTTP2K(TestN2kafka):
    @pytest.mark.parametrize(  # noqa=F811
        'kafka_broker',
        ('kafka', 'kafka_noautocreatetopic'))
    @pytest.mark.parametrize(
        'stats_topic_env, stats_topic_config',
        list(itertools.permutations([False, True], 2)))
    @pytest.mark.parametrize(
        'stats_append_env, stats_append_config',
        list(itertools.permutations([None, '{}', '{"test":1}'], 2)))
    def test_kafka_stats(self,
                         child,
                         kafka_handler,
                         kafka_broker,
                         valgrind_handler,
                         stats_topic_env,
                         stats_topic_config,
                         stats_append_env,
                         stats_append_config):
        ''' Base n2kafka test

        Arguments:
          - child: Child string to execute
          - messages: Messages to test
          - kafka_handler: Kafka handler to use
          - kafka_broker: Kafka broker to use
          - valgrind_handler: Valgrind handler if any
          - stats_topic_env: send librdkafka statistics to a topic, and set it
                             to n2kafka via environment
          - stats_topic_config: send librdkafka statistics to a topic, and set
                                it to n2kafka via environment
          - stats_append_env: Append stats dimensions via environment
          - stats_append_config: Append stats dimensions via config file
        '''

        TEST_MESSAGE = '{"test":1}'
        data_topic = TestN2kafka.random_topic()

        base_config = {
          "listeners": [{
              'proto': 'http',
              'decode_as': 'zz_http2k',
          }],
          'brokers': kafka_broker,
          'rdkafka.statistics.interval.ms': '100',
        }

        if stats_topic_config:
            stats_topic_config = TestN2kafka.random_topic()
        if stats_topic_env:
            stats_topic_env = TestN2kafka.random_topic()

        if stats_topic_config:
            base_config['rdkafka.n2kafka.statistics.topic'] = \
                                                             stats_topic_config
        if stats_topic_env:
            os.environ['RDKAFKA_N2KAFKA_STATISTICS_TOPIC'] = stats_topic_env

        statistics_topic = stats_topic_config or stats_topic_env

        extra_msg_args = {}
        if not statistics_topic:
            extra_msg_args = {
                'expected_stdout_callback': _check_child_rdlog_json_stats
            }
        elif statistics_topic and kafka_broker == 'kafka':
            extra_msg_args = {
                'expected_kafka_messages': [{
                    'topic': statistics_topic,
                    'messages': [_check_kafka_consumed_message_json_stats],
                }]
            }
        else:
            extra_msg_args = {
                'expected_stdout_regex':
                "Can't produce stats message: Local: Unknown topic"
            }

        messages = [
            HTTPPostMessage(
                   uri='/v1/data/' + data_topic,
                   data=TEST_MESSAGE,
                   expected_response_code=200,
                   **extra_msg_args)
        ]

        t_locals = locals()
        self.base_test(base_config=base_config,
                       child_argv_str=child,
                       **{key: t_locals[key]
                          for key in ['messages',
                                      'kafka_handler',
                                      'valgrind_handler',
                                      ]})


if __name__ == '__main__':
    main()

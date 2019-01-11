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
from n2k_test import \
                     HTTPPostMessage, \
                     main, \
                     TestN2kafka

from n2k_test import valgrind_handler  # noqa: F401
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


@timeout_decorator.timeout(5)
def _check_json_stats(child):
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

        for (topic_k, topic) in json_builder.value['topics'].items():
            for (partition_i, partition) in topic['partitions'].items():
                if partition['msgs'] > 0:
                    return  # all OK!


class TestHTTP2K(TestN2kafka):
    def test_kafka_stats(self,  # noqa=F811
                         child,
                         kafka_handler,
                         valgrind_handler):
        ''' Base n2kafka test

        Arguments:
          - child: Child string to execute
          - messages: Messages to test
          - kafka_handler: Kafka handler to use
          - valgrind_handler: Valgrind handler if any
        '''

        TEST_MESSAGE = '{"test":1}'
        used_topic = TestN2kafka.random_topic()

        base_config = {
          "listeners": [{
              'proto': 'http',
              'decode_as': 'zz_http2k',
          }],
          'rdkafka.statistics.interval.ms': '100',
        }

        messages = [
            HTTPPostMessage(
                   uri='/v1/data/' + used_topic,
                   data=TEST_MESSAGE,
                   expected_response_code=200,
                   expected_stdout_callback=_check_json_stats
                   ),
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

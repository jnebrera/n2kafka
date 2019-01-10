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

import pytest

from n2k_test import \
                     main, \
                     N2KafkaChild, \
                     TestN2kafka

from n2k_test import valgrind_handler  # noqa: F401
import contextlib
import itertools
import shlex
import os


class WatchedN2Kafka(N2KafkaChild):
    ''' n2kafka child that allows to inspect it's output after start'''
    def __init__(self, *n2k_args, **n2k_kwargs):
        self.__n2k_private_stdout = []
        self.__n2k_child_stdout = []

        super().__init__(*n2k_args, **n2k_kwargs)

    def stdout_message0(self, t_list, t_timeout_seconds):
        if t_list:
            return t_list.pop(0)

        if t_list is self.__n2k_private_stdout:
            o_list = self.__n2k_child_stdout
        else:
            o_list = self.__n2k_private_stdout

        i = super().stdout_message(t_timeout_seconds)
        o_list.append(i)
        return i

    def stdout_message(self, t_timeout_seconds):
        ''' Obtain child stdout message '''
        return self.stdout_message0(self.__n2k_child_stdout, t_timeout_seconds)

    def stdout_message_copy(self, t_timeout_seconds):
        ''' Obtain the private copy of child stdout '''
        return self.stdout_message0(self.__n2k_private_stdout,
                                    t_timeout_seconds)


class WatchedN2Kafka_handler(object):
    ''' Handle valgrind outputs, merging all xml output in one xml file
    deleting duplicates'''
    def __init__(self,
                 expected_kafka_properties,
                 expected_kafka_property_values):
        self.search_strings = tuple(
            'Kafka_config[{}][{}]\n'.format(kproperty, kvalue)
            for (kproperty, kvalue) in zip(expected_kafka_properties,
                                           expected_kafka_property_values))

    @contextlib.contextmanager
    def run_child(self, child_args, child_cls=None, **kwargs):
        ''' Run child under controlled environment, gathering xml output '''
        if isinstance(child_args, str):
            child_args = shlex.split(child_args)

        with WatchedN2Kafka(child_args, **kwargs) as child:
            yield child

            while self.search_strings:
                out_line = child.stdout_message_copy(1)

                self.search_strings = tuple(itertools.compress(
                          self.search_strings,
                          (out_line.endswith(i) for i in self.search_strings)))


class TestHTTP2K(TestN2kafka):
    @pytest.mark.parametrize(  # noqa: F811
        # rdkafka handler config
        "buffering_max_ms_config, buffering_max_ms_env,", [
            ('10', None),
            (None, '100'),
            ('10', '100'),
    ])
    def test_kafka_config(self,
                          child,
                          kafka_handler,
                          valgrind_handler,
                          buffering_max_ms_config,
                          buffering_max_ms_env):
        ''' Base n2kafka test

        Arguments:
          - child: Child string to execute
          - messages: Messages to test
          - kafka_handler: Kafka handler to use
          - valgrind_handler: Valgrind handler if any
          - buffering_max_ms_config: Buffering max ms to set in config file
          - buffering_max_ms_env: Buffering max ms to set in environment
        '''

        base_config = {
          "listeners": [{
              'proto': 'http',
              'decode_as': 'zz_http2k',
          }]
        }

        if buffering_max_ms_config:
            base_config['rdkafka.queue.buffering.max.ms'] = \
                                                        buffering_max_ms_config

        if buffering_max_ms_env:
            os.environ['RDKAFKA_QUEUE_BUFFERING_MAX_MS'] = buffering_max_ms_env

        if valgrind_handler:
            child_wrapper = valgrind_handler
        else:
            # Use checks for actual testing: we only inspect stdout/stderr here
            child_wrapper = WatchedN2Kafka_handler(
                                 ('queue.buffering.max.ms',),
                                 # Config always override environment
                                 ('10' if buffering_max_ms_config else '100',))

        t_locals = locals()
        self.base_test(base_config=base_config,
                       child_argv_str=child,
                       messages=[],
                       valgrind_handler=child_wrapper,
                       kafka_handler=kafka_handler)


if __name__ == '__main__':
    main()

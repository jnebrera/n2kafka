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

from n2k_test import \
    HTTPPostMessage, \
    main, \
    TestN2kafka

# flake8 does not handle pytest fixtures
from n2k_test import valgrind_handler  # noqa: F401


class TestDumb(TestN2kafka):
    ''' Test dumb (default) decoder '''

    def base_test_dumb(self,  # noqa: F811
                       kafka_topic_name,
                       child,
                       base_config,
                       kafka_handler,
                       valgrind_handler):
        TEST_MESSAGE = '{"test":1}'
        test_messages = [
            HTTPPostMessage(uri='/v1/meraki/mytestvalidator',
                            data=TEST_MESSAGE,
                            expected_response_code=200,
                            expected_kafka_messages=[
                                {'topic': kafka_topic_name,
                                 'messages': [TEST_MESSAGE]}
                            ]),
            HTTPPostMessage(uri='/v1/meraki/mytestvalidator',
                            data='{"test":1}{"test":2}',
                            expected_response_code=200,
                            expected_kafka_messages=[
                                {'topic': kafka_topic_name,
                                 'messages': ['{"test":1}{"test":2}']}
                            ])
        ]

        t_locals = locals()
        self.base_test(messages=test_messages,
                       child_argv_str=t_locals['child'],
                       **{key: t_locals[key] for key in ['base_config',
                                                         'kafka_handler',
                                                         'valgrind_handler']})

    def test_dumb_topic_general(self,  # noqa: F811
                                kafka_handler,
                                valgrind_handler,
                                child):
        ''' Test dumb decoder with topic in general config'''
        used_topic = TestN2kafka.random_topic()
        base_config = {'listeners': [{}], 'topic': used_topic}
        self.base_test_dumb(kafka_topic_name=used_topic,
                            child=child,
                            base_config=base_config,
                            kafka_handler=kafka_handler,
                            valgrind_handler=valgrind_handler)

    # TODO (we can use fixtures with params, to use only one function)
    # TODO
    # def test_dumb_topic_listener(self, kafka_handler, child):
        # ''' Test dumb decoder with a per-listener based'''
        # used_topic = TestN2kafka.random_topic()
        # base_config = {'listeners':[{'topic':used_topic}]}
        # self.base_test_dumb(used_topic, base_config, child)

    # TODO
    # def test_dumb_both_topics(self, kafka_handler, child):
        # ''' Test dumb decoder with both topics defined.'''
        # used_topic = TestN2kafka.random_topic()
        # unused_topic = TestN2kafka.random_topic()
        #  base_config = {
        # 'listeners':[{'topic':used_topic}],
        # 'topic':unused_topic
        # }
        # self.base_test_dumb(used_topic, base_config, child)


if __name__ == '__main__':
    main()

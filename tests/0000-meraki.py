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
                     HTTPGetMessage, \
                     HTTPPostMessage, \
                     main, \
                     TestN2kafka

from n2k_test import valgrind_handler  # noqa: F401


class TestMeraki(TestN2kafka):
    def base_test_meraki(self,  # noqa: F811
                         kafka_topic_name,
                         child,
                         base_config,
                         kafka_handler,
                         valgrind_handler):
        '''Test Meraki GET/POST behavior'''
        TEST_MESSAGE = '{"test":1}'
        test_messages = [
            HTTPGetMessage(uri='/v1/meraki/mytestvalidator',
                           expected_response='mytestvalidator',
                           expected_response_code=200),
            HTTPGetMessage(uri='/v1/meraki/mytestvalidator2',
                           expected_response='mytestvalidator2',
                           expected_response_code=200),
            HTTPPostMessage(uri='/v1/meraki/mytestvalidator',
                            data='{"test":1}',
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
                                 'messages': ['{"test":1}', '{"test":2}']}
                            ])
        ]

        t_locals = locals()
        self.base_test(child_argv_str=t_locals['child'],
                       messages=test_messages,
                       **{key: t_locals[key] for key in ['base_config',
                                                         'kafka_handler',
                                                         'valgrind_handler']})

    def test_meraki_topic_general(self,  # noqa: F811
                                  kafka_handler,
                                  valgrind_handler,
                                  child):
        ''' Test meraki with topic in general config'''
        used_topic = TestN2kafka.random_topic()
        base_config = {'listeners': [
            {'decode_as': 'meraki'}], 'topic': used_topic}
        self.base_test_meraki(kafka_topic_name=used_topic,
                              child=child,
                              base_config=base_config,
                              kafka_handler=kafka_handler,
                              valgrind_handler=valgrind_handler)

    def test_meraki_topic_listener(self,  # noqa: F811
                                   kafka_handler,
                                   valgrind_handler,
                                   child):
        ''' Test meraki with a per-listener based'''
        used_topic = TestN2kafka.random_topic()
        base_config = {'listeners': [
            {'decode_as': 'meraki', 'topic': used_topic}]}
        self.base_test_meraki(kafka_topic_name=used_topic,
                              child=child,
                              base_config=base_config,
                              kafka_handler=kafka_handler,
                              valgrind_handler=valgrind_handler)

    def test_meraki_both_topics(self,  # noqa: F811
                                kafka_handler,
                                valgrind_handler,
                                child):
        ''' Test meraki with both topics defined.'''
        used_topic = TestN2kafka.random_topic()
        unused_topic = TestN2kafka.random_topic()
        base_config = {
            'listeners': [{'decode_as': 'meraki', 'topic': used_topic}],
            'topic': unused_topic
        }
        self.base_test_meraki(kafka_topic_name=used_topic,
                              child=child,
                              base_config=base_config,
                              kafka_handler=kafka_handler,
                              valgrind_handler=valgrind_handler)

    def test_meraki_invalid_url(self,  # noqa: F811
                                kafka_handler,
                                valgrind_handler,
                                child):
        test_messages = [
            HTTPGetMessage(uri='/',
                           expected_response='',
                           expected_response_code=404),
        ]
        base_config = {
            'listeners': [{'decode_as': 'meraki', 'topic': 'unused'}]
        }
        self.base_test(messages=test_messages,
                       child_argv_str=child,
                       base_config=base_config,
                       kafka_handler=kafka_handler,
                       valgrind_handler=valgrind_handler)


if __name__ == '__main__':
    main()

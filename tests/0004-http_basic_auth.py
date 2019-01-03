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

import base64
import itertools
import os
import pytest
from n2k_test import \
                     HTTPPostMessage, \
                     main, \
                     TestN2kafka

from n2k_test import valgrind_handler  # noqa: F401


@pytest.fixture(params=[content + newline for (content, newline) in
                        itertools.chain(
                            itertools.product(
                                itertools.accumulate(
                                    ['',
                                     'user1:{PLAIN}password1',
                                     '\nuser2:{PLAIN}password2']),
                                ['', '\n'])
                        )
                        ])
def htpasswd_content(request):
    return request.param


class TestHTTP2K(TestN2kafka):
    @pytest.mark.parametrize("env_provided", [True, False])  # noqa: F811
    def test_https_auth(self,
                        child,
                        env_provided,
                        kafka_handler,
                        valgrind_handler,
                        htpasswd_content):
        ''' Base n2kafka test

        Arguments:
          - child: Child string to execute
          - messages: Messages to test
          - kafka_handler: Kafka handler to use
          - valgrind_handler: Valgrind handler if any
          - htpasswd_content: htpasswd file content
        '''

        TEST_MESSAGE = '{"test":1}'
        used_topic = TestN2kafka.random_topic()
        htpasswd_file = TestN2kafka.random_resource_file('htpasswd')

        with open(htpasswd_file, 'w') as f:
            f.write(htpasswd_content)

        base_config = {
            **{
              "listeners": [{
                  'proto': 'http',
                  'decode_as': 'zz_http2k',
              }]
            },
        }

        if env_provided:
            os.environ['HTTP_HTPASSWD_FILE'] = htpasswd_file
        else:
            base_config['listeners'][0]['htpasswd_filename'] = htpasswd_file

        htpasswd_has_user1 = any(
            user_password[0] in line
            for line in htpasswd_content.split('\n')
            for user_password in line.split(':')
            if len(user_password) > 1
            and user_password[0] == 'user1')

        user1_password1_expected_kafka_messages, \
            user1_password1_expected_code = (
                {'topic': used_topic,
                 'messages': [TEST_MESSAGE],
                 }, 200) if htpasswd_has_user1 else ([], 401)

        messages = [
            # Try to connect with no authorization header
            HTTPPostMessage(
                   uri='/v1/data/' + used_topic,
                   data=TEST_MESSAGE,
                   expected_response_code=401,
                   ),
        ] + [
            # Try to connect with valid user & password...
            HTTPPostMessage(
               uri='/v1/data/' + used_topic,
               data=TEST_MESSAGE,
               headers={
                'Authorization': base64.b64encode(user_password_string)},
               expected_response_code=expected_response_code,
               expected_kafka_messages=expected_kafka_messages,
               )
            for (user_password_string,
                 expected_response_code,
                 expected_kafka_messages)
            in [
                (b'user1:password1',
                 user1_password1_expected_code,
                 user1_password1_expected_kafka_messages),
                (b'user1:password2', 401, []),
                (b'nuser1:npassword2', 401, [])
            ]
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

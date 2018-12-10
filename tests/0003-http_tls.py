#!/usr/bin/env python3

import requests
import pytest
from n2k_test import \
                     HTTPPostMessage, \
                     main, \
                     TestN2kafka

from n2k_test import valgrind_handler  # noqa: F401


# TODO tests all combinations!
# @pytest.fixture(params=[None, 'certificate.pem', 'pass_certificate.pem'])
# def ca_verify(request):
#     return request.param

# @pytest.fixture(params=[None, '1234', 'wrong_password'])
# def key_password(request):
#     return request.param


class TestHTTP2K(TestN2kafka):
    @pytest.mark.parametrize(  # noqa: F811
        "tls_cert, tls_key, tls_pass, tls_pass_in_file", [
        ('tests/certificate.pem', 'tests/key.pem', None, False),
        ('tests/certificate.pem', 'tests/key.encrypted.pem', '1234', False),
        ('tests/certificate.pem', 'tests/key.encrypted.pem', '1234', True),
    ])
    def test_tls_https(self,
                       child,
                       kafka_handler,
                       valgrind_handler,
                       tls_cert,
                       tls_key,
                       tls_pass,
                       tls_pass_in_file,
                       tmpdir):
        ''' Base n2kafka test

        Arguments:
          - child: Child string to execute
          - messages: Messages to test
          - kafka_handler: Kafka handler to use
          - valgrind_handler: Valgrind handler if any
          - tls_cert: HTTPs cert to export
          - tls_key: HTTPs private key to use in crypt
          - tls_pass: RSA password for Key. Can be NULL
          - password_in_file: tls key password is store in a file.
          - tmpdir: Temporary directory (pytest fixture)
        '''

        TEST_MESSAGE = '{"test":1}'
        used_topic = TestN2kafka.random_topic()

        base_config = {
            **{
              "listeners": [{
                  'proto': 'http',
                  'decode_as': 'zz_http2k',
                  "https_key_filename": tls_key,
                  "https_cert_filename": tls_cert,
              }]
            },
        }

        if tls_pass:
            if tls_pass_in_file:
                path = tmpdir / "password.pass"
                with open(path, 'w') as f:
                    f.write(tls_pass)
                    base_config["listeners"][0]["https_key_password"] = \
                        '@' + str(path)
            else:
                base_config["listeners"][0]["https_key_password"] = tls_pass

        messages = [
            # Try to connect with plain http -> bad
            HTTPPostMessage(
                   uri='/v1/data/' + used_topic,
                   data=TEST_MESSAGE,
                   expected_exception_type=requests.exceptions.ConnectionError,
                   ),

            # Try to connect with no valid certificate
            HTTPPostMessage(
                   uri='/v1/data/' + used_topic,
                   data=TEST_MESSAGE,
                   proto='https',
                   expected_exception_type=requests.exceptions.SSLError,
                   ),

            # Success
            HTTPPostMessage(uri='/v1/data/' + used_topic,
                            data=TEST_MESSAGE,
                            proto='https',
                            verify=tls_cert,
                            expected_response_code=200,
                            expected_kafka_messages=[
                                {'topic': used_topic,
                                 'messages': [TEST_MESSAGE],
                                 }]),
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

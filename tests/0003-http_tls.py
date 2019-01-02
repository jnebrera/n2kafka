#!/usr/bin/env python3

import requests
import pytest
from n2k_test import \
                     HTTPPostMessage, \
                     main, \
                     TestN2kafka

from n2k_test import valgrind_handler  # noqa: F401
import os


class TestHTTP2K(TestN2kafka):
    @pytest.mark.parametrize(  # noqa: F811
        "tls_cert, tls_key, tls_pass, tls_pass_in_file", [
        ('tests/certificate.pem', 'tests/key.pem', None, False),
        ('tests/certificate.pem', 'tests/key.encrypted.pem', '1234', False),
        ('tests/certificate.pem', 'tests/key.encrypted.pem', '1234', True),
    ])
    @pytest.mark.parametrize("env_provided", [True, False])
    @pytest.mark.parametrize("tls_client_cert",
                             [None,
                              ('tests/client-certificate-1.pem',
                               'tests/client-key-1.pem')])
    def test_tls_https(self,
                       child,
                       kafka_handler,
                       valgrind_handler,
                       env_provided,
                       tls_cert,
                       tls_key,
                       tls_pass,
                       tls_pass_in_file,
                       tls_client_cert,
                       tmpdir):
        ''' Base n2kafka test

        Arguments:
          - child: Child string to execute
          - messages: Messages to test
          - kafka_handler: Kafka handler to use
          - valgrind_handler: Valgrind handler if any
          - tmpdir: The temporary dir used for cert-storage env-based search
          - env_provided: Test if private key/cert are env-provided or not
          - tls_cert: HTTPs cert to export
          - tls_key: HTTPs private key to use in crypt
          - tls_pass: RSA password for Key. Can be NULL
          - tls_client_cert: client certificate / key tuple. Can be none.
          - tmpdir: Temporary directory (pytest fixture)
        '''

        TEST_MESSAGE = '{"test":1}'
        used_topic = TestN2kafka.random_topic()

        base_config = {
          "listeners": [{
              'proto': 'http',
              'decode_as': 'zz_http2k',
          }]
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

        if env_provided:
            os.environ['HTTP_TLS_KEY_FILE'] = str(tls_key)
            os.environ['HTTP_TLS_CERT_FILE'] = str(tls_cert)
            if tls_pass:
                os.environ['HTTP_TLS_KEY_PASSWORD'] = \
                    base_config["listeners"][0]["https_key_password"]
                del base_config["listeners"][0]["https_key_password"]
        else:
            base_config['listeners'][0]['https_key_filename'] = tls_key
            base_config['listeners'][0]['https_cert_filename'] = tls_cert

        if tls_client_cert:
            base_config["listeners"][0]["https_clients_ca_filename"] = \
                tls_client_cert[0]

        unauthenticated_client_expected_exception = \
            requests.exceptions.SSLError if tls_client_cert else None

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

            # Non tls-authorized client
            HTTPPostMessage(
                uri='/v1/data/' + used_topic,
                data=TEST_MESSAGE,
                proto='https',
                verify=tls_cert,
                expected_response_code=403,
                expected_response="Unknown error checking certificate, do you "
                "have one?",
                )
            if tls_client_cert else
            # Regular https request
            HTTPPostMessage(uri='/v1/data/' + used_topic,
                            data=TEST_MESSAGE,
                            proto='https',
                            verify=tls_cert,
                            expected_kafka_messages=[
                                {'topic': used_topic,
                                 'messages': [TEST_MESSAGE],
                                 }]),
        ] + [
            # Bad client certificate
            HTTPPostMessage(
                uri='/v1/data/' + used_topic,
                data=TEST_MESSAGE,
                proto='https',
                verify=tls_cert,
                cert=[s.replace('1', '2')
                      for s in tls_client_cert],
                expected_response_code=403,
                expected_response="The signature verification failed",
                ),

            # Right client certificate
            HTTPPostMessage(uri='/v1/data/' + used_topic,
                            data=TEST_MESSAGE,
                            proto='https',
                            verify=tls_cert,
                            cert=tls_client_cert,
                            expected_response_code=200,
                            expected_kafka_messages=[
                                {'topic': used_topic,
                                 'messages': [TEST_MESSAGE],
                                 }]),
        ] if tls_client_cert else []

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

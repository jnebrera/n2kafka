#!/usr/bin/env python3

from n2k_test import \
    HTTPGetMessage, \
    HTTPPostMessage, \
    main, \
    TestN2kafka, \
    valgrind_handler


class TestDumb(TestN2kafka):
    ''' Test dumb (default) decoder '''

    def base_test_dumb(self,
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

    def test_dumb_topic_general(self, kafka_handler, valgrind_handler, child):
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
        #''' Test dumb decoder with a per-listener based'''
        #used_topic = TestN2kafka.random_topic()
        #base_config = {'listeners':[{'topic':used_topic}]}
        #self.base_test_dumb(used_topic, base_config, child)

    # TODO
    # def test_dumb_both_topics(self, kafka_handler, child):
        #''' Test dumb decoder with both topics defined.'''
        #used_topic = TestN2kafka.random_topic()
        #unused_topic = TestN2kafka.random_topic()
        # base_config = {
        #'listeners':[{'topic':used_topic}],
        #'topic':unused_topic
        #}
        #self.base_test_dumb(used_topic, base_config, child)


if __name__ == '__main__':
    main()

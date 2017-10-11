#!/usr/bin/env python3

from n2k_test import HTTPGetMessage, HTTPPostMessage, TestN2kafka, main


class TestMeraki(TestN2kafka):
    def base_test_meraki(self,
                         kafka_topic_name,
                         child,
                         base_config,
                         kafka_handler):
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
                                {'topic': kafka_topic_name, 'messages': [TEST_MESSAGE]}
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
                                                         'kafka_handler']})

    def test_meraki_topic_general(self, kafka_handler, child):
        ''' Test meraki with topic in general config'''
        used_topic = TestN2kafka.random_topic()
        base_config = {'listeners': [
            {'decode_as': 'meraki'}], 'topic': used_topic}
        self.base_test_meraki(kafka_topic_name=used_topic,
                              child=child,
                              base_config=base_config,
                              kafka_handler=kafka_handler)

    def test_meraki_topic_listener(self, kafka_handler, child):
        ''' Test meraki with a per-listener based'''
        used_topic = TestN2kafka.random_topic()
        base_config = {'listeners': [
            {'decode_as': 'meraki', 'topic': used_topic}]}
        self.base_test_meraki(kafka_topic_name=used_topic,
                              child=child,
                              base_config=base_config,
                              kafka_handler=kafka_handler)

    def test_meraki_both_topics(self, kafka_handler, child):
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
                              kafka_handler=kafka_handler)

    def test_meraki_invalid_url(self, kafka_handler, child):
        test_messages = [
            HTTPGetMessage(uri='/',
                           expected_response='',
                           expected_response_code=200),
        ]
        base_config = {
            'listeners': [{'decode_as': 'meraki', 'topic': 'unused'}]
        }
        self.base_test(messages=test_messages,
                       child_argv_str=child,
                       base_config=base_config,
                       kafka_handler=kafka_handler)


if __name__ == '__main__':
    main()

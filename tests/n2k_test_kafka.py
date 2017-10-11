from concurrent.futures import ThreadPoolExecutor, as_completed

import pykafka.common
import itertools
from pykafka import KafkaClient


class KafkaHandler(object):
    ''' Kafka handler for n2kafka tests '''

    def __init__(self, hosts="kafka:9092"):
        self._kafka = KafkaClient(hosts)
        self._kafka_consumers = {}

    def _consumer(self, topic_name):
        try:
            return self._kafka_consumers[topic_name]
        except KeyError as e:
            # Create topic consumer
            topic = self._kafka.topics[topic_name]
            offset_earliest = pykafka.common.OffsetType.EARLIEST
            consumer = topic.get_simple_consumer(
                auto_offset_reset=offset_earliest,
                consumer_timeout_ms=5 * 1000)
            consumer.start()
            self._kafka_consumers[topic.name] = consumer
            return consumer

    def check_kafka_messages(self, topic_name, messages):
        ''' Check kafka messages '''
        try:
            topic_name = topic_name.encode()
        except AttributeError:
            pass  # Already in bytes

        consumer = self._consumer(topic_name)
        consumed_messages = list(itertools.islice(consumer, 0, len(messages)))

        assert([m.value for m in consumed_messages] ==
               [s.encode() for s in messages])

    def assert_all_messages_consumed(self):
        num_consumers = len(self._kafka_consumers)
        if num_consumers == 0:
            return

        with ThreadPoolExecutor(max_workers=num_consumers) as pool:
            block = False
            consumer_futures = [pool.submit(consumer.consume, block)
                                for consumer in self._kafka_consumers.values()]

            # No messages should have been seen for any consumer for
            # consumer_timeout_ms
            return [None] * len(consumer_futures) == \
                   [f.result() for f in as_completed(consumer_futures)]

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

from concurrent.futures import ThreadPoolExecutor, as_completed

import pykafka.common
from pykafka import KafkaClient


class KafkaHandler(object):
    ''' Kafka handler for n2kafka tests '''

    def __init__(self, hosts="kafka:9092"):
        self._kafka = KafkaClient(hosts)
        self._kafka_consumers = {}

    def _consumer(self, topic_name):
        try:
            return self._kafka_consumers[topic_name]
        except KeyError:
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

        consumer = iter(self._consumer(topic_name))

        for m_expected in messages:
            m_consumed = next(consumer)
            if isinstance(m_expected, str):
                m_expected = m_expected.encode()

            if isinstance(m_expected, bytes):
                assert(m_expected == m_consumed.value)
            else:
                m_expected(consumer)

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

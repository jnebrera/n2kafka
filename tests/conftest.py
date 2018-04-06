#!/usr/bin/env python3

import pytest
from n2k_test_kafka import KafkaHandler


def pytest_addoption(parser):
    parser.addoption("--child", action="store", default="./n2kafka",
                     help="Child to execute")


@pytest.fixture(scope='session')
def child(request):
    return request.config.getoption("--child")

@pytest.fixture(scope='session')
def kafka_handler():
    handler = KafkaHandler()
    yield handler

    # Everything after "yield" is treated as tear-down code to pytest
    handler.assert_all_messages_consumed()

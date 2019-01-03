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

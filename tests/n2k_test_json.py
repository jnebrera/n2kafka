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

from enum import Enum, auto
from random import choice, randint
from string import ascii_uppercase


class FuzzyJSON(object):
    ''' Creation of random JSON object to fuzzy testing'''
    class JsonTypes(Enum):
        TRUE = True
        FALSE = False
        NULL = None
        STRING = auto()
        NUMBER = auto()
        OBJECT = auto()
        ARRAY = auto()
        RANDOM = auto()

    def _random_string(t_len):
        return ''.join(choice(ascii_uppercase) for i in range(t_len))

    def __init__(self, t_max_len=10, t_type=JsonTypes.RANDOM):
        ''' Create a fuzzy JSON. If type is string, object or array, maximum len
        will be below t_max_len, and child max len will be t_max_len-1 '''
        if FuzzyJSON.JsonTypes.RANDOM == t_type:
            valid_types = list(FuzzyJSON.JsonTypes)
            valid_types.remove(FuzzyJSON.JsonTypes.RANDOM)
            t_type = choice(valid_types)

        if FuzzyJSON.JsonTypes.STRING == t_type:
            self.value = FuzzyJSON._random_string(t_max_len)
        elif FuzzyJSON.JsonTypes.NUMBER == t_type:
            self.value = randint(0, 2**32 - 1)
        elif FuzzyJSON.JsonTypes.OBJECT == t_type:
            self.value = {
                FuzzyJSON._random_string(10): FuzzyJSON(
                    t_max_len - 1,
                    FuzzyJSON.JsonTypes.RANDOM).value for _ in range(t_max_len)
                }
        elif FuzzyJSON.JsonTypes.ARRAY == t_type:
            self.value = [FuzzyJSON(t_max_len - 1).value
                          for _ in range(t_max_len)]
        else:
            self.value = t_type.value

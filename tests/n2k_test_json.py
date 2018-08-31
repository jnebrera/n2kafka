'''
ZZ JSON test module.
'''

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

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

from contextlib import contextmanager
from itertools import chain

import argparse
import colorama
import os
import sys
import xml.etree.ElementTree as ET


def parse_cmd_args():
        parser = argparse.ArgumentParser(description='Show tests results')

        arguments = [
            {'flags': ['--checks', '-c'], 'help': 'Print checks results'},
            {'flags': ['--memcheck', '-m'], 'help': 'Print memcheck results'},
            {'flags': ['--helgrind', '-e'], 'help': 'Print helgrind results'},
            {'flags': ['--drd', '-d'], 'help': 'Print drd results'},
        ]

        for arg in arguments:
            parser.add_argument(*arg['flags'],
                                help=arg['help'],
                                action='store_true')

        parser.add_argument('test_results',
                            metavar='xml_files',
                            nargs='+',
                            help='Files to show result')

        return parser.parse_args()


def empty_junit_testcase(static_elmtree=None):
    if not static_elmtree:
        static_elmtree = ET.fromstring('<testsuite></testsuite>')

    return static_elmtree


valgrind_xml_root_tag = 'allvalgrindoutput'


#
# @brief      Return a valid XML from concatenated valgrind output
#
# @param      xml_filename  The xml filename
#
# @return     XML with <allvalgrindoutput> root and all valgrind outputs as
#             child
#
def parse_valgrind_xml(xml_filename):
    first_tag = '<' + valgrind_xml_root_tag + '>'
    with open(xml_filename, 'r') as f:
        return ET.fromstringlist(chain([first_tag], f, ['</' + first_tag[1:]]))


def empty_valgrind_xml(static_elmtree=None):
    if not static_elmtree:
        static_elmtree = ET.fromstring('<' + valgrind_xml_root_tag + '></'
                                       + valgrind_xml_root_tag + '>')

    return static_elmtree


def valgrind_error_text(error):
    bytes_lost = blocks_lost = 0
    xwhat = error.find('xwhat')
    if xwhat is not None:
        text, bytes_lost, blocks_lost = (xwhat.find(tag).text
                                         for tag in ['text',
                                                     'leakedbytes',
                                                     'leakedblocks'])

        bytes_lost = int(bytes_lost)
        blocks_lost = int(blocks_lost)

    else:
        text = error.find('what').text

    return text, bytes_lost, blocks_lost


@contextmanager
def printESC(color='', text=''):
    print("{color}{text}".format(color=color,
                                 text=text),
          end='')
    yield
    print(colorama.Style.RESET_ALL)


if __name__ == '__main__':
    try:
        TERMINAL_COLUMNS = os.get_terminal_size().columns
    except Exception:
        TERMINAL_COLUMNS = 80

    testcases = mem_errors = helgrind_errors = drd_errors = 0
    args = parse_cmd_args()

    # Check for every .test file
    for filename in args.test_results:
        with printESC(color=colorama.Style.BRIGHT):
            print('='*(TERMINAL_COLUMNS-8))
            print(filename.center(TERMINAL_COLUMNS))
            print('='*(TERMINAL_COLUMNS-8))

        junit = ET.parse(filename + '.xml').getroot() \
            if args.checks \
            else empty_junit_testcase()

        #  Saved results on XML files
        memcheck, drdchecks, helchecks = (
            parse_valgrind_xml(filename + ext) if enable
            else empty_valgrind_xml()
            for (ext, enable) in [('.mem.xml', args.memcheck),
                                  ('.helgrind.xml', args.helgrind),
                                  ('.drd.xml', args.drd)]
            )

        junit_error = any(testcase.find('failure') is not None
                          for testcase in junit)
        memory_errors = any(child.tag == 'error'
                            for valgrindoutput in memcheck
                            for child in valgrindoutput)
        concurrency_errors = any(child.tag == 'error'
                                 for allvalgrindoutput in [drdchecks,
                                                           helchecks]
                                 for valgrindoutput in allvalgrindoutput
                                 for child in valgrindoutput)
        valgrind_error = memory_errors or concurrency_errors

        if args.checks:
            for testcase in junit:
                failure = testcase.find('failure')

                color, symbol = (colorama.Fore.RED, '✘') \
                    if failure is not None \
                    else (colorama.Fore.YELLOW, '✘') if valgrind_error \
                    else (colorama.Fore.GREEN, '✔')

                with printESC(color):
                    print('\t {symbol} {name} {time}'.format(
                                                 symbol=symbol,
                                                 name=testcase.attrib['name'],
                                                 time=testcase.attrib['time']),
                          end='')

                    if failure is not None:
                        print('\n\t\t' + '-'*(TERMINAL_COLUMNS-16))
                        print('\t\t{message}\n{text}'.format(
                                            message=failure.attrib['message'],
                                            text=failure.text))
                        print('\t\t' + '-'*(TERMINAL_COLUMNS-16))

        if memory_errors:
            with printESC(colorama.Style.BRIGHT):
                print('\t' + '-'*(TERMINAL_COLUMNS-8))
                print("\t Memory issues")
                print('\t' + '-'*(TERMINAL_COLUMNS-8))

            blocks_leak = mem_leak = 0
            for error in (error for valgrindoutput in memcheck
                          for error in valgrindoutput.findall('error')):

                text, bytes_lost, blocks_lost = valgrind_error_text(error)

                mem_leak += bytes_lost
                blocks_leak += blocks_lost

                with printESC(colorama.Fore.RED):
                    print('\t • {text}'.format(text=text), end='')

            print(
                "\t TOTAL MEMORY LEAKED: {bytes} BYTES "
                "IN {blocks} blocks".format(
                    bytes=mem_leak, blocks=blocks_leak))

            with printESC(colorama.Style.BRIGHT):
                print('\t' + '-'*(TERMINAL_COLUMNS-8))

        if concurrency_errors:
            with printESC(colorama.Style.BRIGHT):
                print('\t' + '-'*(TERMINAL_COLUMNS-8))
                print("\t Concurrency issues")
                print('\t' + '-'*(TERMINAL_COLUMNS-8))

            for allvalgrindoutput in [drdchecks, helchecks]:
                for valgrindoutput in allvalgrindoutput:
                    for error in valgrindoutput.findall('error'):
                        text, _, _ = valgrind_error_text(error)
                        with printESC(colorama.Fore.RED):
                            print('\t' '{text}'.format(text=text), end='')

            with printESC(colorama.Style.BRIGHT):
                print('\t' + '-'*(TERMINAL_COLUMNS-8))

    with printESC(colorama.Fore.RED if junit_error or valgrind_error
                  else colorama.Fore.GREEN):
        print(('NOT PASSED'
               if junit_error or valgrind_error
               else 'PASSED').center(TERMINAL_COLUMNS-8))

    sys.exit(0 if not junit_error and not valgrind_error else 1)

#!/usr/bin/env python3

import contextlib
import os
import shlex
from concurrent.futures import ThreadPoolExecutor
from subprocess import Popen

from xml.etree import ElementTree as XmlElementTree


def valgrind_output_xml(pipe_r):
    ''' Extract valgrind output XML from a pipe file descriptor '''
    with os.fdopen(pipe_r, 'r', closefd=False) as fout:
        parser = XmlElementTree.XMLPullParser()
        for line in fout:
            parser.feed(line)

            for event in parser.read_events():
                if event[1].tag == 'valgrindoutput':
                    return event[1]


@contextlib.contextmanager
def pipe():
    ''' Pipe context manager '''
    p = os.pipe()
    yield p
    for fd in p:
        os.close(fd)


def xml_equals(xml_a, xml_b):
    ''' Checks for equality between two xml elements'''
    if len(xml_a) != len(xml_b):
        return False

    for attr in ['tag', 'text', 'tail', 'attrib']:
        if getattr(xml_a, attr) != getattr(xml_b, attr):
            return False

    return all(xml_equals(c1, c2) for c1, c2 in zip(xml_a, xml_b))


def valgrind_error_equals(xml_error_list):
    ''' Compare two errors '''
    # Compare errors kind
    if not all(e.find('kind').text == xml_error_list[0].find('kind').text
               for e in xml_error_list[1:]):
        return False

    # Compare stack
    stacks = [e.find('stack') for e in xml_error_list]
    return all(xml_equals(stacks[0], s) for s in stacks[1:])


def valgrind_xml_filter_out(valgrind_xml_a, valgrind_xml_b):
    ''' Filter valgrind_xml_b errors if they are present in valgrind_xml_a '''
    errors_b = [e for e in valgrind_xml_b if e.tag == 'error']
    errors_a = [e for e in valgrind_xml_a if e.tag == 'error']

    for error_b in errors_b:
        for error_a in errors_a:
            if valgrind_error_equals([error_a, error_b]):
                errors_b.remove(error_b)
                break


class ValgrindHandler(object):
    ''' Handle valgrind outputs, merging all xml output in one xml file
    deleting duplicates'''
    def __init__(self):
        self.xml_file = None
        self.__all_xml = []

    @contextlib.contextmanager
    def run_child(self, child_args, child_cls=Popen, **kwargs):
        ''' Run child under controlled environment, gathering xml output '''
        if isinstance(child_args, str):
            child_args = shlex.split(child_args)
        opt = '--xml-file='
        with pipe() as (pipe_r, pipe_w):
            # Use file descriptor for xml output
            for idx, arg in enumerate(child_args):
                if arg.startswith('--xml-file='):
                    if not self.xml_file:
                        self.xml_file = arg.split('=', 1)[1]
                    child_args[idx] = '--xml-fd={}'.format(pipe_w)
                    break
            else:
                raise KeyError

            with ThreadPoolExecutor() as executor:
                # Need to consume pipe_r while valgrind is executing. If not,
                # child freeze.
                valgrind_output_future = executor.submit(valgrind_output_xml,
                                                         pipe_r)
                with child_cls(child_args, pass_fds=(pipe_w,), **kwargs) \
                        as child:
                    yield child

                valgrind_output = valgrind_output_future.result()
                self.__add_valgrind_output(valgrind_output)

    def __add_valgrind_output(self, valgrind_output):
        if len(self.__all_xml):
            for prev_elm in self.__all_xml:
                valgrind_xml_filter_out(prev_elm, valgrind_output)

        self.__all_xml.append(valgrind_output)

    def write_xml(self):
        with open(self.xml_file, 'wb') as out_f:
            for xml_out in self.__all_xml:
                XmlElementTree.ElementTree(xml_out).write(out_f,
                                                          encoding="utf-8")

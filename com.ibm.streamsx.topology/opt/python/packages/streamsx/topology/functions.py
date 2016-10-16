# Licensed Materials - Property of IBM
# Copyright IBM Corp. 2015,2016

from __future__ import print_function
import sys

# Print function that flushes
def print_flush(v):
    """
    Prints argument to stdout flushing after each tuple.
    :returns: None
    """
    print(v)
    sys.stdout.flush()

def identity(t):
    """
    Returns its single argument.
    :returns: Its argument.
    """
    return t;

# Wraps an iterable instance returning
# it when called. Allows an iterable
# instance to be passed directly to Topology.source
class _IterableInstance(object):
    def __init__(self, it):
        self._it = it
    def __call__(self):
        return self._it

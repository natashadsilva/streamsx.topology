# coding=utf-8
# Licensed Materials - Property of IBM
# Copyright IBM Corp. 2016
import unittest
import sys
import itertools
from enum import IntEnum
import datetime
import decimal
import random

from streamsx.topology.schema import StreamSchema
from streamsx.topology.topology import *
from streamsx.topology.tester import Tester
from streamsx.topology.context import JobConfig
from streamsx.topology.context import ConfigParams
import streamsx.spl.op as op


class AddIt(object):
    def __init__(self, sp):
        self.sp = sp
    def __enter__(self):
        self.spv = self.sp()
    def __exit__(self, exc_type, exc_value, traceback):
        pass
    def __call__(self, t):
        return str(t) + '-' + self.spv

from streamsx.ec import get_submission_time_value

# A wrapper that imports streamsx.ec so that the function can be used in a lambda expression at runtime
# use in a lambda expression in a filter
def wrap_get_int_submission_val(name):
#    from streamsx.ec import get_submission_time_value
    return get_submission_time_value(name, int)

# use as function callable in a filter
def is_dividable_by_modulo_submission_param(tuple):
    #from streamsx.ec import get_submission_time_value
    # assuming the tuple is an int python object
    return tuple % get_submission_time_value(name='modulo', type_=int) == 0

# use as function callable in a map
def greet(tuple):
    #from streamsx.ec import get_submission_time_value
    # assume tuple is a string
    return tuple.format(get_submission_time_value('p1'))

# use as callable class in a filter
# test if a number can be or cannot be divided by a modulo.
# Which test is performed, is controlled by a submission time parameter
class DivideTester(object):
    def __init__(self, subm_param_name, modulo):
        self.spn = subm_param_name
        self.modulo = modulo
    def __enter__(self):
        self.spv = get_submission_time_value(self.spn, bool)
    def __exit__(self, exc_type, exc_value, traceback):
        pass
    def __call__(self, t):
        is_even = t % self.modulo == 0
        return is_even == self.spv

class TestSubmissionParams(unittest.TestCase):
    """ Test submission params (standalone).
    """
    _multiprocess_can_split_ = True

    def setUp(self):
        Tester.setup_standalone(self)

    def test_spl_default(self):
        """
        Test passing as with default using SPL
        """
        N=27
        G='hey there'
        t = ''.join(random.choice('0123456789abcdef') for x in range(20))
        topic = 'topology/test/python/' + t
       
        topo = Topology()
        spGreet = topo.create_submission_parameter('greeting', default=G)
        self.assertIsNone(spGreet())

        sch = StreamSchema('tuple<uint64 seq, rstring s>')
        b = op.Source(topo, "spl.utility::Beacon", sch,
            params = {'initDelay': 10.0, 'period': 0.02, 'iterations':N})
        b.seq = b.output('IterationCount()')
        b.s = b.output(spGreet)

        tester = Tester(topo)
        tester.tuple_count(b.stream, N)
        tester.contents(b.stream, [{'seq':i, 's':G} for i in range(N)])
        tester.test(self.test_ctxtype, self.test_config)

    def test_topo(self):
        topo = Topology()
        s = topo.source(range(38))
        lower = topo.create_submission_parameter('lower')
        upper = topo.create_submission_parameter('upper')
        addin = topo.create_submission_parameter('addin')

        s = s.filter(lambda v: v < int(lower()) or v > int(upper()))

        m = s.filter(lambda v : v < 3)
        m = m.map(AddIt(addin))

        jc = JobConfig()
        jc.submission_parameters['lower'] = 7
        jc.submission_parameters['upper'] = 33
        jc.submission_parameters['addin'] = 'Yeah!'
        jc.add(self.test_config)

        tester = Tester(topo)
        tester.contents(s, [0,1,2,3,4,5,6,34,35,36,37])
        tester.contents(m, ['0-Yeah!','1-Yeah!','2-Yeah!'])
        tester.test(self.test_ctxtype, self.test_config)

    def test_topo_with_def_and_type(self):
        topo = Topology()
        s = topo.source(range(38))
        lower = topo.create_submission_parameter('lower', default=0)
        upper = topo.create_submission_parameter('upper', default=30)

        s = s.filter(lambda v: v < lower() or v > upper())

        jc = JobConfig()
        jc.submission_parameters['lower'] = 5
        jc.add(self.test_config)

        tester = Tester(topo)
        tester.contents(s, [0,1,2,3,4,31,32,33,34,35,36,37])
        tester.test(self.test_ctxtype, self.test_config)

    def test_topo_types_from_default(self):
        topo = Topology()
        sp_str = topo.create_submission_parameter('sp_str', default='Hi')
        sp_int = topo.create_submission_parameter('sp_int', default=89)
        sp_float = topo.create_submission_parameter('sp_float', default=0.5)
        sp_bool = topo.create_submission_parameter('sp_bool', default=False)
        
        s = topo.source(range(17))
        s = s.filter(lambda v : isinstance(sp_str(), str) and sp_str() == 'Hi')
        s = s.filter(lambda v : isinstance(sp_int(), int) and sp_int() == 89)
        s = s.filter(lambda v : isinstance(sp_float(), float) and sp_float() == 0.5)
        s = s.filter(lambda v : isinstance(sp_bool(), bool) and sp_bool() is False)
     
        tester = Tester(topo)
        tester.tuple_count(s, 17)
        tester.test(self.test_ctxtype, self.test_config)

    def test_topo_types_explicit_set(self):
        topo = Topology()
        sp_str = topo.create_submission_parameter('sp_str', type_=str)
        sp_int = topo.create_submission_parameter('sp_int', type_=int)
        sp_float = topo.create_submission_parameter('sp_float', type_=float)
        sp_bool = topo.create_submission_parameter('sp_bool', type_=bool)
        
        s = topo.source(range(17))
        s = s.filter(lambda v : isinstance(sp_str(), str) and sp_str() == 'SeeYa')
        s = s.filter(lambda v : isinstance(sp_int(), int) and sp_int() == 10)
        s = s.filter(lambda v : isinstance(sp_float(), float) and sp_float() == -0.5)
        s = s.filter(lambda v : isinstance(sp_bool(), bool) and sp_bool() is True)
        jc = JobConfig()
        jc.submission_parameters['sp_str'] = 'SeeYa'
        jc.submission_parameters['sp_int'] = 10
        jc.submission_parameters['sp_float'] = -0.5
        jc.submission_parameters['sp_bool'] = True
        jc.add(self.test_config)
     
        tester = Tester(topo)
        tester.tuple_count(s, 17)
        tester.test(self.test_ctxtype, self.test_config)

    def test_parallel(self):
        topo = Topology()
        sp_w1 = topo.create_submission_parameter('w1', type_=int)
        sp_w2 = topo.create_submission_parameter('w2', type_=int)
        
        s = topo.source(range(67)).set_parallel(sp_w1)
        s = s.filter(lambda v : v % sp_w1() == 0)
        s = s.end_parallel()
        s = s.parallel(width=sp_w2)
        s = s.filter(lambda v : v % sp_w2() == 0)
        s = s.end_parallel()

        jc = JobConfig()
        jc.submission_parameters['w1'] = 3
        jc.submission_parameters['w2'] = 5
        jc.add(self.test_config)
     
        tester = Tester(topo)
        tester.contents(s,[0,15,30,45,60]*3, ordered=False)
        tester.test(self.test_ctxtype, self.test_config)

    def test_int_parallel_with_ec(self):
        topo = Topology()
        sp_w1 = topo.create_submission_parameter('w1', type_=int)
        sp_w2 = topo.create_submission_parameter('modulo', type_=int)
        
        s = topo.source(range(67)).set_parallel(sp_w1)
        s = s.filter(lambda v : v % wrap_get_int_submission_val('w1') == 0)
        s = s.filter(lambda v : v % get_submission_time_value('w1', int) == 0)
        s = s.end_parallel()
        s = s.parallel(width=sp_w2)
        s = s.filter(is_dividable_by_modulo_submission_param)
        s = s.end_parallel()

        jc = JobConfig()
        jc.submission_parameters['w1'] = 3
        jc.submission_parameters['modulo'] = 5
        jc.add(self.test_config)
     
        tester = Tester(topo)
        tester.contents(s,[0,15,30,45,60]*3, ordered=False)
        tester.test(self.test_ctxtype, self.test_config)

    def test_no_type_with_ec(self):

        topo = Topology()
        topo.create_submission_parameter('p1')
        
        s = topo.source(['Hello {}!'])
        s = s.map(greet)

        jc = JobConfig()
        jc.submission_parameters['p1'] = 'Rolef'
        jc.add(self.test_config)
     
        tester = Tester(topo)
        tester.contents(s,['Hello Rolef!'])
        tester.test(self.test_ctxtype, self.test_config)

    def test_bool_default_with_ec(self):

        topo = Topology()
        topo.create_submission_parameter('pFalse', False, bool)
        topo.create_submission_parameter('pTrue', False, bool)
        
        s = topo.source([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18])
        # passes through all numbers that cannot be divided by 2, when pFalse is false
        o = s.filter(DivideTester('pFalse', 2))
        # passes through all numbers that can be divided by 3, when pTrue is true
        s = o.filter(DivideTester('pTrue', 3))
        jc = JobConfig()
        #jc.submission_parameters['pFalse'] = False # leave the default
        jc.submission_parameters['pTrue'] = True
        jc.add(self.test_config)
     
        tester = Tester(topo)
        tester.contents(s,[3, 9, 15]) # all odd multiples of 3
        tester.test(self.test_ctxtype, self.test_config)


class TestDistributedSubmissionParams(TestSubmissionParams):
    """ Test submission params (distributed).
    """
    def setUp(self):
        Tester.setup_distributed(self)
        self.test_config[ConfigParams.SSL_VERIFY] = False

    def test_spl(self):
        """
        Test passing as an SPL parameter.
        """
        N=22
        G='hey'
        t = ''.join(random.choice('0123456789abcdef') for x in range(20))
        topic = 'topology/test/python/' + t
       
        topo = Topology()
        spTopic = topo.create_submission_parameter('mytopic')
        spGreet = topo.create_submission_parameter('greeting')

        self.assertIsNone(spTopic())
        self.assertIsNone(spGreet())

        sch = StreamSchema('tuple<uint64 seq, rstring s>')
        b = op.Source(topo, "spl.utility::Beacon", sch,
            params = {'initDelay': 10.0, 'period': 0.02, 'iterations':N})
        b.seq = b.output('IterationCount()')
        b.s = b.output(spGreet)
     
        p = op.Sink("com.ibm.streamsx.topology.topic::Publish", b.stream,
            params={'topic': topic})

        s = op.Source(topo, "com.ibm.streamsx.topology.topic::Subscribe", sch,
            params = {'streamType': sch, 'topic': spTopic})

        jc = JobConfig()
        jc.submission_parameters['mytopic'] = topic
        jc.submission_parameters['greeting'] = G
        jc.add(self.test_config)

        tester = Tester(topo)
        tester.tuple_count(s.stream, N)
        #tester.run_for(300)
        tester.contents(s.stream, [{'seq':i, 's':G} for i in range(N)])
        tester.test(self.test_ctxtype, self.test_config)

class TestSasSubmissionParams(TestDistributedSubmissionParams):
    """ Test submission params (service).
    """
    def setUp(self):
        Tester.setup_streaming_analytics(self, force_remote_build=True)

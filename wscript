#!/usr/bin/env python
from waflib.extras.test_base import summary

def depends(ctx):
    pass

def options(opt):
    opt.load('compiler_c')
    opt.load('compiler_cxx')
    opt.load('boost')
    opt.load('gtest')

    sopts = opt.add_option_group('SCtrlTP / Software ARQ')
    sopts.add_option('--nowarnings', action='store_true', default=False, help='Disable compiler warnings aka hardy-style')

    sopts.add_withoption('rttadj',      default=True,  help='RTT estimate to calculate timeout value')
    sopts.add_withoption('congav',      default=True,  help='Congestion avoidance algorithm (EXPERIMENTAL)')
    # TODO: fixup bpf filter => new packet format
    sopts.add_withoption('bpf',         default=False, help='Filtering by Berkeley Packet Filtering program attached to socket')
    sopts.add_withoption('hpet',        default=False, help='A high precision event timer for better resend timing')
    # TODO: stage1-specific; but we could implement multi-client stuff for stage2
    sopts.add_withoption('routing',     default=False, help='Queue/nathan mapping and nathan locking')
    sopts.add_withoption('packet-mmap', default=False, help='Memory mapped I/O (syscall free) with kernel')
    sopts.add_withoption('onelockfifo', default=True, help='Shared FIFOs with only one instead of two locks')
    sopts.add_withoption('sctrltp-python-bindings', default=True,
                         help='Toggle the generation and build of sctrltp python bindings')

    opt.recurse('pysctrltp')


def configure(conf):
    conf.load('compiler_c')
    conf.load('compiler_cxx')
    conf.load('boost')
    conf.load('gtest')

    conf.find_program('ctags', var='CTAGS', mandatory=False)
    if (conf.env.CTAGS):
        conf.env.CTAGS_DIRS = (conf.path.bld_dir(), '.')

    from subprocess import Popen, PIPE
    LEVEL1_DCACHE_LINESIZE = int(Popen(['getconf', 'LEVEL1_DCACHE_LINESIZE'], stdout=PIPE).communicate()[0].rstrip())
    conf.msg('level 1 cache line size', str(LEVEL1_DCACHE_LINESIZE))

    conf.define('L1D_CLS', LEVEL1_DCACHE_LINESIZE)

    from waflib.Options import options as o
    if o.with_rttadj :      conf.define('WITH_RTTADJ',      1)
    if o.with_congav :      conf.define('WITH_CONGAV',      1)
    if o.with_packet_mmap : conf.define('WITH_PACKET_MMAP', 1)
    if o.with_bpf :         conf.define('WITH_BPF',         1)
    if o.with_routing :     conf.define('WITH_ROUTING',     1)
    if o.with_hpet :        conf.define('WITH_HPET',        1)
    assert not o.with_hpet # it's broken currently? FIXME, check on AMTHosts
    if o.with_onelockfifo : conf.define('WITH_ONELOCKFIFO', 1)
    conf.write_config_header('include/sctrltp/build-config.h')


    # compile flags for libsctrl
    from waflib import Options
    if not conf.env.CFLAGS:
        conf.env.CFLAGS  = ('-Os -g -fPIC').split()
    if not Options.options.nowarnings:
        conf.env.CFLAGS += ('-Wall -Wextra -Winline').split()
        #conf.env.CFLAGS += ('-Werror -Wstrict-overflow=5').split() # old-gcc whines: -Wstrict-overflow=5
        #conf.env.CFLAGS += ('-Wno-long-long -Wno-sign-compare').split()
    conf.env.CFLAGS += ['-DSCTRL_PARALLEL']

    # library definitions
    conf.check_cxx(lib='rt', uselib_store='RT', mandatory=1)
    conf.check_cxx(lib='m', uselib_store='M', mandatory=1)
    conf.check_cxx(lib='pthread', uselib_store='PTHREAD', mandatory=1)

    conf.check_boost(lib='system', uselib_store='BOOST4SCTRLTPARQSTREAM')
    conf.check_boost(lib='system program_options', uselib_store='BOOST4SCTRLTPTESTS')

    if getattr(conf.options, 'with_sctrltp_python_bindings', True):
        conf.recurse('pysctrltp')


def build(bld):
    bld(target          = 'sctrltp_inc',
        export_includes = 'include'
    )

    # the SCtrl headers
    bld(target          = 'sctrl_inc',
        use             = 'RT PTHREAD',
        includes        = 'include',
        export_includes = 'include',
    )

    bld.recurse('src')
    bld.recurse('tests')

    bld.objects (
        target          = 'arqstream_obj',
        source          = 'src/ARQStream.cpp',
        use             = ['sctrltp_inc', 'sctrl_inc', 'hostarq', 'sctrl', 'BOOST4SCTRLTPARQSTREAM'],
        cxxflags        = '-fPIC',
    )

    bld.objects (
        target   = 'hostarq_loopback_test_obj',
        source   = 'tools/LoopbackTest.cpp',
        use      = ['arqstream_obj', 'BOOST4SCTRLTPTESTS'],
        cxxflags = '-fPIC',
    )

    bld.program (
        target       = 'hostarq_loopback_manual_test',
        source       = ['tools/ManualTest.cpp'],
        use          = ['hostarq_loopback_test_obj'],
        install_path = '${PREFIX}/bin',
    )

    bld.program (
        features     = 'cxx cxxprogram gtest',
        target       = 'hostarq_loopback_automated_test',
        source       = ['tools/AutomatedTest.cpp'],
        use          = [ 'hostarq_loopback_test_obj'],
        skip_run     = True,
        install_path = '${PREFIX}/bin',
    )

    bld.program (
        features     = 'cxx cxxprogram gtest',
        target       = 'hostarq_test_locking',
        source       = ['tests/test-locking.cpp', 'src/us_sctp_atomic.cpp'],
        use          = ['PTHREAD', 'sctrltp_inc'],
        skip_run     = True,
        install_path = '${PREFIX}/bin',
    )

    bld.program (
        features     = 'cxx cxxprogram gtest',
        target       = 'hostarq_test_fifo',
        source       = ['tests/test-fifo.cpp', 'src/us_sctp_atomic.cpp', 'src/sctp_fifo.cpp'],
        use          = ['PTHREAD', 'RT', 'sctrltp_inc'],
        skip_run     = True,
        install_path = '${PREFIX}/bin',
    )

    if getattr(bld.options, 'with_sctrltp_python_bindings', True):
        bld.recurse('pysctrltp')

    bld.add_post_fun(summary)


# for hostarq's runtime dependency on hostarq_daemon
from waflib.TaskGen import feature, after_method
@feature('*')
@after_method('process_use')
def post_the_other_dependencies(self):
    deps = getattr(self, 'depends_on', [])
    for name in set(self.to_list(deps)):
        other = self.bld.get_tgen_by_name(name)
        other.post()

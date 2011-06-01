#!/usr/bin/python
# vim: set fileencoding=utf-8 :


import SCons
import SCons.Script as scons
from distutils import sysconfig, version
import os, sys, re, platform

EnsurePythonVersion(2, 5)
EnsureSConsVersion(2, 0)

colors = {}
colors['cyan']   = '\033[96m'
colors['purple'] = '\033[95m'
colors['blue']   = '\033[94m'
colors['green']  = '\033[92m'
colors['yellow'] = '\033[93m'
colors['red']    = '\033[91m'
colors['end']    = '\033[0m'

#If the output is not a terminal, remove the colors
if not sys.stdout.isatty():
   for key, value in colors.iteritems():
      colors[key] = ''

env = Environment(tools = ['default', 'gettext'])

AddOption(
    "--verbose",
    action="store_true",
    dest="verbose_flag",
    default=False,
    help="verbose output"
)

if not GetOption("verbose_flag"):
    env["GETTEXTSTR"] = \
            '%(blue)sCompiling%(purple)s: %(yellow)s$SOURCE%(end)s' % colors,
    env["CXXCOMSTR"] = \
            '%(blue)sCompiling%(purple)s: %(yellow)s$SOURCE%(end)s' % colors,
    env["CCCOMSTR"] = \
            '%(blue)sCompiling%(purple)s: %(yellow)s$SOURCE%(end)s' % colors,
    env["SHCCCOMSTR"] = \
            '%(blue)sCompiling shared%(purple)s: %(yellow)s$SOURCE%(end)s' % colors,
    env["SHCXXCOMSTR"] = \
            '%(blue)sCompiling shared%(purple)s: %(yellow)s$SOURCE%(end)s' % colors,
    env["ARCOMSTR"] = \
            '%(red)sLinking Static Library%(purple)s: %(yellow)s$TARGET%(end)s' % colors,
    env["RANLIBCOMSTR"] = \
            '%(red)sRanlib Library%(purple)s: %(yellow)s$TARGET%(end)s' % colors,
    env["SHLINKCOMSTR"] = \
            '%(red)sLinking Shared Library%(purple)s: %(yellow)s$TARGET%(end)s' % colors,
    env["LINKCOMSTR"] = \
            '%(red)sLinking Program%(purple)s: %(yellow)s$TARGET%(end)s' % colors,
    env["INSTALLSTR"] = \
            '%(green)sInstalling%(purple)s: %(yellow)s$SOURCE%(purple)s => %(yellow)s$TARGET%(end)s' % colors

AddOption(
    '--prefix',
    dest='prefix',
    type='string',
    nargs=1,
    action='store',
    metavar='DIR',
    default='/usr',
    help='installation prefix'
)
prefix = GetOption('prefix')

AddOption(
    '--bindir',
    dest='bindir',
    type='string',
    nargs=1,
    action='store',
    metavar='DIR',
    default='%s/bin' % prefix,
    help='installation binary files prefix'
)

bindir = GetOption('bindir')

# below three variables are not yet used
libdir = "%s/lib" % prefix
includedir = "%s/include" % prefix
datadir = "%s/share" % prefix

AddOption(
    "--db-path",
    dest="db_path",
    action="store",
    metavar="DIR",
    nargs=1,
    default='/tmp/minidlna',
    help="full path of the file database."
)

AddOption(
    "--log-path",
    dest="log_path",
    action="store",
    metavar="DIR",
    nargs=1,
    default='/tmp/minidlna',
    help="full path of the log directory."
)

AddOption(
    "--disable-bsd-daemon",
    dest="bsd_daemon",
    action="store_false",
    default=True,
    help="use home brew daemon functions instead of utilizing BSD daemon."
)

AddOption(
    "--disable-nls",
    dest="enable_nls",
    action="store_false",
    default=True,
    help="Disable internationalization using gettext."
)

db_path = GetOption("db_path")
log_path = GetOption("log_path")
bsd_daemon = GetOption("bsd_daemon")
# Configuration:

checked_packages = dict()
def CheckPKGConfig(context, version):
    context.Message( 'Checking for pkg-config... ' )
    ret = context.TryAction('pkg-config --atleast-pkgconfig-version=%s' % version)[0]
    context.Result( ret )
    return ret

def CheckPKG(context, name):
    context.Message( 'Checking for %s... ' % name )
    key = re.sub(r"(\w+)(?: (?:\=|\<|\>) \S+)?",r"\1", name)
    if key in checked_packages:
        context.Result(checked_packages[key])
        return checked_packages[key]

    ret = context.TryAction('pkg-config --exists \'%s\'' % name)[0]

    context.sconf.config_h_text += "\n"
    context.sconf.Define(
        'HAVE_%s' % key.upper().replace('\W','_'),
        1, 
        "Define to 1 if you have the `%(name)s' package installed" 
        % { 'name': name }
    )

    depends = os.popen('pkg-config --print-requires \'%s\'' %
                       name).readline()

    depends = re.findall(r"(\w+(?: (?:\=|\<|\>) \S+)?)", depends)
    context.Result( ret )
    for dep in depends:
        ret &= context.sconf.CheckPKG(dep.strip())
    checked_packages[key] = ret
    return ret

conf = Configure(
    env, 
    config_h="src/config.h",
    custom_tests = {
    'CheckPKGConfig' : CheckPKGConfig,
    'CheckPKG' : CheckPKG
    }
)    

env.Append(CCFLAGS='-Wall -g -O3')

if not env.GetOption('clean') and not env.GetOption('help'):
    if 'CC' in os.environ:
        env.Replace(CC = os.environ['CC'])
        print(">> Using compiler " + os.environ['CC'])
    
    if 'CFLAGS' in os.environ:
        env.Replace(CFLAGS = os.environ['CFLAGS'])
        print(">> Appending custom build flags : " + os.environ['CFLAGS'])
    
    if 'LDFLAGS' in os.environ:
        env.Append(LINKFLAGS = os.environ['LDFLAGS'])
        print(">> Appending custom link flags : " + os.environ['LDFLAGS'])
    
    
    if not conf.CheckPKGConfig('0.15.0'):
        print 'pkg-config >= 0.15.0 not found.'
        Exit(1)
    
    if not conf.CheckPKG('libavformat'):
        print 'libavformat not found.'
        Exit(1)
    
    if not conf.CheckPKG('libavutil'):
        print 'libavutil not found.'
        Exit(1)
    
    if not conf.CheckPKG('libavcodec'):
        print 'libavcodec not found.'
        Exit(1)
    
    if not conf.CheckPKG('sqlite3'):
        print 'sqlite3 not found.'
        Exit(1)
    
    if not conf.CheckPKG('libexif'):
        print 'libexif not found.'
        Exit(1)
    
    if not conf.CheckPKG('id3tag'):
        print 'id3tag not found.'
        Exit(1)
    
    if not conf.CheckPKG('flac'):
        print 'flac not found.'
        Exit(1)
    
    if not conf.CheckPKG('ogg'):
        print 'ogg not found.'
        Exit(1)
    
    if not conf.CheckPKG('vorbis'):
        print 'vorbis not found.'
        Exit(1)
    
    if not conf.CheckLib('pthread'):
        print 'pthread library not found'
        Exit(1)
    
    if not conf.CheckLib('jpeg'):
        print 'jpeg library not found'
        Exit(1)
    
    if conf.CheckCHeader('sys/inotify.h'):
        conf.config_h_text += "\n"
        conf.Define('HAVE_INOTIFY_H',1)
    
    conf.CheckCHeader('iconv.h')
    
    if GetOption("enable_nls") and conf.CheckCHeader('libintl.h'):
        conf.config_h_text += "\n"
        conf.Define('ENABLE_NLS',1)
    
    if bsd_daemon:
        conf.config_h_text += "\n"
        conf.Define('USE_DAEMON', 1, "Use BSD daemon() instead of home made function")
    
    conf.config_h_text += "\n"
    conf.Define("DEFAULT_DB_PATH", '"%s"' % db_path, "Default database path")
    conf.config_h_text += "\n"
    conf.Define("DEFAULT_LOG_PATH", '"%s"' % log_path, "Default log path")
    
    
    os_url = ""
    os_name = platform.system();
    os_version = version.LooseVersion(platform.release());
    tivo = netgear = readynas = False
    pnpx = 0
    
    if os_name == "OpenBSD":
        
        # rtableid was introduced in OpenBSD 4.0
        if os_version >= "4.0":
            conf.Define("PFRULE_HAS_RTABLEID")
        
        # from the 3.8 version, packets and bytes counters are double : in/out
        if os_version >= "3.8":
            conf.Define("PFRULE_INOUT_COUNTS")
    
        os_url = "http://www.openbsd.org/"
    
    elif os_name == "FreeBSD":
        for line in open('/usr/include/sys/param.h'):
            m = re.match(r"#define __FreeBSD_version (\d+)", line)
            if m and m[1] >= 700049:
                conf.Define("PFRULE_INOUT_COUNTS")
                break
        os_url = "http://www.freebsd.org/"
    elif os_name == "pfSense":
        os_url = "http://www.pfsense.org/"
    elif os_name == "NetBSD":
        os_url = "http://www.freebsd.org/"
    elif os_name == "SunOS":
        conf.Define("USE_IPF","1")
        conf.Define("LOG_PERROR","0")
        conf.Define("SOLARIS_KSTATS","1")
        # TODO fix typedefs
        os_url = "http://www.sun.com/solaris/"
    elif os_name == "Linux":
        os_url = "http://www.kernel.org/"
        dist = platform.linux_distribution()
        os_name = dist[0]
        os_version = dist[1]
    
        # NETGEAR ReadyNAS special case
        if os.path.exists("/etc/raidiator_version"):
            radiator_version = open("/etc/radiator_version").readline()
            m = re.match("^(.*?)!!version=(.*?),", radiator_version)
            os_name = m[1]
            os_version = m[2]
            os_url = "http://www.readynas.com/"
            log_path = "/var/log"
            db_path = "/var/cache/minidlna"
            tivo = netgear = readynas = True
            pnpx = 5
        elif dist[0] == "debian":
            os_url = "http://www.debian.org"
            log_path = "/var/log"
        else :
            pass # unknown linux dist
    else :
        print("Unknown operating system '%s'" % os_name)
        Exit(2)
    
    conf.config_h_text += "\n"
    conf.Define('OS_VERSION', '"%s"'%str(os_version), "Version of the operating system")
    conf.config_h_text += "\n"
    conf.Define('OS_NAME', '"%s"'%str(os_name), "Name of the operating system")
    conf.config_h_text += "\n"
    conf.Define('OS_URL', '"%s"'%str(os_url), "URL for operating system")

    if tivo:
        conf.config_h_text += "\n"
        conf.Define('TIVO_SUPPORT', 1, "Compile in TiVo support.")
    if netgear:
        conf.config_h_text += "\n"
        conf.Define('NETGEAR', 1, "Enable NETGEAR-specific tweaks.")
    if readynas:
        conf.config_h_text += "\n"
        conf.Define('READYNAS', 1, "Enable ReadyNAS-specific tweaks.")
    
    conf.config_h_text += "\n"
    conf.Define('PNPX', pnpx, "Enable PnPX support.")


env.ParseConfig('pkg-config --cflags --libs  libavformat libavutil libavcodec sqlite3 libexif id3tag flac ogg vorbis')

env.Append(CPPDEFINES=['_GNU_SOURCE', ('_FILE_OFFSET_BITS','64'), '_REENTRANT',
                      'HAVE_CONFIG_H'])


env = conf.Finish()
# -----

if GetOption("enable_nls"):
    gettext_po_files = [
        "po/da.po",
        "po/de.po",
        "po/es.po",
        "po/fr.po",
        "po/it.po",
        "po/ja.po",
        "po/nb.po",
        "po/nl.po",
        "po/ru.po",
        "po/sl.po",
        "po/sv.po"
    ];

    for po_file in gettext_po_files:
        mo_file = env.Gettext(po_file)
        localedir = "%(datadir)s/locale/%(lang)s/LC_MESSAGES" % {
            'datadir': datadir,
            'lang': re.sub(r"po\/(\w+)\.po", r"\1", po_file)
        }
        env.Alias('install', env.InstallAs("%s/minidlna.mo"%localedir, mo_file))

o = env.SConscript('src/SConscript', exports=['env', 'bindir'])

env.Install(bindir, o)

env.Alias('install', bindir)

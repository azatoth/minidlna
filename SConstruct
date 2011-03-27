#!/usr/bin/python
# vim: set fileencoding=utf-8 :


import SCons
import SCons.Script as scons
from distutils import sysconfig, version
import platform
import os
import re

EnsurePythonVersion(2, 5)
EnsureSConsVersion(2, 0)

env = Environment()
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
    "--enable-bsd-daemon",
    dest="bsd_daemon",
    action="store_true",
    default=True,
    help="utilize BSD daemon() instead of using home brew daemonize function."
)

db_path = GetOption("db_path")
log_path = GetOption("log_path")
bsd_daemon = GetOption("bsd_daemon")
# Configuration:

def CheckPKGConfig(context, version):
    context.Message( 'Checking for pkg-config... ' )
    ret = context.TryAction('pkg-config --atleast-pkgconfig-version=%s' % version)[0]
    context.Result( ret )
    return ret

def CheckPKG(context, name):
    context.Message( 'Checking for %s... ' % name )
    ret = context.TryAction('pkg-config --exists \'%s\'' % name)[0]
    context.sconf.config_h_text += "\n"
    context.sconf.Define(
        'HAVE_%s' % name.upper().replace('\W','_'), 
        1, 
        "Define to 1 if you have the `%(name)s' package installed" 
        % { 'name': name }
    )
    context.Result( ret )
    return ret

conf = Configure(
    env, 
    config_h="config.h",
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
    
    if conf.CheckCHeader('libintl.h'):
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

minidlna_sources = [
    "minidlna.c", "upnphttp.c", "upnpdescgen.c", "upnpsoap.c",
    "upnpreplyparse.c", "minixml.c", 
    "getifaddr.c", "daemonize.c", "upnpglobalvars.c", 
    "options.c", "minissdp.c", "uuid.c", "upnpevents.c", 
    "sql.c", "utils.c", "metadata.c", "scanner.c", "inotify.c", 
    "tivo_utils.c", "tivo_beacon.c", "tivo_commands.c", 
    "tagutils/textutils.c", "tagutils/misc.c", "tagutils/tagutils.c",
    "playlist.c", "image_utils.c", "albumart.c", "log.c"
]
testupnpdescgen_sources = [ "testupnpdescgen.c", "upnpdescgen.c" ]

minidlna = env.Program(target = "minidlna", source = minidlna_sources);
testupnpdescgen = env.Program(target = "testupnpdescgen", source = testupnpdescgen_sources);

env.Install(bindir, [minidlna, testupnpdescgen])

env.Alias('install', bindir)

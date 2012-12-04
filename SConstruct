import os, sys, io
import errno
import logging
import ConfigParser
from SCons.Node import FS
from SCons.Script import Mkdir,Chmod,Copy,WhereIs

logger = logging.getLogger('scons-qserv')
formatter = logging.Formatter('%(asctime)s %(levelname)s %(message)s')
# this level can be reduce for each handler
logger.setLevel(logging.DEBUG)

#file_handler = logging.FileHandler('scons.log')
#file_handler.setFormatter(formatter)
#file_handler.setLevel(logging.DEBUG)
#logger.addHandler(file_handler) 

console_handler = logging.StreamHandler()
console_handler.setFormatter(formatter)
console_handler.setLevel(logging.DEBUG)
logger.addHandler(console_handler) 

# this file must be placed in main scons directory
# TODO : see quattor to read a default file and overload it
config_file_name="qserv-build.conf"

sample_config = """
# WARNING : these variables mustn't be changed once the install process is started
[DEFAULT]
version = qserv-dev
basedir = /opt/%(version)s
logdir = %(basedir)s/var/log

[qserv]
# Qserv rpc service port is 7080 but is hard-coded

# Tree possibles values :
# mono
# master
# worker
node_type=mono

# Qserv master DNS name
master=qserv-master.in2p3.fr

# Geometry file will be downloaded by default in git master branch
# but a source directory may be specified 
# it could be retrieved for exemple with : git clone git://dev.lsstcorp.org/LSST/DMS/geom
# geom=/home/user/geom

[xrootd]
cmsd_manager_port=4040
xrootd_port=1094

[mysql-proxy]

port=4040

[mysqld]

port=13306

pass='changeme'
#datadir=/data/$(version)/mysql
datadir=%(basedir)s/var/lib/mysql

[lsst]
      
# Where to download LSST data
# Example: PT1.1 data should be in $(datadir)/pt11/
datadir=/data/lsst 
"""

env = Environment()

def read_config():
    buildConfigFile=Dir('.').srcnode().abspath+"/"+config_file_name
    logger.debug("Reading build config file : %s" % buildConfigFile)
    config = ConfigParser.SafeConfigParser()
    config.readfp(io.BytesIO(sample_config))
    config.read(buildConfigFile)

    logger.debug("Build configuration : ")
    for section in config.sections():
       logger.debug("[%s]" % section)
       for option in config.options(section):
        logger.debug("'%s' = '%s'" % (option, config.get(section,option)))

    return config 

def is_readable_dir(dir):
    """
    Test is a directory is readable.
    Return a couple (success,message), where success is a boolean and message a string
    """
    try:
        os.listdir(dir)
    except BaseException as e:
        return (False,"No read access : %s" % (e));
    return (True,"")

def is_writeable_dir(dir):
    """
    Test is a directory exists, if no try to create it, if yes test if it is writeable.
    Return a couple (success,message), where success is a boolean and message a string
    """
    try:
	if (os.path.exists(dir)):
            filename="%s/test.check" % dir
            f = open(filename,'w')
            f.close()
            os.remove(filename)
        else:
            Execute(Mkdir(dir))
    except IOError as e:
        if (e.errno==errno.ENOENT) :
            return (False,"No write access to directory : %s" % (dir));
    except BaseException as e:
        return (False,"No write access : %s" % (e));
    return (True,"")

def init_target(target, source, env):

    check_success=True

    ret =  is_writeable_dir(config.get("qserv","base_dir")) 
    if (not ret[0]):
    	logging.fatal("Checking Qserv base directory : %s" % ret[1])
        check_success=False   
 
    ret =  is_writeable_dir(config.get("qserv","log_dir")) 
    if (not ret[0]):
    	logging.fatal("Checking Qserv log directory : %s" % ret[1])
        check_success=False    

    ret =  is_writeable_dir(config.get("mysqld","data_dir")) 
    if (not ret[0]):
    	logging.fatal("Checking MySQL data directory : %s" % ret[1])
        check_success=False    

    ret =  is_readable_dir(config.get("lsst","data_dir")) 
    if (not ret[0]):
    	logging.fatal("Checking LSST data directory : %s" % ret[1])
        check_success=False    
    
    if not check_success :
        sys.exit(1)
    else:
        logger.info("Qserv initial directory structure analysis succeeded")    

def symlink(target, source, env):
    os.symlink(os.path.abspath(str(source[0])), os.path.abspath(str(target[0])))

config = read_config()

log_dir         = config.get("qserv","log_dir")
mysqld_data_dir = config.get("mysqld","data_dir")
lsst_data_dir   = config.get("lsst","data_dir")

#symlink_builder = Builder(action = "ln -s ${SOURCE.file} ${TARGET.file}", chdir = True)
symlink_builder = Builder(action = symlink, chdir = True)

env.Append(BUILDERS = {"Symlink" : symlink_builder})

mylib_link = env.Symlink("toto", "qserv-env.sh")

env.Alias('symlink', mylib_link)

#if Execute(action=init_action):
#        # A problem occurred while making the temp directory.
#        Exit(1)

init_bld = env.Builder(action=init_target)
env.Append(BUILDERS = {'Init' : init_bld})
init = env.Init( ['always_do_it'], [])
env.Alias('init', init)

#Execute(Mkdir('tutu'))

#qserv_init_alias = env.Alias('qserv_inYit', env.Qserv_init())
#env.Alias('install', [qserv_init_alias])

####################################
# SCons build system for Pedigree
## Tyler Kennedy (AKA Linuxhq AKA TkTech)
####################################

import os.path
Import(['env'])

####################################
# SCons build system for Pedigree
## Tyler Kennedy (AKA Linuxhq AKA TkTech)
####################################

import os
import shutil
Import(['env'])

# Subsystems always get built first
subdirs = [
    'src/subsys/posix',
    'src/subsys/pedigree-c'
]

# Currently the native API is only supported on x86 (not x86-64 or other
# architectures).
if env['ARCH_TARGET'] == 'X86':
    subdirs += ['src/subsys/native']

# Then modules and the system proper get built
subdirs += ['src/modules', 'src/system/kernel']

# On X86, X64 and PPC we build applications and LGPL libraries
if env['ARCH_TARGET'] in ['X86', 'X64', 'PPC']:
    subdirs += ['src/user', 'src/lgpl']
if not env['ARCH_TARGET'] in ['X86', 'X64']:
    subdirs += ['src/system/boot']

SConscript([os.path.join(i, 'SConscript') for i in subdirs],exports = ['env'])

rootdir = env.Dir("#").abspath
builddir = env.Dir("#" + env["PEDIGREE_BUILD_BASE"]).abspath
imagedir = env.Dir(env['PEDIGREE_IMAGES_DIR']).abspath

# Build the configuration database (no dependencies)
configdb = env.File(builddir + '/config.db')
config_file = env.File('#src/modules/system/config/config_database.h')

# Complete the build - image creation
floppyimg = env.File(builddir + '/floppy.img')
hddimg = env.File(builddir + '/hdd.img')
cdimg = env.File(builddir + '/pedigree.iso')

configSchemas = []
for i in os.walk(env.Dir("#src").abspath):
    configSchemas += map(lambda x: i[0] + '/' + x, filter(lambda y: y == 'schema', i[2]))
env.Command(configdb, configSchemas, '@cd ' + rootdir + ' && python ./scripts/buildDb.py')

def makeHeader(target, source, env):
    f = open(target[0].path, "w")
    f.write("/* Made from " + source[0].path + " */\n")
    f.write("/* Autogenerated by the build system, do not edit. */\n")
    
    f.write("static uint8_t file[] = {\n");
    i = open(source[0].path, "rb")
    data = i.read()
    for i in data:
        f.write("0x%02x,\n" % (ord(i)))
    f.write("};\n")
    f.close()
    
if('STATIC_DRIVERS' in env['CPPDEFINES']):
    env.Command(config_file, configdb, makeHeader)

# TODO: If any of these commands fail, they WILL NOT STOP the build!

def buildImageLosetup(target, source, env):
    if env['verbose']:
        print '      Creating ' + os.path.basename(target[0].path)
    else:
        print '      Creating \033[32m' + os.path.basename(target[0].path) + '\033[0m'

    builddir = env.Dir("#" + env["PEDIGREE_BUILD_BASE"]).abspath
    imagedir = env.Dir(env['PEDIGREE_IMAGES_DIR']).abspath
    appsdir = env.Dir(env['PEDIGREE_BUILD_APPS']).abspath
    modsdir = env.Dir(env['PEDIGREE_BUILD_MODULES']).abspath
    drvsdir = env.Dir(env['PEDIGREE_BUILD_DRIVERS']).abspath

    outFile = target[0].path
    imageBase = source[0].path
    offset = 32256
    source = source[1:]

    # Copy the base image to the destination, overwriting any image that
    # may already exist there.
    if('gz' in imageBase):
        shutil.copy(imageBase, "./tmp.tar.gz")
        os.system("tar -xzf tmp.tar.gz")
        os.system("rm -f tmp.tar.gz")
        shutil.move(os.path.basename(imageBase).replace('tar.gz', 'img'), outFile)
    else:
        shutil.copy(imageBase, outFile)

    os.mkdir("tmp")
    os.system("sudo mount -o loop,rw,offset=" + str(offset) + " " + outFile + " ./tmp")

    # Perhaps the menu.lst should refer to .pedigree-root :)
    os.system("sudo cp " + builddir + "/config.db ./tmp/.pedigree-root")

    # Copy the kernel, initrd, and configuration database
    for i in source[0:3]:
        os.system("sudo cp " + i.abspath + " ./tmp/boot/")
    source = source[3:]

    # Copy each input file across
    for i in source:
        otherPath = ''
        search, prefix = imagedir, ''

        # Applications
        if appsdir in i.abspath:
            search = appsdir
            prefix = '/applications'

        # Modules
        elif modsdir in i.abspath:
            search = modsdir
            prefix = '/system/modules'

        # Drivers
        elif drvsdir in i.abspath:
            search = drvsdir
            prefix = '/system/modules'

        # Additional Libraries
        elif builddir in i.abspath:
            search = builddir
            prefix = '/libraries'

        otherPath = prefix + i.abspath.replace(search, '')

        # Clean out the last directory name if needed
        if(os.path.isdir(i.abspath)):
            otherPath = '/'.join(otherPath.split('/')[:-1])
            if(len(otherPath) == 0 or otherPath[0] != '/'):
                otherPath = '/' + otherPath

        os.system("sudo cp -R " + i.path + " ./tmp" + otherPath)

    os.system("sudo umount ./tmp")

    for i in os.listdir("tmp"):
        os.remove(i)
    os.rmdir("tmp")

def buildImageMtools(target, source, env):
    if env['verbose']:
        print '      Creating ' + os.path.basename(target[0].path)
    else:
        print '      Creating \033[32m' + os.path.basename(target[0].path) + '\033[0m'

    builddir = env.Dir("#" + env["PEDIGREE_BUILD_BASE"]).abspath
    imagedir = env.Dir(env['PEDIGREE_IMAGES_DIR']).abspath
    appsdir = env.Dir(env['PEDIGREE_BUILD_APPS']).abspath
    modsdir = env.Dir(env['PEDIGREE_BUILD_MODULES']).abspath
    drvsdir = env.Dir(env['PEDIGREE_BUILD_DRIVERS']).abspath

    outFile = target[0].path
    imageBase = source[0].path
    source = source[1:]

    # Copy the base image to the destination, overwriting any image that
    # may already exist there.
    if('gz' in imageBase):
        os.system("tar -xzf " + imageBase + " -C .")
        shutil.move(os.path.basename(imageBase).replace('tar.gz', 'img'), outFile)
    else:
        shutil.copy(imageBase, outFile)

    # Open for use in mtools
    mtsetup = env.File("#/scripts/mtsetup.sh").abspath
    os.system("sh " + mtsetup + " " + outFile + " > /dev/null 2>&1")

    destDrive = " C:"
    os.system("mcopy -Do " + builddir + "/config.db C:/.pedigree-root > /dev/null 2>&1; rm -f .pedigree-root")

    # Copy the kernel, initrd, and configuration database
    for i in source[0:3]:
        os.system("mcopy -Do " + i.abspath + " C:/boot > /dev/null 2>&1")
    source = source[3:]

    # Copy each input file across
    for i in source:
        otherPath = ''
        search, prefix = imagedir, ''

        # Applications
        if appsdir in i.abspath:
            search = appsdir
            prefix = '/applications'

        # Modules
        elif modsdir in i.abspath:
            search = modsdir
            prefix = '/system/modules'

        # Drivers
        elif drvsdir in i.abspath:
            search = drvsdir
            prefix = '/system/modules'

        # Additional Libraries
        elif builddir in i.abspath:
            search = builddir
            prefix = '/libraries'

        otherPath = prefix + i.abspath.replace(search, '')
        os.system("mcopy -bms -Do " + i.path + destDrive + otherPath + " > /dev/null 2>&1")

def buildCdImage(target, source, env):
    if env['verbose']:
        print '      Creating ' + os.path.basename(target[0].path)
    else:
        print '      Creating \033[32m' + os.path.basename(target[0].path) + '\033[0m'

    builddir = env.Dir("#" + env["PEDIGREE_BUILD_BASE"]).abspath
    pathToGrub = env.Dir("#images/grub").abspath
    configDb = source[0].abspath
    stage2_eltorito = "stage2_eltorito-" + env['ARCH_TARGET'].lower()
    shutil.copy(pathToGrub + "/" + stage2_eltorito, "./stage2_eltorito")

    cmd = "mkisofs -D -joliet -graft-points -quiet -input-charset ascii -R \
                 -b boot/grub/stage2_eltorito -no-emul-boot -boot-load-size 4 \
                 -boot-info-table -o " + target[0].path + " -V 'PEDIGREE' \
                 boot/grub/stage2_eltorito=./stage2_eltorito \
                 boot/grub/menu.lst=" + pathToGrub + "/menu.lst \
                 boot/kernel=" + source[2].abspath + " \
                 boot/initrd.tar=" + source[1].abspath + " \
                /livedisk.img=" + source[3].abspath + "\
                .pedigree-root=" + configDb
    os.system(cmd)

    os.remove("./stage2_eltorito")

if not env['ARCH_TARGET'] in ["X86", "X64", "PPC"]:
    print "No hard disk image being built, architecture doesn't need one."
#elif env["installer"]:
#    print "Oops, installer images aren't built yet. Tell pcmattman to write Python scripts"
#    print "to build these images, please."
else:
    # Define dependencies
    env.Depends(hddimg, 'libs')
    env.Depends(hddimg, 'apps')
    env.Depends(hddimg, 'initrd')
    env.Depends(hddimg, configdb)
    env.Depends(cdimg, hddimg) # Inherent dependency on libs/apps

    fileList = []

    # Build the disk images (whichever are the best choice for this system)
    if(env['havelosetup']):
        fileList += ["#/images/hdd_ext2.tar.gz"]
        buildImage = buildImageLosetup
    else:
        fileList += ["#/images/hdd_fat16.tar.gz"]
        buildImage = buildImageMtools

    # /boot directory
    fileList += [builddir + '/kernel/kernel', builddir + '/initrd.tar', configdb]

    for root, dirs, files in os.walk(imagedir):
        # Add directory names first
        for j in dirs:
            fileList += [root + '/' + j]

        # Then add filenames
        for j in files:
            fileList += [root + '/' + j]

        # Recursive copy is performed, so only grab the first directory
        break

    # Add apps to the input list
    fileList += [[i[0] + '/' + j for j in i[2]] for i in os.walk(builddir + '/apps')]

    # Add modules and drivers to the input list
    fileList += [[i[0] + '/' + j for j in i[2]] for i in os.walk(builddir + '/modules')]
    fileList += [[i[0] + '/' + j for j in i[2]] for i in os.walk(builddir + '/drivers')]

    # Add libraries
    fileList += [builddir + '/libc.so',
                 builddir + '/libm.so',
                 builddir + '/libpthread.so',
                 builddir + '/libSDL.so']

    # Build the hard disk image
    env.Command(hddimg, fileList, Action(buildImage, None))

    # Build the live CD ISO
    env.Command(cdimg, [configdb, builddir + '/initrd.tar', builddir + '/kernel/kernel', hddimg], Action(buildCdImage, None))


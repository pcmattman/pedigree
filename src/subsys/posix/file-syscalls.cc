/*
 * Copyright (c) 2008-2014, Pedigree Developers
 *
 * Please see the CONTRIB file in the root of the source tree for a full
 * list of contributors.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <syscallError.h>
#include <processor/types.h>
#include <processor/Processor.h>
#include <processor/MemoryRegion.h>
#include <processor/PhysicalMemoryManager.h>
#include <processor/VirtualAddressSpace.h>
#include <process/Process.h>
#include <utilities/Tree.h>
#include <vfs/File.h>
#include <vfs/LockedFile.h>
#include <vfs/MemoryMappedFile.h>
#include <vfs/Symlink.h>
#include <vfs/Directory.h>
#include <vfs/VFS.h>
#include <console/Console.h>
#include <network-stack/NetManager.h>
#include <network-stack/Tcp.h>
#include <utilities/utility.h>
#include <users/UserManager.h>

#include <Subsystem.h>
#include <PosixSubsystem.h>
#include <PosixProcess.h>

#include "file-syscalls.h"
#include "console-syscalls.h"
#include "pipe-syscalls.h"
#include "net-syscalls.h"

extern int posix_getpid();

//
// Syscalls pertaining to files.
//

#define CHECK_FLAG(a, b) (((a) & (b)) == (b))

#define GET_CWD() (Processor::information().getCurrentThread()->getParent()->getCwd())

static PosixProcess *getPosixProcess()
{
    Process *pStockProcess = Processor::information().getCurrentThread()->getParent();
    if(pStockProcess->getType() != Process::Posix)
    {
        return 0;
    }

    PosixProcess *pProcess = static_cast<PosixProcess *>(pStockProcess);
    return pProcess;
}

static File *traverseSymlink(File *file)
{
    /// \todo detect inability to access at each intermediate step.
    if(!file)
    {
        SYSCALL_ERROR(DoesNotExist);
        return 0;
    }

    Tree<File*, File*> loopDetect;
    while(file->isSymlink())
    {
        file = Symlink::fromFile(file)->followLink();
        if(!file)
        {
            SYSCALL_ERROR(DoesNotExist);
            return 0;
        }

        if(loopDetect.lookup(file))
        {
            SYSCALL_ERROR(LoopExists);
            return 0;
        }
        else
            loopDetect.insert(file, file);
    }

    return file;
}

static bool doChdir(File *dir)
{
    File *target = 0;
    if (dir->isSymlink())
    {
        target = traverseSymlink(dir);
        if (!target)
        {
            F_NOTICE("Symlink traversal failed.");
            SYSCALL_ERROR(DoesNotExist);
            return false;
        }
    }

    if (dir && (dir->isDirectory() || (dir->isSymlink() && target->isDirectory())))
    {
        File *pRealFile = dir;
        if (dir->isSymlink())
        {
            pRealFile = target;
        }

        // Only need execute permissions to enter a directory.
        if (!VFS::checkAccess(pRealFile, false, false, true))
        {
            return false;
        }

        Processor::information().getCurrentThread()->getParent()->setCwd(dir);
    }
    else if(dir && !dir->isDirectory())
    {
        SYSCALL_ERROR(NotADirectory);
        return false;
    }
    else
    {
        SYSCALL_ERROR(DoesNotExist);
        return false;
    }

    return true;
}

static bool doStat(const char *name, File *pFile, struct stat *st, bool traverse = true)
{
    if (traverse)
    {
        pFile = traverseSymlink(pFile);
        if(!pFile)
        {
            F_NOTICE("    -> Symlink traversal failed");
            return -1;
        }
    }

    int mode = 0;
    if (ConsoleManager::instance().isConsole(pFile) || (name && !StringCompare(name, "/dev/null")))
    {
        F_NOTICE("    -> S_IFCHR");
        mode = S_IFCHR;
    }
    else if (pFile->isDirectory())
    {
        F_NOTICE("    -> S_IFDIR");
        mode = S_IFDIR;
    }
    else if (pFile->isSymlink() || pFile->isPipe())
    {
        F_NOTICE("    -> S_IFLNK");
        mode = S_IFLNK;
    }
    else
    {
        F_NOTICE("    -> S_IFREG");
        mode = S_IFREG;
    }

    // Clear any cruft in the stat structure before we fill it.
    ByteSet(st, 0, sizeof(*st));

    uint32_t permissions = pFile->getPermissions();
    if (permissions & FILE_UR) mode |= S_IRUSR;
    if (permissions & FILE_UW) mode |= S_IWUSR;
    if (permissions & FILE_UX) mode |= S_IXUSR;
    if (permissions & FILE_GR) mode |= S_IRGRP;
    if (permissions & FILE_GW) mode |= S_IWGRP;
    if (permissions & FILE_GX) mode |= S_IXGRP;
    if (permissions & FILE_OR) mode |= S_IROTH;
    if (permissions & FILE_OW) mode |= S_IWOTH;
    if (permissions & FILE_OX) mode |= S_IXOTH;
    F_NOTICE("    -> " << Oct << mode);

    /// \todo expose number of links and number of blocks from Files
    st->st_dev   = static_cast<short>(reinterpret_cast<uintptr_t>(pFile->getFilesystem()));
    F_NOTICE("    -> " << st->st_dev);
    st->st_ino   = static_cast<short>(pFile->getInode());
    F_NOTICE("    -> " << st->st_ino);
    st->st_mode  = mode;
    st->st_nlink = 1;
    st->st_uid   = pFile->getUid();
    st->st_gid   = pFile->getGid();
    st->st_rdev  = 0;
    st->st_size  = static_cast<int>(pFile->getSize());
    st->st_atime = pFile->getAccessedTime();
    st->st_mtime = pFile->getModifiedTime();
    st->st_ctime = pFile->getCreationTime();
    st->st_blksize = static_cast<int>(pFile->getBlockSize());
    st->st_blocks = (st->st_size / st->st_blksize) + ((st->st_size % st->st_blksize) ? 1 : 0);

    return true;
}

static bool doChmod(File *pFile, mode_t mode)
{
    // Are we the owner of the file?
    User *pCurrentUser = Processor::information().getCurrentThread()->getParent()->getUser();

    size_t uid = pCurrentUser->getId();
    if (!(uid == pFile->getUid() || uid == 0))
    {
        // Not allowed - EPERM.
        // User must own the file or be superuser.
        SYSCALL_ERROR(NotEnoughPermissions);
        return false;
    }

    /// \todo Might want to change permissions on open file descriptors?
    uint32_t permissions = 0;
    if (mode & S_IRUSR) permissions |= FILE_UR;
    if (mode & S_IWUSR) permissions |= FILE_UW;
    if (mode & S_IXUSR) permissions |= FILE_UX;
    if (mode & S_IRGRP) permissions |= FILE_GR;
    if (mode & S_IWGRP) permissions |= FILE_GW;
    if (mode & S_IXGRP) permissions |= FILE_GX;
    if (mode & S_IROTH) permissions |= FILE_OR;
    if (mode & S_IWOTH) permissions |= FILE_OW;
    if (mode & S_IXOTH) permissions |= FILE_OX;
    pFile->setPermissions(permissions);

    return true;
}

static bool doChown(File *pFile, uid_t owner, gid_t group)
{
    // If we're root, changing is fine.
    size_t newOwner = pFile->getUid();
    size_t newGroup = pFile->getGid();
    if (owner != static_cast<uid_t>(-1))
    {
        newOwner = owner;
    }
    if (group != static_cast<gid_t>(-1))
    {
        newGroup = group;
    }

    // We can only chown the user if we're root.
    if (pFile->getUid() != newOwner)
    {
        User *pCurrentUser = Processor::information().getCurrentThread()->getParent()->getUser();
        if (pCurrentUser->getId())
        {
            SYSCALL_ERROR(NotEnoughPermissions);
            return false;
        }
    }

    // We can change the group to anything if we're root, but otherwise only
    // to a group we're a member of.
    if (pFile->getGid() != newGroup)
    {
        User *pCurrentUser = Processor::information().getCurrentThread()->getParent()->getUser();
        if (pCurrentUser->getId())
        {
            Group *pTargetGroup = UserManager::instance().getGroup(newGroup);
            if (!pTargetGroup->isMember(pCurrentUser))
            {
                SYSCALL_ERROR(NotEnoughPermissions);
                return false;
            }
        }
    }

    // Update the file's uid/gid now that we've checked we're allowed to.
    if (pFile->getUid() != newOwner)
    {
        pFile->setUid(newOwner);
    }

    if (pFile->getGid() != newGroup)
    {
        pFile->setGid(newGroup);
    }

    return true;
}

bool normalisePath(String &nameToOpen, const char *name, bool *onDevFs)
{
    // Rebase /dev onto the devfs. /dev/tty is special.
    if (!StringCompare(name, "/dev/tty"))
    {
        // Get controlling console, unless we have none.
        Process *pProcess = Processor::information().getCurrentThread()->getParent();
        if (!pProcess->getCtty())
        {
            if (onDevFs)
                *onDevFs = true;
        }

        nameToOpen = name;
        return true;
    }
    else if (!StringCompareN(name, "/dev", StringLength("/dev")))
    {
        nameToOpen = "dev»";
        nameToOpen += (name + StringLength("/dev"));
        if (onDevFs)
            *onDevFs = true;
        return true;
    }
    else if (!StringCompareN(name, "/bin", StringLength("/bin")))
    {
        nameToOpen = "/applications";
        nameToOpen += (name + StringLength("/bin"));
        return true;
    }
    else if (!StringCompareN(name, "/etc", StringLength("/etc")))
    {
        nameToOpen = "/config";
        nameToOpen += (name + StringLength("/etc"));
        return true;
    }
    else if (!StringCompareN(name, "/tmp", StringLength("/tmp")))
    {
        nameToOpen = "scratch»";
        nameToOpen += (name + StringLength("/tmp"));
        return true;
    }
    else if (!StringCompareN(name, "/var/run", StringLength("/var/run")))
    {
        nameToOpen = "runtime»";
        nameToOpen += (name + StringLength("/var/run"));
        return true;
    }
    else if (!StringCompareN(name, "/@", StringLength("/@")))
    {
        // Absolute UNIX paths for POSIX stupidity.
        // /@/path/to/foo = /path/to/foo
        // /@/root»/applications = root»/applications
        const char *newName = name + StringLength("/@");
        if (*newName == '/')
            ++newName;
        nameToOpen = newName;
        return true;
    }
    else
    {
        nameToOpen = name;
        return false;
    }
}

int posix_close(int fd)
{
    F_NOTICE("close(" << fd << ")");
    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    FileDescriptor *pFd = pSubsystem->getFileDescriptor(fd);
    if (!pFd)
    {
        // Error - no such file descriptor.
        SYSCALL_ERROR(BadFileDescriptor);
        return -1;
    }

    // If this was a master psuedoterminal, we should unlock it now.
    if(ConsoleManager::instance().isConsole(pFd->file))
    {
        if(ConsoleManager::instance().isMasterConsole(pFd->file))
        {
            ConsoleManager::instance().unlockConsole(pFd->file);
        }
    }

    pSubsystem->freeFd(fd);
    return 0;
}

int posix_open(const char *name, int flags, int mode)
{
    if(!PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(name), PATH_MAX, PosixSubsystem::SafeRead))
    {
        F_NOTICE("open -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("open(" << name << ", " << ((flags & O_RDWR) ? "O_RDWR" : "") << ((flags & O_RDONLY) ? "O_RDONLY" : "") << ((flags & O_WRONLY) ? "O_WRONLY" : "") << ")");
    F_NOTICE("  -> actual flags " << flags);
    F_NOTICE("  -> mode is " << Oct << mode);
    
    // One of these three must be specified.
    if(!(CHECK_FLAG(flags, O_RDONLY) || CHECK_FLAG(flags, O_RDWR) || CHECK_FLAG(flags, O_WRONLY)))
    {
        F_NOTICE("One of O_RDONLY, O_WRONLY, or O_RDWR must be passed.");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    // verify the filename - don't try to open a dud file
    if (name[0] == 0)
    {
        F_NOTICE("  -> File does not exist (null path).");
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }

    // Lookup this process.
    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        F_NOTICE("  -> No subsystem for this process!");
        return -1;
    }

    PosixProcess *pPosixProcess = getPosixProcess();
    if (pPosixProcess)
    {
        mode &= ~pPosixProcess->getMask();
    }

    size_t fd = pSubsystem->getFd();

    File* file = 0;

    bool onDevFs = false;
    String nameToOpen;
    normalisePath(nameToOpen, name, &onDevFs);
    if (nameToOpen == "/dev/tty")
    {
        file = pProcess->getCtty();
        if(!file)
        {
            F_NOTICE("  -> returning -1, no controlling tty");
            return -1;
        }
        else if(ConsoleManager::instance().isMasterConsole(file))
        {
            // If we happened to somehow open a master console, get its slave.
            F_NOTICE("  -> controlling terminal was not a slave");
            file = ConsoleManager::instance().getOther(file);
        }
    }

    F_NOTICE("  -> actual filename to open is '" << nameToOpen << "'");

    if (!file)
    {
        // Find file.
        file = VFS::instance().find(nameToOpen, GET_CWD());
    }

    bool bCreated = false;
    if (!file)
    {
        if ((flags & O_CREAT) && !onDevFs)
        {
            F_NOTICE("  {O_CREAT}");
            bool worked = VFS::instance().createFile(nameToOpen, mode, GET_CWD());
            if (!worked)
            {
                // createFile should set the error if it fails.
                F_NOTICE("  -> File does not exist (createFile failed)");
                pSubsystem->freeFd(fd);
                return -1;
            }

            file = VFS::instance().find(nameToOpen, GET_CWD());
            if (!file)
            {
                F_NOTICE("  -> File does not exist (O_CREAT failed)");
                SYSCALL_ERROR(DoesNotExist);
                pSubsystem->freeFd(fd);
                return -1;
            }

            bCreated = true;
        }
        else
        {
            F_NOTICE("  -> Does not exist.");
            // Error - not found.
            SYSCALL_ERROR(DoesNotExist);
            pSubsystem->freeFd(fd);
            return -1;
        }
    }

    if(!file)
    {
      F_NOTICE("  -> File does not exist.");
      SYSCALL_ERROR(DoesNotExist);
      pSubsystem->freeFd(fd);
      return -1;
    }

    file = traverseSymlink(file);

    if(!file)
    {
        SYSCALL_ERROR(DoesNotExist);
        pSubsystem->freeFd(fd);
        return -1;
    }

    if (file->isDirectory() && (flags & (O_WRONLY | O_RDWR)))
    {
        // Error - is directory.
        F_NOTICE("  -> Is a directory, and O_WRONLY or O_RDWR was specified.");
        SYSCALL_ERROR(IsADirectory);
        pSubsystem->freeFd(fd);
        return -1;
    }

    if ((flags & O_CREAT) && (flags & O_EXCL) && !bCreated)
    {
        // file exists with O_CREAT and O_EXCL
        F_NOTICE("  -> File exists");
        SYSCALL_ERROR(FileExists);
        pSubsystem->freeFd(fd);
        return -1;
    }

    // O_RDONLY is zero.
    bool checkRead = (flags == O_RDONLY) || (flags & O_RDWR);

    // Check for the desired permissions.
    if (!VFS::checkAccess(file,
        checkRead, flags & (O_WRONLY | O_RDWR | O_TRUNC), false))
    {
        // checkAccess does a SYSCALL_ERROR for us.
        F_NOTICE("  -> file access denied.");
        return -1;
    }

    // Check for console (as we have special handling needed here)
    if (ConsoleManager::instance().isConsole(file))
    {
        // If a master console, attempt to lock.
        if(ConsoleManager::instance().isMasterConsole(file))
        {
            // Lock the master, we now own it.
            // Or, we don't - if someone else has it open for example.
            if(!ConsoleManager::instance().lockConsole(file))
            {
                F_NOTICE("Couldn't lock pseudoterminal master");
                SYSCALL_ERROR(DeviceBusy);
                pSubsystem->freeFd(fd);
                return -1;
            }
        }
    }

    // Permissions were OK.
    if ((flags & O_TRUNC) && ((flags & O_CREAT) || (flags & O_WRONLY) || (flags & O_RDWR)))
    {
        F_NOTICE("  -> {O_TRUNC}");
        // truncate the file
        file->truncate();
    }

    FileDescriptor *f = new FileDescriptor(file, (flags & O_APPEND) ? file->getSize() : 0, fd, 0, flags);
    if(f)
        pSubsystem->addFileDescriptor(fd, f);

    F_NOTICE("    -> " << fd);

    return static_cast<int> (fd);
}

int posix_read(int fd, char *ptr, int len)
{
    F_NOTICE("read(" << Dec << fd << Hex << ", " << reinterpret_cast<uintptr_t>(ptr) << ", " << len << ")");
    if(!PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(ptr), len, PosixSubsystem::SafeWrite))
    {
        F_NOTICE("  -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    // Lookup this process.
    Thread *pThread = Processor::information().getCurrentThread();
    Process *pProcess = pThread->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    FileDescriptor *pFd = pSubsystem->getFileDescriptor(fd);
    if (!pFd)
    {
        // Error - no such file descriptor.
        SYSCALL_ERROR(BadFileDescriptor);
        return -1;
    }
    
    if(pFd->file->isDirectory())
    {
        SYSCALL_ERROR(IsADirectory);
        return -1;
    }

    // Are we allowed to block?
    bool canBlock = !((pFd->flflags & O_NONBLOCK) == O_NONBLOCK);

    // Handle async descriptor that is not ready for reading.
    // File::read has no mechanism for presenting such an error, other than
    // returning 0. However, a read() returning 0 is an EOF condition.
    if(!canBlock)
    {
        if(!pFd->file->select(false, 0))
        {
            SYSCALL_ERROR(NoMoreProcesses);
            return -1;
        }
    }

    // Prepare to handle EINTR.
    uint64_t nRead = 0;
    if (ptr && len)
    {
        pThread->setInterrupted(false);
        nRead = pFd->file->read(pFd->offset, len, reinterpret_cast<uintptr_t>(ptr), canBlock);
        if((!nRead) && (pThread->wasInterrupted()))
        {
            SYSCALL_ERROR(Interrupted);
            return -1;
        }
        pFd->offset += nRead;
    }

    F_NOTICE("    -> " << Dec << nRead << Hex);

    return static_cast<int>(nRead);
}

int posix_write(int fd, char *ptr, int len, bool nocheck)
{
    F_NOTICE("write(" << fd << ", " << reinterpret_cast<uintptr_t>(ptr) << ", " << len << ")");
    if(!nocheck && !PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(ptr), len, PosixSubsystem::SafeRead))
    {
        F_NOTICE("  -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    if(ptr)
    {
        F_NOTICE("write(" << fd << ", " << String(ptr, len) << ", " << len << ")");
    }

    // Lookup this process.
    Thread *pThread = Processor::information().getCurrentThread();
    Process *pProcess = pThread->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    FileDescriptor *pFd = pSubsystem->getFileDescriptor(fd);
    if (!pFd)
    {
        // Error - no such file descriptor.
        SYSCALL_ERROR(BadFileDescriptor);
        return -1;
    }

    // Copy to kernel.
    uint64_t nWritten = 0;
    if (ptr && len)
    {
        nWritten = pFd->file->write(pFd->offset, len, reinterpret_cast<uintptr_t>(ptr));
        pFd->offset += nWritten;
    }

    F_NOTICE("  -> write returns " << nWritten);

    // Handle broken pipe (write of zero bytes to a pipe).
    // Note: don't send SIGPIPE if we actually tried a zero-length write.
    if (pFd->file->isPipe() && (nWritten == 0 && len > 0))
    {
        F_NOTICE("  -> write to a broken pipe");
        SYSCALL_ERROR(BrokenPipe);
        pSubsystem->threadException(pThread, Subsystem::Pipe);
        return -1;
    }

    return static_cast<int>(nWritten);
}

off_t posix_lseek(int file, off_t ptr, int dir)
{
    F_NOTICE("lseek(" << file << ", " << ptr << ", " << dir << ")");

    // Lookup this process.
    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    FileDescriptor *pFd = pSubsystem->getFileDescriptor(file);
    if (!pFd)
    {
        // Error - no such file descriptor.
        SYSCALL_ERROR(BadFileDescriptor);
        return -1;
    }

    size_t fileSize = pFd->file->getSize();
    switch (dir)
    {
    case SEEK_SET:
        pFd->offset = ptr;
        break;
    case SEEK_CUR:
        pFd->offset += ptr;
        break;
    case SEEK_END:
        pFd->offset = fileSize + ptr;
        break;
    }

    return static_cast<int>(pFd->offset);
}

int posix_link(char *target, char *link)
{
    if(!(PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(target), PATH_MAX, PosixSubsystem::SafeRead) &&
        PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(link), PATH_MAX, PosixSubsystem::SafeRead)))
    {
        F_NOTICE("link -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("link(" << target << ", " << link << ")");

    // Try and find the target.
    String realTarget;
    String realLink;
    normalisePath(realTarget, target);
    normalisePath(realLink, link);

    File *pTarget = VFS::instance().find(realTarget, GET_CWD());
    pTarget = traverseSymlink(pTarget);
    if (!pTarget)
    {
        F_NOTICE(" -> target '" << realTarget << "' did not exist.");
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }

    bool result = VFS::instance().createLink(realLink, pTarget, GET_CWD());

    if (!result)
    {
        F_NOTICE(" -> failed to create link");
        return -1;
    }

    F_NOTICE(" -> ok");
    return 0;
}

int posix_readlink(const char* path, char* buf, unsigned int bufsize)
{
    if(!(PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(path), PATH_MAX, PosixSubsystem::SafeRead) &&
        PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(buf), bufsize, PosixSubsystem::SafeWrite)))
    {
        F_NOTICE("readlink -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("readlink(" << path << ", " << reinterpret_cast<uintptr_t>(buf) << ", " << bufsize << ")");

    String realPath;
    normalisePath(realPath, path);

    File* f = VFS::instance().find(realPath, GET_CWD());
    if (!f)
    {
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }

    if (!f->isSymlink())
    {
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    if (buf == 0)
        return -1;

    HugeStaticString str;
    HugeStaticString tmp;
    str.clear();
    tmp.clear();

    return Symlink::fromFile(f)->followLink(buf, bufsize);
}

int posix_realpath(const char *path, char *buf, size_t bufsize)
{
    F_NOTICE("realpath");

    if(!(PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(path), PATH_MAX, PosixSubsystem::SafeRead) &&
        PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(buf), bufsize, PosixSubsystem::SafeWrite)))
    {
        F_NOTICE("realpath -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    String realPath;
    normalisePath(realPath, path);
    F_NOTICE("  -> traversing " << realPath);
    File* f = VFS::instance().find(realPath, GET_CWD());
    if (!f)
    {
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }

    f = traverseSymlink(f);
    if(!f)
    {
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }

    if(!f->isDirectory())
    {
        SYSCALL_ERROR(NotADirectory);
        return -1;
    }

    String actualPath("/@/");
    actualPath += f->getFullPath(true);
    if(actualPath.length() > (bufsize - 1))
    {
        SYSCALL_ERROR(NameTooLong);
        return -1;
    }

    // File is good, copy it now.
    F_NOTICE("  -> returning " << actualPath);
    StringCopyN(buf, static_cast<const char *>(actualPath), bufsize);

    return 0;
}

int posix_unlink(char *name)
{
    if(!PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(name), PATH_MAX, PosixSubsystem::SafeRead))
    {
        F_NOTICE("unlink -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("unlink(" << name << ")");

    String realPath;
    normalisePath(realPath, name);

    File *pFile = VFS::instance().find(realPath, GET_CWD());
    if (!pFile)
    {
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }
    else if (pFile->isDirectory())
    {
        // We don't support unlink() on directories - use rmdir().
        SYSCALL_ERROR(NotEnoughPermissions);
        return -1;
    }

    // remove() checks permissions to ensure we can delete the file.
    if (VFS::instance().remove(realPath, GET_CWD()))
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

int posix_symlink(char *target, char *link)
{
    if(!(PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(target), PATH_MAX, PosixSubsystem::SafeRead) &&
        PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(link), PATH_MAX, PosixSubsystem::SafeRead)))
    {
        F_NOTICE("symlink -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("symlink(" << target << ", " << link << ")");

    bool worked = VFS::instance().createSymlink(String(link), String(target), GET_CWD());
    if (worked)
        return 0;
    else
        ERROR("Symlink failed for `" << link << "' -> `" << target << "'");
    return -1;
}

int posix_rename(const char* source, const char* dst)
{
    if(!(PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(source), PATH_MAX, PosixSubsystem::SafeRead) &&
        PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(dst), PATH_MAX, PosixSubsystem::SafeRead)))
    {
        F_NOTICE("rename -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("rename(" << source << ", " << dst << ")");

    String realSource;
    String realDestination;
    normalisePath(realSource, source);
    normalisePath(realDestination, dst);

    File* src = VFS::instance().find(realSource, GET_CWD());
    File* dest = VFS::instance().find(realDestination, GET_CWD());

    if (!src)
    {
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }

    // traverse symlink
    src = traverseSymlink(src);
    if(!src)
    {
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }

    if (dest)
    {
        // traverse symlink
        dest = traverseSymlink(dest);
        if(!dest)
        {
            SYSCALL_ERROR(DoesNotExist);
            return -1;
        }

        if (dest->isDirectory() && !src->isDirectory())
        {
            SYSCALL_ERROR(FileExists);
            return -1;
        }
        else if (!dest->isDirectory() && src->isDirectory())
        {
            SYSCALL_ERROR(NotADirectory);
            return -1;
        }
    }
    else
    {
        VFS::instance().createFile(realDestination, 0777, GET_CWD());
        dest = VFS::instance().find(realDestination, GET_CWD());
        if (!dest)
        {
            // Failed to create the file?
            return -1;
        }
    }

    // Gay algorithm.
    uint8_t* buf = new uint8_t[src->getSize()];
    src->read(0, src->getSize(), reinterpret_cast<uintptr_t>(buf));
    dest->truncate();
    dest->write(0, src->getSize(), reinterpret_cast<uintptr_t>(buf));
    VFS::instance().remove(realSource, GET_CWD());
    delete [] buf;

    return 0;
}

char* posix_getcwd(char* buf, size_t maxlen)
{
    if(!PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(buf), maxlen, PosixSubsystem::SafeWrite))
    {
        F_NOTICE("getcwd -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return 0;
    }

    F_NOTICE("getcwd(" << maxlen << ")");

    File* curr = GET_CWD();

    // Absolute path syntax.
    String str("/@/");
    str += curr->getFullPath(true);

    size_t maxLength = str.length();
    if(maxLength > maxlen)
    {
        // Too long.
        SYSCALL_ERROR(BadRange);
        return 0;
    }
    StringCopyN(buf, static_cast<const char*>(str), maxLength);

    F_NOTICE(" -> " << str);

    return buf;
}

int posix_stat(const char *name, struct stat *st)
{
    if(!(PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(name), PATH_MAX, PosixSubsystem::SafeRead) &&
        PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(st), sizeof(struct stat), PosixSubsystem::SafeWrite)))
    {
        F_NOTICE("stat -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("stat(" << name << ")");

    // verify the filename - don't try to open a dud file (otherwise we'll open the cwd)
    if (name[0] == 0)
    {
        F_NOTICE("    -> Doesn't exist");
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }

    if(!st)
    {
        F_NOTICE("    -> Invalid argument");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    String realPath;
    normalisePath(realPath, name);

    File* file = VFS::instance().find(realPath, GET_CWD());
    if (!file)
    {
        F_NOTICE("    -> Not found by VFS");
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }

    if (!doStat(name, file, st))
    {
        return -1;
    }

    F_NOTICE("    -> Success");
    return 0;
}

int posix_fstat(int fd, struct stat *st)
{
    if(!PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(st), sizeof(struct stat), PosixSubsystem::SafeWrite))
    {
        F_NOTICE("fstat -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("fstat(" << Dec << fd << Hex << ")");
    if(!st)
    {
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    // Lookup this process.
    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    FileDescriptor *pFd = pSubsystem->getFileDescriptor(fd);
    if (!pFd)
    {
        ERROR("Error, no such FD!");
        // Error - no such file descriptor.
        return -1;
    }

    if (!doStat(0, pFd->file, st))
    {
        return -1;
    }

    F_NOTICE("    -> Success");
    return 0;
}

int posix_lstat(char *name, struct stat *st)
{
    if(!(PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(name), PATH_MAX, PosixSubsystem::SafeRead) &&
        PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(st), sizeof(struct stat), PosixSubsystem::SafeWrite)))
    {
        F_NOTICE("lstat -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("lstat(" << name << ")");
    if(!st)
    {
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    String realPath;
    normalisePath(realPath, name);

    File *file = VFS::instance().find(realPath, GET_CWD());
    if (!file)
    {
        F_NOTICE("    -> Not found by VFS");
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }

    if (!doStat(name, file, st, false))
    {
        return -1;
    }

    F_NOTICE("    -> Success");
    return 0;
}

int posix_opendir(const char *dir, DIR *ent)
{
    if(!(PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(dir), PATH_MAX, PosixSubsystem::SafeRead) &&
        PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(ent), sizeof(DIR), PosixSubsystem::SafeWrite)))
    {
        F_NOTICE("opendir -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("opendir(" << dir << ")");

    // Lookup this process.
    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    String realPath;
    normalisePath(realPath, dir);

    File* file = VFS::instance().find(realPath, GET_CWD());
    if (!file)
    {
        // Error - not found.
        F_NOTICE(" -> not found");
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }
    
    file = traverseSymlink(file);

    if(!file)
        return -1;

    if (!file->isDirectory())
    {
        // Error - not a directory.
        F_NOTICE(" -> not a directory");
        SYSCALL_ERROR(NotADirectory);
        return -1;
    }

    // Need read permission to list the directory.
    if (!VFS::checkAccess(file, true, false, false))
    {
        // checkAccess does a SYSCALL_ERROR for us.
        return -1;
    }

    size_t fd = pSubsystem->getFd();
    FileDescriptor *f = new FileDescriptor;
    f->file = file;
    f->offset = 0;
    f->fd = fd;

    // Fill out the DIR structure too.
    Directory *pDirectory = Directory::fromFile(file);
    ByteSet(ent, 0, sizeof(*ent));
    ent->fd = fd;
    ent->count = pDirectory->getNumChildren();

    // Register the fd, we're about to buffer the directory and be done here
    pSubsystem->addFileDescriptor(fd, f);

    // Load the buffer now that we've set up for the buffer.
    if (posix_readdir(ent) < 0)
    {
        // readdir does a SYSCALL_ERROR when it fails
        F_NOTICE(" -> readdir failed...");
        pSubsystem->freeFd(fd);
        ByteSet(ent, 0xFF, sizeof(*ent));
        return -1;
    }

    F_NOTICE(" -> " << fd);
    return static_cast<int>(fd);
}

int posix_readdir(DIR *dir)
{
    if(!PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(dir), sizeof(DIR), PosixSubsystem::SafeWrite))
    {
        F_NOTICE("readdir -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("readdir(" << dir->fd << ")");

    // Lookup this process.
    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    FileDescriptor *pFd = pSubsystem->getFileDescriptor(dir->fd);
    if (!pFd || !pFd->file)
    {
        // Error - no such file descriptor.
        F_NOTICE(" -> bad file");
        SYSCALL_ERROR(BadFileDescriptor);
        return -1;
    }

    if(!pFd->file->isDirectory())
    {
        F_NOTICE(" -> not a directory");
        SYSCALL_ERROR(NotADirectory);
        return -1;
    }

    if (dir->totalpos % 64)
    {
        // Not on a multiple of 64 - possibly called directly rather than via
        // libc (where the proper magic is done).
        F_NOTICE(" -> wrong position");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    // Buffer another 64 entries.
    Directory *pDirectory = Directory::fromFile(pFd->file);
    for (size_t i = 0; i < 64; ++i)
    {
        File *pFile = pDirectory->getChild(dir->totalpos + i);
        if (!pFile)
            break;

        dir->ent[i].d_ino = pFile->getInode();

        // Some applications consider a null inode to mean "bad file" which is
        // a horrible assumption for them to make. Because the presence of a file
        // is indicated by more effective means (ie, successful return from
        // readdir) this just appeases the applications which aren't portably
        // written.
        if(dir->ent[i].d_ino == 0)
            dir->ent[i].d_ino = 0x7fff; // Signed, don't want this to turn negative

        // Copy filename.
        StringCopyN(dir->ent[i].d_name, static_cast<const char *>(pFile->getName()), MAXNAMLEN);
        if(pFile->isSymlink())
            dir->ent[i].d_type = DT_LNK;
        else
            dir->ent[i].d_type = pFile->isDirectory() ? DT_DIR : DT_REG;
    }

    return 0;
}

int posix_closedir(DIR *dir)
{
    if(!PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(dir), sizeof(DIR), PosixSubsystem::SafeRead))
    {
        F_NOTICE("closedir -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    if (dir->fd < 0)
    {
        SYSCALL_ERROR(BadFileDescriptor);
        return -1;
    }

    F_NOTICE("closedir(" << dir->fd << ")");

    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    pSubsystem->freeFd(dir->fd);

    return 0;
}

int posix_ioctl(int fd, int command, void *buf)
{
    F_NOTICE("ioctl(" << Dec << fd << ", " << Hex << command << ", " << reinterpret_cast<uintptr_t>(buf) << ")");

    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    FileDescriptor *f = pSubsystem->getFileDescriptor(fd);
    if (!f)
    {
        // Error - no such FD.
        return -1;
    }

    /// \todo Sanitise buf, if it has meaning for the command.

    if (f->file->supports(command))
    {
        return f->file->command(command, buf);
    }

    switch (command)
    {
        case TIOCGWINSZ:
        {
            return console_getwinsize(f->file, reinterpret_cast<winsize_t*>(buf));
        }

        case TIOCSWINSZ:
        {
            const winsize_t *ws = reinterpret_cast<const winsize_t*>(buf);
            F_NOTICE(" -> TIOCSWINSZ " << Dec << ws->ws_col << "x" << ws->ws_row << Hex);
            return console_setwinsize(f->file, ws);
        }

        case TIOCFLUSH:
        {
            return console_flush(f->file, buf);
        }

        case TIOCSCTTY:
        {
            F_NOTICE(" -> TIOCSCTTY");
            return console_setctty(fd, reinterpret_cast<uintptr_t>(buf) == 1);
        }

        case FIONBIO:
        {
            // set/unset non-blocking
            if (buf)
            {
                int a = *reinterpret_cast<int *>(buf);
                if (a)
                {
                    F_NOTICE("  -> set non-blocking");
                    f->flflags |= O_NONBLOCK;
                }
                else
                {
                    F_NOTICE("  -> set blocking");
                    f->flflags &= ~O_NONBLOCK;
                }
            }
            else
                f->flflags &= ~O_NONBLOCK;

            return 0;
        }
        default:
        {
            // Error - no such ioctl.
            SYSCALL_ERROR(InvalidArgument);
            return -1;
        }
    }
}

int posix_chdir(const char *path)
{
    if(!PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(path), PATH_MAX, PosixSubsystem::SafeRead))
    {
        F_NOTICE("chdir -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("chdir(" << path << ")");

    String realPath;
    normalisePath(realPath, path);

    File *dir = VFS::instance().find(realPath, GET_CWD());
    if (!dir)
    {
        F_NOTICE("Does not exist.");
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }

    return doChdir(dir) ? 0 : -1;
}

int posix_dup(int fd)
{
    F_NOTICE("dup(" << fd << ")");

    // grab the file descriptor pointer for the passed descriptor
    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    FileDescriptor *f = pSubsystem->getFileDescriptor(fd);
    if (!f)
    {
        SYSCALL_ERROR(BadFileDescriptor);
        return -1;
    }

    size_t newFd = pSubsystem->getFd();

    // Copy the descriptor
    FileDescriptor* f2 = new FileDescriptor(*f);
    pSubsystem->addFileDescriptor(newFd, f2);

    return static_cast<int>(newFd);
}

int posix_dup2(int fd1, int fd2)
{
    F_NOTICE("dup2(" << fd1 << ", " << fd2 << ")");

    if (fd2 < 0)
    {
        SYSCALL_ERROR(BadFileDescriptor);
        return -1; // EBADF
    }

    if (fd1 == fd2)
        return fd2;

    // grab the file descriptor pointer for the passed descriptor
    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    FileDescriptor* f = pSubsystem->getFileDescriptor(fd1);
    if (!f)
    {
        SYSCALL_ERROR(BadFileDescriptor);
        return -1;
    }

    // Copy the descriptor.
    //
    // This will also increase the refcount *before* we close the original, else we
    // might accidentally trigger an EOF condition on a pipe! (if the write refcount
    // drops to zero)...
    FileDescriptor* f2 = new FileDescriptor(*f);
    pSubsystem->addFileDescriptor(fd2, f2);

    // According to the spec, CLOEXEC is cleared on DUP.
    f2->fdflags &= ~FD_CLOEXEC;

    return fd2;
}

int posix_mkdir(const char* name, int mode)
{
    if(!PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(name), PATH_MAX, PosixSubsystem::SafeRead))
    {
        F_NOTICE("mkdir -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("mkdir(" << name << ")");

    String realPath;
    normalisePath(realPath, name);

    PosixProcess *pPosixProcess = getPosixProcess();
    if (pPosixProcess)
    {
        mode &= ~pPosixProcess->getMask();
    }

    bool worked = VFS::instance().createDirectory(realPath, mode, GET_CWD());
    return worked ? 0 : -1;
}

int posix_rmdir(const char *path)
{
    if(!PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(path), PATH_MAX, PosixSubsystem::SafeRead))
    {
        F_NOTICE("rmdir -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("rmdir(" << path << ")");

    String realPath;
    normalisePath(realPath, path);

    // remove() holds the main logic for this.
    bool worked = VFS::instance().remove(realPath, GET_CWD());
    return worked ? 0 : -1;
}

int posix_isatty(int fd)
{
    // Lookup this process.
    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    FileDescriptor *pFd = pSubsystem->getFileDescriptor(fd);
    if (!pFd)
    {
        // Error - no such file descriptor.
        ERROR("isatty: no such file descriptor (" << Dec << fd << Hex << ")");
        return 0;
    }

    int result = ConsoleManager::instance().isConsole(pFd->file) ? 1 : 0;
    NOTICE("isatty(" << fd << ") -> " << result);
    return result;
}

int posix_fcntl(int fd, int cmd, void* arg)
{
    /// \todo Same as ioctl, figure out how best to sanitise input addresses
    F_NOTICE("fcntl(" << fd << ", " << cmd << ", " << arg << ")");

    // grab the file descriptor pointer for the passed descriptor
    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    FileDescriptor* f = pSubsystem->getFileDescriptor(fd);
    if(!f)
    {
        SYSCALL_ERROR(BadFileDescriptor);
        return -1;
    }

    switch (cmd)
    {
        case F_DUPFD:

            if (arg)
            {
                size_t fd2 = reinterpret_cast<size_t>(arg);

                // Copy the descriptor (addFileDescriptor automatically frees the old one, if needed)
                FileDescriptor* f2 = new FileDescriptor(*f);
                pSubsystem->addFileDescriptor(fd2, f2);

                // According to the spec, CLOEXEC is cleared on DUP.
                f2->fdflags &= ~FD_CLOEXEC;

                return static_cast<int>(fd2);
            }
            else
            {
                size_t fd2 = pSubsystem->getFd();

                // copy the descriptor
                FileDescriptor* f2 = new FileDescriptor(*f);
                pSubsystem->addFileDescriptor(fd2, f2);

                // According to the spec, CLOEXEC is cleared on DUP.
                f2->fdflags &= ~FD_CLOEXEC;

                return static_cast<int>(fd2);
            }
            break;

        case F_GETFD:
            return f->fdflags;
        case F_SETFD:
            f->fdflags = reinterpret_cast<size_t>(arg);
            return 0;
        case F_GETFL:
            F_NOTICE("  -> get flags " << f->flflags);
            return f->flflags;
        case F_SETFL:
            F_NOTICE("  -> set flags " << arg);
            f->flflags = reinterpret_cast<size_t>(arg) & (O_APPEND | O_NONBLOCK);
            F_NOTICE("  -> new flags " << f->flflags);
            return 0;
        case F_GETLK: // Get record-locking information
        case F_SETLK: // Set or clear a record lock (without blocking
        case F_SETLKW: // Set or clear a record lock (with blocking)

            /// \todo this doesn't work for multiple locks with different start
            /// and len values, which means it is insufficient for sqlite3.

            /// \note advisory locking disabled for now
            return 0;

            // Grab the lock information structure
            struct flock *lock = reinterpret_cast<struct flock*>(arg);
            if(!lock)
            {
                SYSCALL_ERROR(InvalidArgument);
                return -1;
            }

            // Lock the LockedFile map
            // LockGuard<Mutex> lockFileGuard(g_PosixLockedFileMutex);

            // Can only take exclusive locks...
            if(cmd == F_GETLK)
            {
                if(f->lockedFile)
                {
                    lock->l_type = F_WRLCK;
                    lock->l_whence = SEEK_SET;
                    lock->l_start = lock->l_len = 0;
                    lock->l_pid = f->lockedFile->getLocker();
                }
                else
                    lock->l_type = F_UNLCK;

                return 0;
            }

            // Trying to set an exclusive lock?
            if(lock->l_type == F_WRLCK)
            {
                // Already got a LockedFile instance?
                if(f->lockedFile)
                {
                    if(cmd == F_SETLK)
                    {
                        return f->lockedFile->lock(false) ? 0 : -1;
                    }
                    else
                    {
                        // Lock the file, blocking
                        f->lockedFile->lock(true);
                        return 0;
                    }
                }

                // Not already locked!
                LockedFile *lf = new LockedFile(f->file);
                if(!lf)
                {
                    SYSCALL_ERROR(OutOfMemory);
                    return -1;
                }

                // Insert
                g_PosixGlobalLockedFiles.insert(f->file->getFullPath(), lf);
                f->lockedFile = lf;

                // The file is now locked
                return 0;
            }

            // Trying to unlock?
            if(lock->l_type == F_UNLCK)
            {
                // No locked file? The unlock still succeeds.
                if(f->lockedFile)
                {
                    f->lockedFile->unlock();
                }

                return 0;
            }

            // Success, none of the above, no reason to be unlockable
            return 0;
    }

    SYSCALL_ERROR(Unimplemented);
    return -1;
}

struct _mmap_tmp
{
    void *addr;
    size_t len;
    int prot;
    int flags;
    int fildes;
    off_t off;
};

void *posix_mmap(void *p)
{
    F_NOTICE("mmap");

    if(!PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(p), sizeof(_mmap_tmp), PosixSubsystem::SafeRead))
    {
        F_NOTICE("mmap -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return MAP_FAILED;
    }

    // Grab the parameter list
    _mmap_tmp *map_info = reinterpret_cast<_mmap_tmp*>(p);

    // Get real variables from the parameters
    void *addr = map_info->addr;
    size_t len = map_info->len;
    int prot = map_info->prot;
    int flags = map_info->flags;
    int fd = map_info->fildes;
    off_t off = map_info->off;

    F_NOTICE("  -> addr=" << reinterpret_cast<uintptr_t>(addr) << ", len=" << len << ", prot=" << prot << ", flags=" << flags << ", fildes=" << fd << ", off=" << off << ".");

    // Get the File object to map
    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return MAP_FAILED;
    }

    // The return address
    void *finalAddress = 0;

    VirtualAddressSpace &va = Processor::information().getVirtualAddressSpace();
    size_t pageSz = PhysicalMemoryManager::getPageSize();

    // Sanitise input.
    uintptr_t sanityAddress = reinterpret_cast<uintptr_t>(addr);
    if(sanityAddress)
    {
        if((sanityAddress < va.getUserStart()) ||
            (sanityAddress >= va.getKernelStart()))
        {
            if(flags & MAP_FIXED)
            {
                // Invalid input and MAP_FIXED, this is an error.
                SYSCALL_ERROR(InvalidArgument);
                F_NOTICE("  -> mmap given invalid fixed address");
                return MAP_FAILED;
            }
            else
            {
                // Invalid input - but not MAP_FIXED, so we can ignore addr.
                sanityAddress = 0;
            }
        }
    }

    // Verify the passed length
    if(!len || (sanityAddress & (pageSz-1)))
    {
        SYSCALL_ERROR(InvalidArgument);
        return MAP_FAILED;
    }

    // Create permission set.
    MemoryMappedObject::Permissions perms;
    if(prot & PROT_NONE)
    {
        perms = MemoryMappedObject::None;
    }
    else
    {
        // Everything implies a readable memory region.
        perms = MemoryMappedObject::Read;
        if(prot & PROT_WRITE)
            perms |= MemoryMappedObject::Write;
        if(prot & PROT_EXEC)
            perms |= MemoryMappedObject::Exec;
    }

    if(flags & MAP_ANON)
    {
        if(flags & MAP_SHARED)
        {
            F_NOTICE("  -> failed (MAP_SHARED cannot be used with MAP_ANONYMOUS)");
            SYSCALL_ERROR(InvalidArgument);
            return MAP_FAILED;
        }

        MemoryMappedObject *pObject = MemoryMapManager::instance().mapAnon(sanityAddress, len, perms);
        if(!pObject)
        {
            /// \todo Better error?
            SYSCALL_ERROR(OutOfMemory);
            F_NOTICE("  -> failed (mapAnon)!");
            return MAP_FAILED;
        }

        F_NOTICE("  -> " << sanityAddress);

        finalAddress = reinterpret_cast<void*>(sanityAddress);
    }
    else
    {
        // Valid file passed?
        FileDescriptor* f = pSubsystem->getFileDescriptor(fd);
        if(!f)
        {
            SYSCALL_ERROR(BadFileDescriptor);
            return MAP_FAILED;
        }

        /// \todo check flags on the file descriptor (e.g. O_RDONLY shouldn't be opened writeable)

        // Grab the file to map in
        File *fileToMap = f->file;

        // Check general file permissions, open file mode aside.
        // Note: PROT_WRITE is OK for private mappings, as the backing file
        // doesn't get updated for those maps.
        if (!VFS::checkAccess(fileToMap, prot & PROT_READ, (prot & PROT_WRITE) && (flags & MAP_SHARED), prot & PROT_EXEC))
        {
            F_NOTICE("  -> mmap on " << fileToMap->getFullPath() << " failed due to permissions.");
            return MAP_FAILED;
        }
        
        F_NOTICE("mmap: file name is " << fileToMap->getFullPath());

        // Grab the MemoryMappedFile for it. This will automagically handle
        // MAP_FIXED mappings too
        bool bCopyOnWrite = (flags & MAP_SHARED) == 0;
        MemoryMappedObject *pFile = MemoryMapManager::instance().mapFile(fileToMap, sanityAddress, len, perms, off, bCopyOnWrite);
        if(!pFile)
        {
            /// \todo Better error?
            SYSCALL_ERROR(OutOfMemory);
            F_NOTICE("  -> failed (mapFile)!");
            return MAP_FAILED;
        }

        F_NOTICE("  -> " << sanityAddress);

        finalAddress = reinterpret_cast<void*>(sanityAddress);
    }

    // Complete
    return finalAddress;
}

int posix_msync(void *p, size_t len, int flags) {
    F_NOTICE("msync");

    uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    size_t pageSz = PhysicalMemoryManager::getPageSize();

    // Verify the passed length
    if(!len || (addr & (pageSz-1)))
    {
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    if((flags & ~(MS_ASYNC | MS_INVALIDATE | MS_SYNC)) != 0)
    {
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    // Make sure there's at least one object we'll touch.
    if(!MemoryMapManager::instance().contains(addr, len))
    {
        SYSCALL_ERROR(OutOfMemory);
        return -1;
    }

    if(flags & MS_INVALIDATE)
    {
        MemoryMapManager::instance().invalidate(addr, len);
    }
    else
    {
        MemoryMapManager::instance().sync(addr, len, flags & MS_ASYNC);
    }

    return 0;
}

int posix_mprotect(void *p, size_t len, int prot)
{
    F_NOTICE("mprotect");

    uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    size_t pageSz = PhysicalMemoryManager::getPageSize();

    // Verify the passed length
    if(!len || (addr & (pageSz-1)))
    {
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    // Make sure there's at least one object we'll touch.
    if(!MemoryMapManager::instance().contains(addr, len))
    {
        SYSCALL_ERROR(OutOfMemory);
        return -1;
    }

    // Create permission set.
    MemoryMappedObject::Permissions perms;
    if(prot & PROT_NONE)
    {
        perms = MemoryMappedObject::None;
    }
    else
    {
        // Everything implies a readable memory region.
        perms = MemoryMappedObject::Read;
        if(prot & PROT_WRITE)
            perms |= MemoryMappedObject::Write;
        if(prot & PROT_EXEC)
            perms |= MemoryMappedObject::Exec;
    }

    /// \todo EACCESS, which needs us to be able to get the File for a given
    ///       mapping (if one exists).

    MemoryMapManager::instance().setPermissions(addr, len, perms);

    return 0;
}

int posix_munmap(void *addr, size_t len)
{
    F_NOTICE("munmap(" << reinterpret_cast<uintptr_t>(addr) << ", " << len << ")");

    if(!len)
    {
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    MemoryMapManager::instance().remove(reinterpret_cast<uintptr_t>(addr), len);

    return 0;
}

int posix_access(const char *name, int amode)
{
    if(!PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(name), PATH_MAX, PosixSubsystem::SafeRead))
    {
        F_NOTICE("access -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("access(" << (name ? name : "n/a") << ", " << Dec << amode << Hex << ")");

    if(!name)
    {
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }

    String realPath;
    normalisePath(realPath, name);

    // Grab the file
    File *file = VFS::instance().find(realPath, GET_CWD());
    file = traverseSymlink(file);
    if (!file)
    {
        F_NOTICE("  -> '" << realPath << "' does not exist");
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }

    // If we're only checking for existence, we're done here.
    if (amode == F_OK)
    {
        F_NOTICE("  -> ok");
        return 0;
    }

    if (!VFS::checkAccess(file, amode & R_OK, amode & W_OK, amode & X_OK))
    {
        // checkAccess does a SYSCALL_ERROR for us.
        F_NOTICE("  -> not ok");
        return -1;
    }

    F_NOTICE("  -> ok");
    return 0;
}

int posix_ftruncate(int a, off_t b)
{
	F_NOTICE("ftruncate(" << a << ", " << b << ")");

    // Grab the File pointer for this file
    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    FileDescriptor *pFd = pSubsystem->getFileDescriptor(a);
    if (!pFd)
    {
        // Error - no such file descriptor.
        SYSCALL_ERROR(BadFileDescriptor);
        return -1;
    }
    File *pFile = pFd->file;

    // If we are to simply truncate, do so
    if(b == 0)
    {
        pFile->truncate();
        return 0;
    }
    else if(static_cast<size_t>(b) == pFile->getSize())
        return 0;
    // If we need to reduce the file size, do so
    else if(static_cast<size_t>(b) < pFile->getSize())
    {
        pFile->setSize(b);
        return 0;
    }
    // Otherwise, extend the file
    else
    {
        size_t currSize = pFile->getSize();
        size_t numExtraBytes = b - currSize;
        NOTICE("Extending by " << numExtraBytes << " bytes");
        uint8_t *nullBuffer = new uint8_t[numExtraBytes];
        NOTICE("Got the buffer");
        ByteSet(nullBuffer, 0, numExtraBytes);
        NOTICE("Zeroed the buffer");
        pFile->write(currSize, numExtraBytes, reinterpret_cast<uintptr_t>(nullBuffer));
        NOTICE("Deleting the buffer");
        delete [] nullBuffer;
        NOTICE("Complete");
        return 0;
    }
}

int posix_fsync(int fd)
{
    F_NOTICE("fsync(" << fd << ")");

    // Grab the File pointer for this file
    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    FileDescriptor *pFd = pSubsystem->getFileDescriptor(fd);
    if (!pFd)
    {
        // Error - no such file descriptor.
        SYSCALL_ERROR(BadFileDescriptor);
        return -1;
    }
    File *pFile = pFd->file;
    pFile->sync();

    return 0;
}

int pedigree_get_mount(char* mount_buf, char* info_buf, size_t n)
{
    if(!(PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(mount_buf), PATH_MAX, PosixSubsystem::SafeWrite) &&
        PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(info_buf), PATH_MAX, PosixSubsystem::SafeWrite)))
    {
        F_NOTICE("pedigree_get_mount -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    NOTICE("pedigree_get_mount(" << Dec << n << Hex << ")");
    
    typedef List<String*> StringList;
    typedef Tree<Filesystem *, List<String*>* > VFSMountTree;
    VFSMountTree &mounts = VFS::instance().getMounts();
    
    size_t i = 0;
    for(VFSMountTree::Iterator it = mounts.begin();
        it != mounts.end();
        it++)
    {
        Filesystem *pFs = it.key();
        StringList *pList = it.value();
        Disk *pDisk = pFs->getDisk();
        
        for(StringList::Iterator it2 = pList->begin();
            it2 != pList->end();
            it2++, i++)
        {
            String mount = **it2;
            
            if(i == n)
            {
                String info, s;
                if(pDisk)
                {
                    pDisk->getName(s);
                    pDisk->getParent()->getName(info);
                    info += " // ";
                    info += s;
                }
                else
                    info = "no disk";
                
                StringCopy(mount_buf, static_cast<const char *>(mount));
                StringCopy(info_buf, static_cast<const char *>(info));
                
                return 0;
            }
        }
    }
    
    return -1;
}

int posix_chmod(const char *path, mode_t mode)
{
    if(!PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(path), PATH_MAX, PosixSubsystem::SafeRead))
    {
        F_NOTICE("chmod -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("chmod(" << String(path) << ", " << Oct << mode << Hex << ")");
    
    if((mode == static_cast<mode_t>(-1)) || (mode > 0777))
    {
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    bool onDevFs = false;
    String realPath;
    normalisePath(realPath, path, &onDevFs);

    if(onDevFs)
    {
        // Silently ignore.
        return 0;
    }

    File* file = VFS::instance().find(realPath, GET_CWD());
    if (!file)
    {
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }
    
    // Read-only filesystem?
    if(file->getFilesystem()->isReadOnly())
    {
        SYSCALL_ERROR(ReadOnlyFilesystem);
        return -1;
    }

    // Symlink traversal
    file = traverseSymlink(file);
    if(!file)
        return -1;

    return doChmod(file, mode) ? 0 : -1;
}

int posix_chown(const char *path, uid_t owner, gid_t group)
{
    if(!PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(path), PATH_MAX, PosixSubsystem::SafeRead))
    {
        F_NOTICE("chown -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("chown(" << String(path) << ", " << owner << ", " << group << ")");

    // Is there any need to change?
    if((owner == group) && (owner == static_cast<uid_t>(-1)))
        return 0;

    bool onDevFs = false;
    String realPath;
    normalisePath(realPath, path, &onDevFs);

    if(onDevFs)
    {
        // Silently ignore.
        return 0;
    }

    File* file = VFS::instance().find(realPath, GET_CWD());
    if (!file)
    {
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }
    
    // Read-only filesystem?
    if(file->getFilesystem()->isReadOnly())
    {
        SYSCALL_ERROR(ReadOnlyFilesystem);
        return -1;
    }

    // Symlink traversal
    file = traverseSymlink(file);
    if(!file)
        return -1;

    return doChown(file, owner, group) ? 0 : -1;
}

int posix_fchmod(int fd, mode_t mode)
{
    F_NOTICE("fchmod(" << fd << ", " << Oct << mode << Hex << ")");

    if((mode == static_cast<mode_t>(-1)) || (mode > 0777))
    {
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }
    
    // Lookup this process.
    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    FileDescriptor *pFd = pSubsystem->getFileDescriptor(fd);
    if (!pFd)
    {
        // Error - no such file descriptor.
        SYSCALL_ERROR(BadFileDescriptor);
        return -1;
    }
    
    File *file = pFd->file;
    
    // Read-only filesystem?
    if(file->getFilesystem()->isReadOnly())
    {
        SYSCALL_ERROR(ReadOnlyFilesystem);
        return -1;
    }

    return doChmod(file, mode) ? 0 : -1;
    
    return 0;
}

int posix_fchown(int fd, uid_t owner, gid_t group)
{
    F_NOTICE("fchown(" << fd << ", " << owner << ", " << group << ")");

    // Is there any need to change?
    if((owner == group) && (owner == static_cast<uid_t>(-1)))
        return 0;
    
    // Lookup this process.
    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    FileDescriptor *pFd = pSubsystem->getFileDescriptor(fd);
    if (!pFd)
    {
        // Error - no such file descriptor.
        SYSCALL_ERROR(BadFileDescriptor);
        return -1;
    }
    
    File *file = pFd->file;
    
    // Read-only filesystem?
    if(file->getFilesystem()->isReadOnly())
    {
        SYSCALL_ERROR(ReadOnlyFilesystem);
        return -1;
    }

    return doChown(file, owner, group) ? 0 : -1;
}

int posix_fchdir(int fd)
{
    F_NOTICE("fchdir(" << fd << ")");
    
    // Lookup this process.
    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    FileDescriptor *pFd = pSubsystem->getFileDescriptor(fd);
    if (!pFd)
    {
        // Error - no such file descriptor.
        SYSCALL_ERROR(BadFileDescriptor);
        return -1;
    }
    
    File *file = pFd->file;
    return doChdir(file) ? 0 : -1;
}

static int statvfs_doer(Filesystem *pFs, struct statvfs *buf)
{
    if(!pFs)
    {
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }
    
    /// \todo Get all this data from the Filesystem object
    buf->f_bsize = 4096;
    buf->f_frsize = 512;
    buf->f_blocks = static_cast<fsblkcnt_t>(-1);
    buf->f_bfree = static_cast<fsblkcnt_t>(-1);
    buf->f_bavail = static_cast<fsblkcnt_t>(-1);
    buf->f_files = 0;
    buf->f_ffree = static_cast<fsfilcnt_t>(-1);
    buf->f_favail = static_cast<fsfilcnt_t>(-1);
    buf->f_fsid = 0;
    buf->f_flag = (pFs->isReadOnly() ? ST_RDONLY : 0) | ST_NOSUID; // No suid in pedigree yet.
    buf->f_namemax = VFS_MNAMELEN;
    
    // FS type
    StringCopy(buf->f_fstypename, "ext2");
    
    // "From" point
    /// \todo Disk device hash + path (on raw filesystem maybe?)
    StringCopy(buf->f_mntfromname, "from");
    
    // "To" point
    /// \todo What to put here?
    StringCopy(buf->f_mntfromname, "to");
    
    return 0;
}

int posix_fstatvfs(int fd, struct statvfs *buf)
{
    if(!PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(buf), sizeof(struct statvfs), PosixSubsystem::SafeWrite))
    {
        F_NOTICE("fstatvfs -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("fstatvfs(" << fd << ")");
    
    // Lookup this process.
    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    PosixSubsystem *pSubsystem = reinterpret_cast<PosixSubsystem*>(pProcess->getSubsystem());
    if (!pSubsystem)
    {
        ERROR("No subsystem for this process!");
        return -1;
    }

    FileDescriptor *pFd = pSubsystem->getFileDescriptor(fd);
    if (!pFd)
    {
        // Error - no such file descriptor.
        SYSCALL_ERROR(BadFileDescriptor);
        return -1;
    }
    
    File *file = pFd->file;

    return statvfs_doer(file->getFilesystem(), buf);
}

int posix_statvfs(const char *path, struct statvfs *buf)
{
    if(!(PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(path), PATH_MAX, PosixSubsystem::SafeRead) &&
        PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(buf), sizeof(struct statvfs), PosixSubsystem::SafeWrite)))
    {
        F_NOTICE("statvfs -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("statvfs(" << path << ")");

    String realPath;
    normalisePath(realPath, path);

    File* file = VFS::instance().find(realPath, GET_CWD());
    if (!file)
    {
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }

    // Symlink traversal
    file = traverseSymlink(file);
    if(!file)
        return -1;
    
    return statvfs_doer(file->getFilesystem(), buf);
}

int posix_utime(const char *path, const struct utimbuf *times)
{
    if(!(PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(path), PATH_MAX, PosixSubsystem::SafeRead) &&
        ((!times) || PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(times), sizeof(struct utimbuf), PosixSubsystem::SafeRead))))
    {
        F_NOTICE("utimes -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("utimes(" << path << ")");

    String realPath;
    normalisePath(realPath, path);

    File* file = VFS::instance().find(realPath, GET_CWD());
    if (!file)
    {
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }

    // Symlink traversal
    file = traverseSymlink(file);
    if(!file)
        return -1;

    if (!VFS::checkAccess(file, false, true, false))
    {
        // checkAccess does a SYSCALL_ERROR for us.
        return -1;
    }

    Time::Timestamp accessTime;
    Time::Timestamp modifyTime;
    if (times)
    {
        accessTime = times->actime * Time::Multiplier::SECOND;
        modifyTime = times->modtime * Time::Multiplier::SECOND;
    }
    else
    {
        accessTime = modifyTime = Time::getTime();
    }

    file->setAccessedTime(accessTime);
    file->setModifiedTime(modifyTime);

    return 0;
}

int posix_utimes(const char *path, const struct timeval *times)
{
    if(!(PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(path), PATH_MAX, PosixSubsystem::SafeRead) &&
        ((!times) || PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(times), sizeof(struct timeval) * 2, PosixSubsystem::SafeRead))))
    {
        F_NOTICE("utimes -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("utimes(" << path << ")");

    String realPath;
    normalisePath(realPath, path);

    File* file = VFS::instance().find(realPath, GET_CWD());
    if (!file)
    {
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }

    // Symlink traversal
    file = traverseSymlink(file);
    if(!file)
        return -1;

    if (!VFS::checkAccess(file, false, true, false))
    {
        // checkAccess does a SYSCALL_ERROR for us.
        return -1;
    }

    Time::Timestamp accessTime;
    Time::Timestamp modifyTime;
    if (times)
    {
        struct timeval access = times[0];
        struct timeval modify = times[1];

        accessTime = access.tv_sec * Time::Multiplier::SECOND;
        accessTime += access.tv_usec * Time::Multiplier::MICROSECOND;

        modifyTime = modify.tv_sec * Time::Multiplier::SECOND;
        modifyTime += modify.tv_usec * Time::Multiplier::MICROSECOND;
    }
    else
    {
        accessTime = modifyTime = Time::getTimeNanoseconds();
    }

    file->setAccessedTime(accessTime);
    file->setModifiedTime(modifyTime);

    return 0;
}

int posix_chroot(const char *path)
{
    if(!PosixSubsystem::checkAddress(reinterpret_cast<uintptr_t>(path), PATH_MAX, PosixSubsystem::SafeRead))
    {
        F_NOTICE("chroot -> invalid address");
        SYSCALL_ERROR(InvalidArgument);
        return -1;
    }

    F_NOTICE("chroot(" << path << ")");

    String realPath;
    normalisePath(realPath, path);

    File* file = VFS::instance().find(realPath, GET_CWD());
    if (!file)
    {
        SYSCALL_ERROR(DoesNotExist);
        return -1;
    }

    // Symlink traversal
    file = traverseSymlink(file);
    if(!file)
        return -1;

    // chroot must be a directory.
    if (!file->isDirectory())
    {
        SYSCALL_ERROR(NotADirectory);
        return -1;
    }

    Process *pProcess = Processor::information().getCurrentThread()->getParent();
    pProcess->setRootFile(file);

    return 0;
}

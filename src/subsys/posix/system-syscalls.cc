/*
 * Copyright (c) 2008 James Molloy, Jörg Pfähler, Matthew Iselin
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

#include "file-syscalls.h"
#include "system-syscalls.h"
#include "pipe-syscalls.h"
#include "syscallNumbers.h"
#include <syscallError.h>
#include <processor/types.h>
#include <processor/Processor.h>
#include <processor/VirtualAddressSpace.h>
#include <processor/state.h>
#include <process/Process.h>
#include <process/Thread.h>
#include <process/Scheduler.h>
#include <Log.h>
#include <linker/Elf.h>
#include <vfs/File.h>
#include <vfs/Symlink.h>
#include <vfs/VFS.h>
#include <panic.h>
#include <processor/PhysicalMemoryManager.h>
#include <processor/StackFrame.h>
#include <linker/DynamicLinker.h>
#include <utilities/String.h>
#include <utilities/Vector.h>
#include <machine/Machine.h>

#include <users/UserManager.h>
//
// Syscalls pertaining to system operations.
//

#define GET_CWD() (Processor::information().getCurrentThread()->getParent()->getCwd())

extern "C"
{
  extern void sigret_stub();
  extern char sigret_stub_end;
}

/// Saves a char** array in the Vector of String*s given.
static void save_string_array(const char **array, Vector<String*> &rArray)
{
  while (*array)
  {
    String *pStr = new String(*array);
    rArray.pushBack(pStr);
    array++;
  }
}
/// Creates a char** array, properly null-terminated, from the Vector of String*s given, at the location "arrayLoc",
/// returning the end of the char** array created in arrayEndLoc and the start as the function return value.
static char **load_string_array(Vector<String*> &rArray, uintptr_t arrayLoc, uintptr_t &arrayEndLoc)
{
  char **pMasterArray = reinterpret_cast<char**> (arrayLoc);

  char *pPtr = reinterpret_cast<char*> (arrayLoc + sizeof(char*) * (rArray.count()+1) );
  int i = 0;
  for (Vector<String*>::Iterator it = rArray.begin();
       it != rArray.end();
       it++)
  {
    String *pStr = *it;

    strcpy(pPtr, *pStr);
    pPtr[pStr->length()] = '\0'; // Ensure NULL-termination.

    pMasterArray[i] = pPtr;

    pPtr += pStr->length()+1;
    i++;

    delete pStr;
  }

  pMasterArray[i] = 0; // Null terminate.
  arrayEndLoc = reinterpret_cast<uintptr_t> (pPtr);

  return pMasterArray;
}

int posix_sbrk(int delta)
{
  int ret = reinterpret_cast<int>(
    Processor::information().getVirtualAddressSpace().expandHeap (delta, VirtualAddressSpace::Write));

  if (ret == 0)
  {
    SYSCALL_ERROR(OutOfMemory);
    return -1;
  }
  else
    return ret;
}

int posix_fork(ProcessorState state)
{
  SC_NOTICE("fork()");

  Processor::setInterrupts(false);

  // Create a new process.
  Process *pProcess = new Process(Processor::information().getCurrentThread()->getParent());

  if (!pProcess)
  {
    SYSCALL_ERROR(OutOfMemory);
    return -1;
  }

  // Register with the dynamic linker.
  pProcess->setLinker(new DynamicLinker(*pProcess->getLinker()));

  MemoryMappedFileManager::instance().clone(pProcess);

  typedef Tree<size_t,FileDescriptor*> FdMap;
  FdMap parentFdMap = Processor::information().getCurrentThread()->getParent()->getFdMap();
  FdMap childFdMap = pProcess->getFdMap();

  for(FdMap::Iterator it = parentFdMap.begin(); it != parentFdMap.end(); it++)
  {
    FileDescriptor* pFd = reinterpret_cast<FileDescriptor*> (it.value());
    if(!pFd)
      continue;
    size_t newFd = reinterpret_cast<size_t>(it.key());

    FileDescriptor *pFd2 = new FileDescriptor;

    pFd2->file = pFd->file;
    pFd2->offset = pFd->offset;
    pFd2->fd = pFd->fd;
    pFd2->fdflags = pFd->fdflags;
    pFd2->flflags = pFd->flflags;
    pProcess->getFdMap().insert(newFd, pFd2);
    pProcess->nextFd(newFd + 1);

    pFd2->file->increaseRefCount((pFd2->flflags & O_RDWR) || (pFd2->flflags & O_WRONLY) );
  }

  // Child returns 0.
  state.setSyscallReturnValue(0);

  // Create a new thread for the new process.
  new Thread(pProcess, state);

  // Parent returns child ID.
  return pProcess->getId();
}

int posix_execve(const char *name, const char **argv, const char **env, SyscallState &state)
{
  SC_NOTICE("execve(\"" << name << "\")");

  String myArgv;
  int i;
  while(argv[i])
  {
    myArgv += String(argv[i]);
    myArgv += " ";
    i++;
  }
  SC_NOTICE("  {" << myArgv << "}");

  Processor::setInterrupts(false);

  if (argv == 0 || env == 0)
  {
    SYSCALL_ERROR(ExecFormatError);
    return -1;
  }

  Process *pProcess = Processor::information().getCurrentThread()->getParent();
  pProcess->getSpaceAllocator() = MemoryAllocator();
  pProcess->getSpaceAllocator().free(0x00100000, 0x80000000);

  // Ensure we only have one thread running (us).
  if (pProcess->getNumThreads() > 1)
  {
    SYSCALL_ERROR(ExecFormatError);
    return -1;
  }

  // Attempt to find the file, first!
  File* file = VFS::instance().find(String(name), Processor::information().getCurrentThread()->getParent()->getCwd());

  if (!file)
  {
    // Error - not found.
    SYSCALL_ERROR(DoesNotExist);
    return -1;
  }

  while (file->isSymlink())
    file = Symlink::fromFile(file)->followLink();

  if (file->isDirectory())
  {
    // Error - is directory.
    SYSCALL_ERROR(IsADirectory);
    return -1;
  }

  pProcess->description() = String(name);

  // Save the argv and env lists so they aren't destroyed when we overwrite the address space.
  Vector<String*> savedArgv, savedEnv;
  save_string_array(argv, savedArgv);
  save_string_array(env, savedEnv);

  // Get rid of all the crap from the last elf image.
  /// \todo Preserve anonymous mmaps etc.

  MemoryMappedFileManager::instance().unmapAll();

  pProcess->getAddressSpace()->revertToKernelAddressSpace();

  DynamicLinker *pOldLinker = pProcess->getLinker();
  DynamicLinker *pLinker = new DynamicLinker();

  // Set the new linker now before we loadProgram, else we could trap and 
  // have a linker mismatch.
  pProcess->setLinker(pLinker);

  if (!pLinker->loadProgram(file))
  {
      /// \todo Check for a shebang here.
      pProcess->setLinker(pOldLinker);
      delete pLinker;
      SYSCALL_ERROR(ExecFormatError);
      return -1;
  }
  delete pOldLinker;

  // Close all FD_CLOEXEC descriptors. Done here because from this point we're committed to running -
  // there's no further return until the end of the function.
  typedef Tree<size_t,FileDescriptor*> FdMap;
  FdMap parentFdMap = Processor::information().getCurrentThread()->getParent()->getFdMap();
  for(FdMap::Iterator it = parentFdMap.begin(); it != parentFdMap.end(); it++)
  {
    FileDescriptor* pFd = reinterpret_cast<FileDescriptor*> (it.value());
    if(pFd)
      if(pFd->fdflags & FD_CLOEXEC)
        posix_close(reinterpret_cast<int>(it.key()));
  }

  // Create a new stack.
  for (int j = 0; j < STACK_START-STACK_END; j += PhysicalMemoryManager::getPageSize())
  {
    physical_uintptr_t phys = PhysicalMemoryManager::instance().allocatePage();
    bool b = Processor::information().getVirtualAddressSpace().map(phys,
                                                                   reinterpret_cast<void*> (j+STACK_END),
                                                                   VirtualAddressSpace::Write);
    if (!b)
      WARNING("map() failed in execve");
  }

  // Create room for the argv and env list.
  for (int j = 0; j < ARGV_ENV_LEN; j += PhysicalMemoryManager::getPageSize())
  {
    physical_uintptr_t phys = PhysicalMemoryManager::instance().allocatePage();
    bool b = Processor::information().getVirtualAddressSpace().map(phys,
                                                                   reinterpret_cast<void*> (j+ARGV_ENV_LOC),
                                                                   VirtualAddressSpace::Write);
    if (!b)
      WARNING("map() failed in execve (2)");
  }

  // Load the saved argv and env into this address space, starting at "location".
  uintptr_t location = ARGV_ENV_LOC;
  argv = const_cast<const char**> (load_string_array(savedArgv, location, location));
  env  = const_cast<const char**> (load_string_array(savedEnv , location, location));

  Elf *elf = pProcess->getLinker()->getProgramElf();

  ProcessorState pState = state;
  pState.setStackPointer(STACK_START-8);
  pState.setInstructionPointer(elf->getEntryPoint());

  StackFrame::construct(pState, 0, 2, argv, env);

  state.setStackPointer(pState.getStackPointer());
  state.setInstructionPointer(elf->getEntryPoint());

  /// \todo Can this go somewhere other than 0x50000000? Stack perhaps?
  physical_uintptr_t phys = PhysicalMemoryManager::instance().allocatePage();
  Processor::information().getVirtualAddressSpace().map(phys,
                                                        reinterpret_cast<void*> (0x50000000),
                                                        VirtualAddressSpace::Write);
  NOTICE("Copying " << (reinterpret_cast<uintptr_t>(&sigret_stub_end) - reinterpret_cast<uintptr_t>(sigret_stub)) << " bytes.");
  memcpy(reinterpret_cast<void*>(0x50000000), reinterpret_cast<void*>(sigret_stub), (reinterpret_cast<uintptr_t>(&sigret_stub_end) - reinterpret_cast<uintptr_t>(sigret_stub)));
  pProcess->setSigReturnStub(0x50000000);

  /// \todo Genericize this somehow - "pState.setScratchRegisters(state)"?
#ifdef PPC_COMMON
  state.m_R6 = pState.m_R6;
  state.m_R7 = pState.m_R7;
  state.m_R8 = pState.m_R8;
#endif

  return 0;
}

int posix_waitpid(int pid, int *status, int options)
{
  if (options & 1)
  {
    SC_NOTICE("waitpid(pid=" << Dec << pid << Hex << ", WNOHANG)");
  }
  else
  {
    SC_NOTICE("waitpid(pid=" << Dec << pid << Hex << ")");
  }

  // Don't care about process groups at the moment.
  if (pid < -1)
  {
    pid *= -1;
  }

  while (1)
  {
    // Is the pid an absolute pid reference?
    if (pid > 0)
    {
      // Does this process exist?
      Process *pProcess = 0;
      for (size_t i = 0; i < Scheduler::instance().getNumProcesses(); i++)
      {
        pProcess = Scheduler::instance().getProcess(i);

        if (static_cast<int>(pProcess->getId()) == pid)
          break;
      }
      if(pProcess == 0)
        return -1;

      if (static_cast<int>(pProcess->getId()) != pid)
      {
        // ECHILD - process n'existe pas.
        return -1;
      }

      // Is it actually our child?
      if (pProcess->getParent() != Processor::information().getCurrentThread()->getParent())
      {
        // ECHILD - not our child!
        return -1;
      }

      // Is it zombie?
      if (pProcess->getThread(0)->getStatus() == Thread::Zombie)
      {
        // Ph33r the Reaper...
        if (status)
          *status = pProcess->getExitStatus();

        // Delete the process; it's been reaped good and proper.
        delete pProcess;
        return pid;
      }
    }
    else
    {
      // Get any pid.
      Process *thisP = Processor::information().getCurrentThread()->getParent();
      bool hadAnyChildren = false;
      for (size_t i = 0; i < Scheduler::instance().getNumProcesses(); i++)
      {
        Process *pProcess = Scheduler::instance().getProcess(i);

        if (pProcess->getParent() == thisP)
        {
          hadAnyChildren = true;
          if (pProcess->getThread(0)->getStatus() == Thread::Zombie)
          {
            // Kill 'em all!
            if (status)
              *status = pProcess->getExitStatus();
            pid = pProcess->getId();
            delete pProcess;
            return pid;
          }
        }
      }
      if (!hadAnyChildren)
      {
        // Error - no children (ECHILD)
        return -1;
      }
    }

    if (options & 1) // WNOHANG set
      return 0;
    /// \todo Bugfix - the following line should be deleted, and the line below uncommented.
    //Scheduler::instance().yield(0);
    // Sleep...
    Processor::information().getCurrentThread()->getParent()->m_DeadThreads.acquire();
  }

  return -1;
}

int posix_exit(int code)
{
  SC_NOTICE("exit(" << Dec << (code&0xFF) << Hex << ")");

  /// Here, we should close ALL file descriptors!
  Process *pProcess = Processor::information().getCurrentThread()->getParent();
  pProcess->setExitStatus( (code&0xFF) << 8 );

  typedef Tree<size_t,FileDescriptor*> FdMap;
  FdMap parentFdMap = Processor::information().getCurrentThread()->getParent()->getFdMap();
  for(FdMap::Iterator it = parentFdMap.begin(); it != parentFdMap.end(); it++)
  {
    FileDescriptor* pFd = reinterpret_cast<FileDescriptor*> (it.value());
    if(!pFd)
      continue;

    pFd->file->decreaseRefCount((pFd->flflags & O_RDWR) || (pFd->flflags & O_WRONLY) );
    delete pFd;
  }

  delete pProcess->getLinker();

  MemoryMappedFileManager::instance().unmapAll();

  pProcess->kill();

  // Should NEVER get here.
  for(;;);
}

int posix_getpid()
{
  Process *pProcess = Processor::information().getCurrentThread()->getParent();
  return pProcess->getId();
}

int posix_gettimeofday(timeval *tv, timezone *tz)
{
  Timer *pTimer = Machine::instance().getTimer();

  tv->tv_sec = pTimer->getUnixTimestamp();
  tv->tv_usec = 0;

  return 0;
}

char *store_str_to(char *str, char *strend, String s)
{
  int i = 0;
  while (s[i] && str != strend)
    *str++ = s[i++];
  *str++ = '\0';

  return str;
}

int posix_getpwent(passwd *pw, int n, char *str)
{
  SC_NOTICE("getpwent(" << Dec << n << Hex << ")");

  // Grab the given user.
  User *pUser = UserManager::instance().getUser(n);
  if (!pUser) return -1;

  char *strend = str + 256; // If we get here, we've gone off the end of str.

  pw->pw_name = str;
  str = store_str_to(str, strend, pUser->getUsername());

  pw->pw_passwd = str;
  *str++ = '\0';

  pw->pw_uid = pUser->getId();
  pw->pw_gid = pUser->getDefaultGroup()->getId();
  pw->pw_comment = str;
  str = store_str_to(str, strend, pUser->getFullName());

  pw->pw_gecos = str;
  *str++ = '\0';
  pw->pw_dir = str;
  str = store_str_to(str, strend, pUser->getHome());

  pw->pw_shell = str;
  str = store_str_to(str, strend, pUser->getShell());

  return 0;
}

int posix_getpwnam(passwd *pw, const char *name, char *str)
{
  // Grab the given user.
  User *pUser = UserManager::instance().getUser(String(name));
  if (!pUser) return -1;

  char *strend = str + 256; // If we get here, we've gone off the end of str.

  pw->pw_name = str;
  str = store_str_to(str, strend, pUser->getUsername());

  pw->pw_passwd = str;
  *str++ = '\0';

  pw->pw_uid = pUser->getId();
  pw->pw_gid = pUser->getDefaultGroup()->getId();
  pw->pw_comment = str;
  str = store_str_to(str, strend, pUser->getFullName());

  pw->pw_gecos = str;
  *str++ = '\0';

  pw->pw_dir = str;
  str = store_str_to(str, strend, pUser->getHome());

  pw->pw_shell = str;
  str = store_str_to(str, strend, pUser->getShell());

  return 0;
}

int posix_getuid()
{
  SC_NOTICE("getuid() -> " << Dec << Processor::information().getCurrentThread()->getParent()->getUser()->getId());
  return Processor::information().getCurrentThread()->getParent()->getUser()->getId();
}

int posix_getgid()
{
  SC_NOTICE("getgid() -> " << Dec << Processor::information().getCurrentThread()->getParent()->getGroup()->getId());
  return Processor::information().getCurrentThread()->getParent()->getGroup()->getId();
}

int pedigree_login(int uid, const char *password)
{
  // Grab the given user.
  User *pUser = UserManager::instance().getUser(uid);
  if (!pUser) return -1;

  if (pUser->login(String(password)))
    return 0;
  else
    return -1;
}

/*********************** Signals implementation ***********************/

// useful macros from sys/signal.h
#define sigaddset(what,sig) (*(what) |= (1<<(sig)), 0)
#define sigdelset(what,sig) (*(what) &= ~(1<<(sig)), 0)
#define sigemptyset(what)   (*(what) = 0, 0)
#define sigfillset(what)    (*(what) = ~(0), 0)
#define sigismember(what,sig) (((*(what)) & (1<<(sig))) != 0)

int posix_sigaction(int sig, const struct sigaction *act, struct sigaction *oact, int type)
{
  NOTICE("sigaction(" << Dec << sig << Hex << ")");

  Thread* pThread = Processor::information().getCurrentThread();
  Process* pProcess = pThread->getParent();

  // store the old signal handler information if we can
  if(oact)
  {
    Process::SignalHandler* oldSignalHandler = pProcess->getSignalHandler(sig);
    if(oldSignalHandler)
    {
      oact->sa_flags = oldSignalHandler->flags;
      oact->sa_mask = oldSignalHandler->sigMask;
      if(oldSignalHandler->type == 0)
        oact->sa_handler = reinterpret_cast<void (*)(int)>(oldSignalHandler->handlerLocation);
      else if(oldSignalHandler->type == 1)
        oact->sa_handler = reinterpret_cast<void (*)(int)>(0);
      else if(oldSignalHandler->type == 2)
        oact->sa_handler = reinterpret_cast<void (*)(int)>(1);
    }
    else
      memset(oact, 0, sizeof(struct sigaction));
  }

  if(act)
  {
    Process::SignalHandler* sigHandler = new Process::SignalHandler;
    sigHandler->handlerLocation = reinterpret_cast<uintptr_t>(act->sa_handler);
    sigHandler->sigMask = act->sa_mask;
    sigHandler->flags = act->sa_flags;
    sigHandler->type = type;
    pProcess->setSignalHandler(sig, sigHandler);
  }
  else if(!oact)
  {
    SYSCALL_ERROR(InvalidArgument);
    return -1;
  }

  return 0;
}

uintptr_t posix_signal(int sig, void* func)
{
  ERROR("signal called but glue signal should redirect to sigaction");
  return 0;
}

int posix_raise(int sig)
{
  NOTICE("raise");

  // create the pending signal and pass it in
  Process* pProcess = Processor::information().getCurrentThread()->getParent();
  Process::SignalHandler* signalHandler = pProcess->getSignalHandler(sig);
  Process::PendingSignal* pendingSignal = new Process::PendingSignal;
  if(signalHandler)
    pendingSignal->sigMask = signalHandler->sigMask;

  // add the signal to the queue
  pProcess->addPendingSignal(static_cast<size_t>(sig), pendingSignal);

  // let another thread be scheduled; we won't come back until after the pending signal is handled
  Scheduler::instance().yield(0);

  // reset the saved state so reschedules work properly
  Processor::information().getCurrentThread()->setSavedInterruptState(0);

  return 0;
}

int pedigree_sigret()
{
  NOTICE("pedigree_sigret");

  // we do not want an interrupt until we're waiting for the signal handler to complete
  bool bInterrupts = Processor::getInterrupts();
  if(bInterrupts)
    Processor::setInterrupts(false);

  Thread* pThread = Processor::information().getCurrentThread();

  // set the parent signal mask back the way it was
  pThread->getParent()->setSignalMask(pThread->getCurrentSignal().oldMask);

  // reset the thread information
  Thread::CurrentSignal curr;
  pThread->setCurrentSignal(curr);

  // when we reschedule we will go to the saved state rather than the state picked up
  // when this thread is switched *from*
  pThread->useSaved(true);

  // and now that we're done fiddling with the thread let us be interrupted
  if(bInterrupts)
    Processor::setInterrupts(true);

  // reschedule, when we get back to this thread it'll load the old state
  Scheduler::instance().yield(0);
  while(1)
    Processor::halt();

  return 0;
}

int posix_kill(int pid, int sig)
{
  NOTICE("kill(" << pid << ", " << sig << ")");

  Process* p = Scheduler::instance().getProcess(static_cast<size_t>(pid));
  if(p)
  {
    if((p->getSignalMask() & (1 << sig)))
    {
      /// \todo What happens when the signal is blocked?
      return 0;
    }

    // build the pending signal and pass it in
    Process::SignalHandler* signalHandler = p->getSignalHandler(sig);
    Process::PendingSignal* pendingSignal = new Process::PendingSignal;
    pendingSignal->sigMask = signalHandler->sigMask;

    // add the signal to the queue
    p->addPendingSignal(static_cast<size_t>(sig), pendingSignal);
  }
  else
  {
    SYSCALL_ERROR(NoSuchProcess);
    return -1;
  }

  return 0;
}

int posix_sigprocmask(int how, const uint32_t *set, uint32_t *oset)
{
  NOTICE("sigprocmask");

  uint32_t currMask = Processor::information().getCurrentThread()->getParent()->getSignalMask();

  if(oset)
  {
    // if no actual passed set, the how argument is invalid and we merely return the current mask
    *oset = currMask;
    if(!set)
      return 0;
  }
  if(!set)
  {
    SYSCALL_ERROR(InvalidArgument);
    return -1;
  }

  uint32_t passedMask = *set;

  // SIGKILL and SIGSTOP are not blockable
  sigdelset(&passedMask, 9);
  sigdelset(&passedMask, 17);

  uint32_t returnMask = 0;
  bool bProcessed = false;
  switch(how)
  {
    // SIG_BLOCK: union of the current set and the passed set
    case 0:
      returnMask = currMask | passedMask;
      bProcessed = true;
      break;
     // SIG_SETMASK: set the mask to the passed set
    case 1:
      returnMask = passedMask;
      bProcessed = true;
      break;
    // SIG_UNBLOCK: unset the bits in the passed set
    case 2:
      returnMask = currMask ^ passedMask;
      bProcessed = true;
      break;
  };

  if(!bProcessed)
  {
    SYSCALL_ERROR(InvalidArgument);
    return -1;
  }
  else
  {
    Processor::information().getCurrentThread()->getParent()->setSignalMask(returnMask);

    // now that the new signal mask is set, reschedule so that any (now unblocked) signals may run
    Scheduler::instance().yield(0);
  }

  return 0;
}

int posix_alarm(uint32_t seconds)
{
  /// \todo Implement
  NOTICE("alarm");
  return 0;
}

int posix_sleep(uint32_t seconds)
{
  /// \todo Implement
  NOTICE("sleep");
  return 0;
}

/// \note Currently all functionality can be provided without any extra storage in the handle.
struct dlHandle
{
  int mode;
};

uintptr_t posix_dlopen(const char* file, int mode, void* p)
{
  SC_NOTICE("dlopen(" << file << ")");

  File *pFile = VFS::instance().find(String(file), GET_CWD());

  if (!pFile)
  {
      SYSCALL_ERROR(DoesNotExist);
      return 0;
  }

  while (pFile->isSymlink())
      pFile = Symlink::fromFile(pFile)->followLink();

  if (pFile->isDirectory())
  {
      // Error - is directory.
      SYSCALL_ERROR(IsADirectory);
      return 0;
  }

  Process *pProcess = Processor::information().getCurrentThread()->getParent();
  
  if(!pProcess->getLinker()->loadObject(pFile))
  {
    ERROR("dlopen: couldn't load " << String(file) << ".");
    return 0;
  }
  
  dlHandle* handle = reinterpret_cast<dlHandle*>(p);
  
  return reinterpret_cast<uintptr_t>(handle);
}

// m_ProcessObjects should be better to use!

uintptr_t posix_dlsym(void* handle, const char* name)
{
  SC_NOTICE("dlsym(" << name << ")");
  
  if(!handle)
      return 0;
  
  Process *pProcess = Processor::information().getCurrentThread()->getParent();

  return pProcess->getLinker()->resolve(String(name));
}

int posix_dlclose(void* handle)
{
  SC_NOTICE("dlclose");
  
  return 0;
}


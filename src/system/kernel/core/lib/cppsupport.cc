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

#include <processor/types.h>
#include <Spinlock.h>
#include "cppsupport.h"
#include <panic.h>
#include <processor/Processor.h>
#include <processor/PhysicalMemoryManager.h>
#include <processor/VirtualAddressSpace.h>
#include <Log.h>

#include "SlamAllocator.h"

/// If the debug allocator is enabled, this switches it into underflow detection
/// mode.
#define DEBUG_ALLOCATOR_CHECK_UNDERFLOWS

// Required for G++ to link static init/destructors.
void *__dso_handle;

// Defined in the linker.
uintptr_t start_ctors;
uintptr_t end_ctors;

#ifdef USE_DEBUG_ALLOCATOR
Spinlock allocLock;
uintptr_t heapBase = 0x60000000; // 0xC0000000;
#endif

/// Calls the constructors for all global objects.
/// Call this before using any global objects.
void initialiseConstructors()
{
  // Constructor list is defined in the linker script.
  // The .ctors section is just an array of function pointers.
  // iterate through, calling each in turn.
  uintptr_t *iterator = reinterpret_cast<uintptr_t*>(&start_ctors);
  while (iterator < reinterpret_cast<uintptr_t*>(&end_ctors))
  {
    void (*fp)(void) = reinterpret_cast<void (*)(void)>(*iterator);
    fp();
    iterator++;
  }
}

/// Required for G++ to compile code.
extern "C" void __cxa_atexit(void (*f)(void *), void *p, void *d)
{
}

/// Called by G++ if a pure virtual function is called. Bad Thing, should never happen!
extern "C" void __cxa_pure_virtual()
{
    panic("Pure virtual function call made");
}

/// Called by G++ if function local statics are initialised for the first time
extern "C" int __cxa_guard_acquire()
{
  return 1;
}
extern "C" void __cxa_guard_release()
{
  // TODO
}

extern "C" void *malloc(size_t sz)
{
    return reinterpret_cast<void *>(new uint8_t[sz]); //SlamAllocator::instance().allocate(sz));
}

extern "C" void free(void *p)
{
    if (p == 0)
        return;
    //SlamAllocator::instance().free(reinterpret_cast<uintptr_t>(p));
    delete reinterpret_cast<uint8_t*>(p);
}

extern "C" void *realloc(void *p, size_t sz)
{
    if (p == 0)
        return malloc(sz);
    if (sz == 0)
    {
        free(p);
        return 0;
    }
    
    void *tmp = malloc(sz);
    memcpy(tmp, p, sz);
    free(p);

    return tmp;
}

void *operator new (size_t size) throw()
{
#ifdef USE_DEBUG_ALLOCATOR

    /// \todo underflow flag
    
    // Full size of the region to allocate
    size_t nPages = ((size / 0x1000) + 1);
    size_t realSize = nPages * 0x1000;
    
    allocLock.acquire();
    
    // Find the base of the unmapped region (overflow check)
    uintptr_t currBase = heapBase;
    uintptr_t overflowBase = currBase + realSize;
    
    // Update the base of the heap
    heapBase = overflowBase + 0x1000;
    
    allocLock.release();
    
    // We return a block just before the unmapped region
    uintptr_t ret = overflowBase - size;
    
    // Map in the pages we actually want to access
    VirtualAddressSpace &va = Processor::information().getVirtualAddressSpace();
    for(size_t i = 0; i < nPages; i++)
    {
        physical_uintptr_t page = PhysicalMemoryManager::instance().allocatePage();
        // NOTICE("Mapping " << (currBase + (i * 0x1000)) << " to " << page);
        bool success = va.map(
                page,
                reinterpret_cast<void*>(currBase + (i * 0x1000)),
                VirtualAddressSpace::KernelMode | VirtualAddressSpace::Write
        );
        if(!success)
            FATAL("Debug allocator - mapping failed!");
    }
    
    // NOTICE("debug allocator returning " << ret);
    
    return reinterpret_cast<void*>(ret);

#elif defined(X86_COMMON) || defined(MIPS_COMMON) || defined(PPC_COMMON)
    void *ret = reinterpret_cast<void *>(SlamAllocator::instance().allocate(size));
    return ret;
#else
    return 0;
#endif
}
void *operator new[] (size_t size) throw()
{
#ifdef USE_DEBUG_ALLOCATOR
    
    /// \todo underflow flag
    
    // Full size of the region to allocate
    size_t nPages = ((size / 0x1000) + 1);
    size_t realSize = nPages * 0x1000;
    
    allocLock.acquire();
    
    // Find the base of the unmapped region (overflow check)
    uintptr_t currBase = heapBase;
    uintptr_t overflowBase = currBase + realSize;
    
    // Update the base of the heap
    heapBase = overflowBase + 0x1000;
    
    allocLock.release();
    
    // We return a block just before the unmapped region
    uintptr_t ret = overflowBase - size;
    
    // Map in the pages we actually want to access
    VirtualAddressSpace &va = Processor::information().getVirtualAddressSpace();
    for(size_t i = 0; i < nPages; i++)
    {
        physical_uintptr_t page = PhysicalMemoryManager::instance().allocatePage();
        // NOTICE("Mapping " << (currBase + (i * 0x1000)) << " to " << page);
        bool success = va.map(
                page,
                reinterpret_cast<void*>(currBase + (i * 0x1000)),
                VirtualAddressSpace::KernelMode | VirtualAddressSpace::Write
        );
        if(!success)
            FATAL("Debug allocator - mapping failed!");
    }
    
    // NOTICE("debug allocator returning " << ret);
    
    return reinterpret_cast<void*>(ret);

#elif defined(X86_COMMON) || defined(MIPS_COMMON) || defined(PPC_COMMON)
    void *ret = reinterpret_cast<void *>(SlamAllocator::instance().allocate(size));
    return ret;
#else
    return 0;
#endif
}
void *operator new (size_t size, void* memory) throw()
{
  return memory;
}
void *operator new[] (size_t size, void* memory) throw()
{
  return memory;
}
void operator delete (void * p)
{
#ifdef USE_DEBUG_ALLOCATOR
    return;
#endif

#if defined(X86_COMMON) || defined(MIPS_COMMON) || defined(PPC_COMMON)
    if (p == 0) return;
    SlamAllocator::instance().free(reinterpret_cast<uintptr_t>(p));
#endif
}
void operator delete[] (void * p)
{
#ifdef USE_DEBUG_ALLOCATOR
    return;
#endif

#if defined(X86_COMMON) || defined(MIPS_COMMON) || defined(PPC_COMMON)
    if (p == 0) return;
    SlamAllocator::instance().free(reinterpret_cast<uintptr_t>(p));
#endif
}
void operator delete (void *p, void *q)
{
  // TODO
  panic("Operator delete -implement");
}
void operator delete[] (void *p, void *q)
{
  // TODO
  panic("Operator delete[] -implement");
}

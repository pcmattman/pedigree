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

#include <Log.h>
#include <machine/Machine.h>
#include <machine/Timer.h>
#include <process/Scheduler.h>
#include <process/Semaphore.h>
#include <processor/Processor.h>

#include <utilities/assert.h>

void interruptSemaphore(uint8_t *pBuffer)
{
    Processor::information().getCurrentThread()->setInterrupted(true);
}

Semaphore::SemaphoreEvent::SemaphoreEvent() :
    Event(reinterpret_cast<uintptr_t>(&interruptSemaphore), false /* Not deletable */)
{
}

Semaphore::Semaphore(size_t nInitialValue)
    : magic(0xdeadbaba), m_Counter(nInitialValue), m_BeingModified(false), m_Queue()
{
    assert(magic == 0xdeadbaba);
}

Semaphore::~Semaphore()
{
    assert(magic == 0xdeadbaba);
}

void Semaphore::acquire(size_t n, size_t timeoutSecs, size_t timeoutUsecs)
{
    assert(magic == 0xdeadbaba);
  // Spin 10 times in the case that the lock is about to be released on
  // multiprocessor systems, and just once for uniprocessor systems, so we don't
  // go through the rigmarole of creating a timeout event if the lock is
  // available.
#ifdef MULTIPROCESSOR
  for (int i = 0; i < 10; i++)
#endif
    if (tryAcquire(n))
      return;

  // If we have a timeout, create the event and register it.
  Event *pEvent = 0;
  if (timeoutSecs || timeoutUsecs)
  {
      pEvent = new SemaphoreEvent();
      Machine::instance().getTimer()->addAlarm(pEvent, timeoutSecs, timeoutUsecs);
  }

  while (true)
  {
    Thread *pThread = Processor::information().getCurrentThread();
    if (tryAcquire(n))
    {
      if (pEvent)
      {
          Machine::instance().getTimer()->removeAlarm(pEvent);
          delete pEvent;
      }
      return;
    }

    m_BeingModified.acquire();

    // To avoid a race condition, check again here after we've disabled interrupts.
    // This stops the condition where the lock is released after tryAcquire returns false,
    // but before we grab the "being modified" lock, which means the lock could be released by this point!
    if (tryAcquire(n))
    {
      if (pEvent)
      {
        Machine::instance().getTimer()->removeAlarm(pEvent);
        delete pEvent;
      }
      m_BeingModified.release();
      return;
    }

    m_Queue.pushBack(pThread);

    pThread->setInterrupted(false);
    pThread->setDebugState(Thread::SemWait, reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
    Processor::information().getScheduler().sleep(&m_BeingModified);
    pThread->setDebugState(Thread::None, 0);

    // Why were we woken?
    if (pThread->wasInterrupted() || pThread->getUnwindState() != Thread::Continue)
    {
        // We were deliberately interrupted - most likely because of a timeout.
        if (pEvent)
        {
          Machine::instance().getTimer()->removeAlarm(pEvent);
          delete pEvent;
        }
        return;
    }
  }

}

bool Semaphore::tryAcquire(size_t n)
{
  ssize_t value = m_Counter;

  if ((value - static_cast<ssize_t>(n)) < 0)return false;
  if (m_Counter.compareAndSwap(value, value - n))
  {
    #ifdef STRICT_LOCK_ORDERING
      // TODO LockManager::acquired(*this);
    #endif
    return true;
  }
  return false;
}

void Semaphore::release(size_t n)
{
    assert(magic == 0xdeadbaba);
  m_Counter += n;

  m_BeingModified.acquire();

  while(m_Queue.count() != 0)
  {
    // TODO: Check for dead thread.
    Thread *pThread = m_Queue.popFront();

    pThread->getLock().acquire();
    pThread->setStatus(Thread::Ready);
    pThread->getLock().release();
  }

  m_BeingModified.release();

  #ifdef STRICT_LOCK_ORDERING
    // TODO LockManager::released(*this);
  #endif
}

ssize_t Semaphore::getValue()
{
    return static_cast<ssize_t>(m_Counter);
}

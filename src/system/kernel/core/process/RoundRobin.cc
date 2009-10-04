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

#if defined(THREADS)
#include <process/RoundRobin.h>
#include <process/Thread.h>
#include <processor/Processor.h>
#include <Log.h>
#include <LockGuard.h>

RoundRobin::RoundRobin() :
  m_List(), m_Lock(false)
{
}

RoundRobin::~RoundRobin()
{
}

void RoundRobin::addThread(Thread *pThread)
{
}

void RoundRobin::removeThread(Thread *pThread)
{
  LockGuard<Spinlock> guard(m_Lock);

  for(ThreadList::Iterator it = m_List.begin();
      it != m_List.end();
      it++)
  {
    if (*it == pThread)
    {
      m_List.erase (it);
      return;
    }
  }
}

Thread *RoundRobin::getNext()
{
    LockGuard<Spinlock> guard(m_Lock);

    Thread *pThread = 0;
    if (m_List.size())
        pThread = m_List.popFront();

    if (pThread)
        pThread->getLock().acquire();

    return pThread;

}

void RoundRobin::threadStatusChanged(Thread *pThread)
{
    if (pThread->getStatus() == Thread::Ready)
    {
        m_List.pushBack(pThread);
    }
}

#endif

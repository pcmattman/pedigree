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

#include "VbeDisplay.h"
#include <Log.h>
#include <machine/x86_common/Bios.h>
#include <machine/Vga.h>
#include <machine/Machine.h>
#include <processor/PhysicalMemoryManager.h>
#include <TUI/TuiSyscallManager.h>

extern TuiSyscallManager g_TuiSyscallManager;

VbeDisplay::VbeDisplay() : m_VbeVersion(), m_ModeList(), m_Mode(), m_pFramebuffer(), m_Buffers(), m_Allocator()
{
 
}

VbeDisplay::VbeDisplay(Device *p, VbeVersion version, List<Display::ScreenMode*> &sms, size_t vidMemSz) :
    Display(p), m_VbeVersion(version), m_ModeList(sms), m_Mode(), m_pFramebuffer(),
    m_Buffers(), m_Allocator()
{
  Display::ScreenMode *pSm = 0;
  // Try to find mode 0x117 (1024x768x16)
  for (List<Display::ScreenMode*>::Iterator it = m_ModeList.begin();
       it != m_ModeList.end();
       it++)
  {
    if ((*it)->id == 0x117)
    {
      pSm = *it;
      break;
    }
  }
  if (pSm == 0)
  {
    FATAL("Screenmode not found");
  }

  for (Vector<Device::Address*>::Iterator it = m_Addresses.begin();
       it != m_Addresses.end();
       it++)
  {
    if ((*it)->m_Address == pSm->framebuffer)
    {
      m_pFramebuffer = static_cast<MemoryMappedIo*> ((*it)->m_Io);
      break;
    }
  }

  m_Allocator.free(0, vidMemSz);

  setScreenMode(*pSm);
}

VbeDisplay::~VbeDisplay()
{
}

void *VbeDisplay::getFramebuffer()
{
  return reinterpret_cast<void*> (m_pFramebuffer->virtualAddress());
}

bool VbeDisplay::getPixelFormat(Display::PixelFormat *pPf)
{
  memcpy(pPf, &m_Mode.pf, sizeof(Display::PixelFormat));
  return true;
}

bool VbeDisplay::getCurrentScreenMode(Display::ScreenMode &sm)
{
  sm = m_Mode;
  return true;
}

bool VbeDisplay::getScreenModes(List<Display::ScreenMode*> &sms)
{
  sms = m_ModeList;
  return true;
}

bool VbeDisplay::setScreenMode(Display::ScreenMode sm)
{
  m_Mode = sm;

  // SET SuperVGA VIDEO MODE - AX=4F02h, BX=new mode
  Bios::instance().setAx (0x4F02);
  Bios::instance().setBx (m_Mode.id | (1<<14));
  Bios::instance().setEs (0x0000);
  Bios::instance().setDi (0x0000);
  Bios::instance().executeInterrupt (0x10);

  // Check the signature.
  if (Bios::instance().getAx() != 0x004F)
  {
    ERROR("VBE: Set mode failed! (mode " << Hex << m_Mode.id << ")");
    return false;
  }
  NOTICE("VBE: Set mode " << m_Mode.id);

  // Tell the Machine instance what VBE mode we're in, so it can set it again if we enter the debugger and return.
  Machine::instance().getVga(0)->setMode(m_Mode.id);

  // Inform the TUI that the current mode has changed.
  g_TuiSyscallManager.modeChanged(this, m_Mode, m_pFramebuffer->physicalAddress(), m_pFramebuffer->size());

  return true;
}

VbeDisplay::rgb_t *VbeDisplay::newBuffer()
{
    Buffer *pBuffer = new Buffer;

    size_t pgmask = PhysicalMemoryManager::getPageSize()-1;
    size_t sz = m_Mode.width*m_Mode.height * sizeof(rgb_t);

    if (sz & pgmask) sz += PhysicalMemoryManager::getPageSize();
    sz &= ~pgmask;

    if (!PhysicalMemoryManager::instance().allocateRegion(pBuffer->mr,
                                                          sz / PhysicalMemoryManager::getPageSize(),
                                                          0,
                                                          VirtualAddressSpace::Write,
                                                          -1))
    {
        ERROR("VbeDisplay::newBuffer: allocateRegion failed!");
    }

    pBuffer->pBackbuffer = reinterpret_cast<rgb_t*>(pBuffer->mr.virtualAddress());

    sz = m_Mode.width*m_Mode.height * (m_Mode.pf.nBpp/8);
    if (sz & pgmask) sz += PhysicalMemoryManager::getPageSize();
    sz &= ~pgmask;

    if (!PhysicalMemoryManager::instance().allocateRegion(pBuffer->fbmr,
                                                          sz / PhysicalMemoryManager::getPageSize(),
                                                          0,
                                                          VirtualAddressSpace::Write,
                                                          -1))
    {
        ERROR("VbeDisplay::newBuffer: allocateRegion failed! (1)");
    }

    pBuffer->pFbBackbuffer = reinterpret_cast<uint8_t*>(pBuffer->fbmr.virtualAddress());

    m_Buffers.insert(pBuffer->pBackbuffer, pBuffer);

    NOTICE("New buffer: returning " << (uintptr_t)pBuffer->pBackbuffer);

    return pBuffer->pBackbuffer;
}

void VbeDisplay::setCurrentBuffer(rgb_t *pBuffer)
{
    Buffer *pBuf = m_Buffers.lookup(pBuffer);
    if (!pBuf)
    {
        ERROR("VbeDisplay: Bad buffer:" << reinterpret_cast<uintptr_t>(pBuffer));
        return;
    }
    NOTICE("setCurrentbuffer");
    memcpy(getFramebuffer(), pBuf->pFbBackbuffer, m_Mode.width*m_Mode.height * (m_Mode.pf.nBpp/8));
}

void VbeDisplay::updateBuffer(rgb_t *pBuffer, size_t x1, size_t y1, size_t x2,
                              size_t y2)
{
    NOTICE("updateBuffer: " << x1 << ", " << y1 << ", " << x2 << ", " << y2);
    if (m_Mode.pf.nBpp == 16)
    {
//        updateBuffer_16bpp (pBuffer, x1, y1, x2, y2);
//        return;
    }
    else if (m_Mode.pf.nBpp == 24)
    {
//        updateBuffer_24bpp (pBuffer, x1, y1, x2, y2);
//       return;
    }
 
    Buffer *pBuf = m_Buffers.lookup(pBuffer);
    if (!pBuf)
    {
        ERROR("VbeDisplay: updateBuffer: Bad buffer:" << reinterpret_cast<uintptr_t>(pBuffer));
        return;
    }
   
    if (x1 == ~0UL) x1 = 0;
    if (x2 == ~0UL) x2 = m_Mode.width-1;
    if (y1 == ~0UL) y1 = 0;
    if (y2 == ~0UL) y2 = m_Mode.height-1;

    size_t bytesPerPixel = m_Mode.pf.nBpp/8;

    // Unoptimised version for arbitrary pixel formats.
    for (size_t y = y1; y <= y2; y++)
    {
        for (size_t x = x1; x <= x2; x++)
        {
            size_t i = y*m_Mode.width + x;
            packColour(pBuffer[i], i, reinterpret_cast<uintptr_t>(pBuf->pFbBackbuffer));
        }

        memcpy(reinterpret_cast<uint8_t*>(getFramebuffer())+y*m_Mode.pf.nPitch + x1*bytesPerPixel,
               pBuf->pFbBackbuffer + y*m_Mode.pf.nPitch + x1*bytesPerPixel,
               (x2-x1)*bytesPerPixel);
    }
}

void VbeDisplay::killBuffer(rgb_t *pBuffer)
{
    Buffer *pBuf = m_Buffers.lookup(pBuffer);
    if (!pBuf)
    {
        ERROR("VbeDisplay: killBuffer: Bad buffer:" << reinterpret_cast<uintptr_t>(pBuffer));
        return;
    }
    pBuf->mr.free();
    pBuf->fbmr.free();

    delete pBuf;
}

void VbeDisplay::bitBlit(rgb_t *pBuffer, size_t fromX, size_t fromY, size_t toX,
                         size_t toY, size_t width, size_t height)
{
    Buffer *pBuf = m_Buffers.lookup(pBuffer);
    if (!pBuf)
    {
        ERROR("VbeDisplay: bitBlit: Bad buffer:" << reinterpret_cast<uintptr_t>(pBuffer));
        return;
    }
    
    size_t bytesPerPixel = m_Mode.pf.nBpp/8;

    uint8_t *pFb = pBuf->pFbBackbuffer;

    // Just like memmove(), if the dest < src, copy forwards, else copy backwards.
    size_t min = 0;
    size_t max = height;
    ssize_t increment = 1;
    if (fromY < toY)
    {
        min = height;
        max = 0;
        increment = -1;
    }

    // Unoptimised bitblit. This could definately be made better.
    for (size_t y = min; y < max; y += increment)
    {
        memmove(&pBuffer[(y+toY)*m_Mode.width + toX],
                &pBuffer[(y+fromY)*m_Mode.width + fromX],
                width*sizeof(rgb_t));
        memmove(&pFb[((y+toY)*m_Mode.width + toX) * bytesPerPixel],
                &pFb[((y+fromY)*m_Mode.width + fromX) * bytesPerPixel],
                width*bytesPerPixel);
    }
}

void VbeDisplay::fillRectangle(rgb_t *pBuffer, size_t x, size_t y, size_t width, size_t height, rgb_t colour)
{
    Buffer *pBuf = m_Buffers.lookup(pBuffer);
    if (!pBuf)
    {
        ERROR("VbeDisplay: fillRect: Bad buffer:" << reinterpret_cast<uintptr_t>(pBuffer));        
        return;
    }
    
    uint8_t *pFb = reinterpret_cast<uint8_t*>(getFramebuffer());

    size_t bytesPerPixel = m_Mode.pf.nBpp/8;

    size_t compiledColour = 0;
    if (m_Mode.pf.nBpp == 15 || m_Mode.pf.nBpp == 16)
        // Bit of a dirty hack. Oh well.
        packColour(colour, 0, reinterpret_cast<uintptr_t>(&compiledColour));

    for (size_t i = y; i < y+height; i++)
    {
        for (size_t j = x; j < x+width; j++)
        {
            pBuffer[i*m_Mode.width + j] = colour;
            switch (m_Mode.pf.nBpp)
            {
                case 15:
                case 16:
                {
                    uint16_t *pFb16 = reinterpret_cast<uint16_t*> (pFb);
                    pFb16[i*m_Mode.width + j] = compiledColour&0xFFFF;
                    break;
                }
                case 24:
                    pFb[ (i*m_Mode.width + j) * 3 + 0] = colour.r;
                    pFb[ (i*m_Mode.width + j) * 3 + 1] = colour.g;
                    pFb[ (i*m_Mode.width + j) * 3 + 2] = colour.b;
                    break;
                default:
                    WARNING("VbeDisplay: Pixel format not handled in fillRectangle.");
            }
        }
    }
}

void VbeDisplay::packColour(rgb_t colour, size_t idx, uintptr_t pFb)
{
    PixelFormat pf = m_Mode.pf;

    uint8_t r = colour.r;
    uint8_t g = colour.g;
    uint8_t b = colour.b;

    // Calculate the range of the Red field.
    uint8_t range = 1 << pf.mRed;

    // Clamp the red value to this range.
    r = (r * range) / 256;

    range = 1 << pf.mGreen;

    // Clamp the green value to this range.
    g = (g * range) / 256;

    range = 1 << pf.mBlue;

    // Clamp the blue value to this range.
    b = (b * range) / 256;

    // Assemble the colour.
    uint32_t c =  0 |
        (static_cast<uint32_t>(r) << pf.pRed) |
        (static_cast<uint32_t>(g) << pf.pGreen) |
        (static_cast<uint32_t>(b) << pf.pBlue);

    switch (pf.nBpp)
    {
        case 15:
        case 16:
        {
            uint16_t *pFb16 = reinterpret_cast<uint16_t*>(pFb);
            pFb16[idx] = c;
            break;
        }
        case 24:
        {
            rgb_t *pFbRgb = reinterpret_cast<rgb_t*>(pFb);
            pFbRgb[idx].r = static_cast<uint32_t>(r);
            pFbRgb[idx].g = static_cast<uint32_t>(g);
            pFbRgb[idx].b = static_cast<uint32_t>(b);
            break;
        }
        case 32:
        {
            uint32_t *pFb32 = reinterpret_cast<uint32_t*>(pFb);
            pFb32[idx] = c;
            break;
        }
    }
}


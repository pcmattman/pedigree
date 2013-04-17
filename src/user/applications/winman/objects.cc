/*
 * Copyright (c) 2010 James Molloy, Matthew Iselin
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

#include "winman.h"

void WObject::reposition(size_t x, size_t y, size_t w, size_t h)
{
    if(x == ~0UL)
        x = m_Dimensions.getX();
    if(y == ~0UL)
        y = m_Dimensions.getY();
    if(w == ~0UL)
        w = m_Dimensions.getW();
    if(h == ~0UL)
        h = m_Dimensions.getH();
    //m_Dimensions.update(x, y, m_Dimensions.getW(), m_Dimensions.getH());

    syslog(LOG_INFO, "before reposition: %d, %d %dx%d", m_Dimensions.getX(), m_Dimensions.getY(), m_Dimensions.getW(), m_Dimensions.getH());
    syslog(LOG_INFO, "reposition: %d, %d %dx%d", x, y, w, h);
    m_Dimensions.update(x, y, w, h);
    syslog(LOG_INFO, "actual reposition: %d, %d %dx%d", m_Dimensions.getX(), m_Dimensions.getY(), m_Dimensions.getW(), m_Dimensions.getH());

    ssize_t horizDistance = w - static_cast<ssize_t>(m_Dimensions.getW());
    ssize_t vertDistance = h - static_cast<ssize_t>(m_Dimensions.getH());
    //resize(horizDistance, vertDistance, this);
}

void WObject::bump(ssize_t bumpX, ssize_t bumpY)
{
    size_t x = m_Dimensions.getX() + bumpX;
    size_t y = m_Dimensions.getY() + bumpY;
    size_t w = m_Dimensions.getW();
    size_t h = m_Dimensions.getH();
    m_Dimensions.update(x, y, w, h);
}

Window::Window(uint64_t handle, PedigreeIpc::IpcEndpoint *endpoint, ::Container *pParent, PedigreeGraphics::Framebuffer *pBaseFramebuffer) :
    m_Handle(handle), m_Endpoint(endpoint), m_pParent(pParent), m_pBaseFramebuffer(pBaseFramebuffer), m_bFocus(false)
{
    m_pParent->addChild(this);
}

void Window::render()
{
    PedigreeGraphics::Rect &me = getDimensions();
    size_t x = me.getX() + 5;
    size_t y = me.getY() + 5;
    size_t w = me.getW() - 10;
    size_t h = me.getH() - 10;
    //syslog(LOG_INFO, "winman: Window::render(%d, %d, %d, %d)", x, y, w, h);
    size_t r = 0;
    if(m_bFocus)
        r = 255;
    else if(m_pParent->getFocusWindow() == this)
        r = 255 / 2;

    // Window border.

    // Top
    m_pBaseFramebuffer->line(x - 1, y - 1, x + w + 1, y - 1, PedigreeGraphics::createRgb(255, 255, 255), PedigreeGraphics::Bits32_Rgb);

    // Left
    m_pBaseFramebuffer->line(x - 1, y - 1, x - 1, y + h + 1, PedigreeGraphics::createRgb(255, 255, 255), PedigreeGraphics::Bits32_Rgb);

    // Right
    m_pBaseFramebuffer->line(x + w + 1, y - 1, x + w + 1, y + h + 1, PedigreeGraphics::createRgb(255, 255, 255), PedigreeGraphics::Bits32_Rgb);

    // Bottom
    m_pBaseFramebuffer->line(x - 1, y + h + 1, x + w + 1, y + h + 1, PedigreeGraphics::createRgb(255, 255, 255), PedigreeGraphics::Bits32_Rgb);

    // Window.
    m_pBaseFramebuffer->rect(x, y, w, h, PedigreeGraphics::createRgb(r, 0, 0), PedigreeGraphics::Bits32_Rgb);
    // m_pBaseFramebuffer->redraw(x, y, w, h, true);
}

void Window::focus()
{
    m_bFocus = true;
    m_pParent->setFocusWindow(this);
}

void Window::nofocus()
{
    m_bFocus = false;
}

void Window::resize(ssize_t horizDistance, ssize_t vertDistance, WObject *pChild)
{
    syslog(LOG_INFO, "winman: window::resize w=%ld h=%ld", horizDistance, vertDistance);

    PedigreeGraphics::Rect &me = getDimensions();
    size_t currentWidth = me.getW();
    size_t currentHeight = me.getH();

    reposition(
        me.getX(), me.getY(),
        currentWidth + horizDistance, currentHeight + vertDistance);
}

void Container::retile()
{
    syslog(LOG_INFO, "winman: retile");

    if(m_Children.size() == 0)
    {
        syslog(LOG_INFO, "winman: no children in container, no retile");
        return;
    }

    PedigreeGraphics::Rect &me = getDimensions();
    size_t currentWidth = me.getW();
    size_t currentHeight = me.getH();

    if(m_Layout == SideBySide)
    {
        size_t dividedW = currentWidth / m_Children.size();
        size_t newX = me.getX();
        size_t newY = me.getY();

        WObjectList_t::iterator it = m_Children.begin();
        for(; it != m_Children.end(); ++it)
        {
            syslog(LOG_INFO, "tile now at x=%d, w=%d", newX, dividedW);
            (*it)->reposition(newX, newY, dividedW, currentHeight);
            newX += dividedW;
        }
    }
    else if(m_Layout == Stacked)
    {
        size_t dividedH = currentHeight / m_Children.size();
        size_t newX = me.getX();
        size_t newY = me.getY();

        WObjectList_t::iterator it = m_Children.begin();
        for(; it != m_Children.end(); ++it)
        {
            syslog(LOG_INFO, "tile now at y=%d, h=%d", newY, dividedH);
            (*it)->reposition(newX, newY, currentWidth, dividedH);
            newY += dividedH;
        }
    }

    // Fun logic to tile children goes here.
    // May need to call resized() on child windows.
}

void Container::resize(ssize_t horizDistance, ssize_t vertDistance, WObject *pChild)
{
    PedigreeGraphics::Rect &me = getDimensions();
    size_t currentWidth = me.getW();
    size_t currentHeight = me.getH();

    syslog(LOG_INFO, "winman: resize w=%d h=%d", horizDistance, vertDistance);

    if(pChild)
    {
        syslog(LOG_INFO, "winman: container resize with child");

        ssize_t bumpX = horizDistance;
        ssize_t bumpY = vertDistance;

        horizDistance = -horizDistance;
        vertDistance = -vertDistance;

        WObjectList_t::iterator it = m_Children.begin();
        bool bResize = false;
        for(; it != m_Children.end(); ++it)
        {
            if(bResize)
            {
                syslog(LOG_INFO, "resizing a client");
                (*it)->bump(bumpX, bumpY); // Bump X/Y co-ords.
                (*it)->resize(horizDistance, vertDistance);
                return; // Resize will cascade.
            }

            syslog(LOG_INFO, "%p vs %p", (*it), pChild);
            if((*it) == pChild)
            {
                syslog(LOG_INFO, "found child");
                bResize = true;
            }
        }

        if(!bResize)
            syslog(LOG_INFO, "winman: didn't find children for resize???");
    }
    else
    {
        reposition(
            me.getX(), me.getY(),
            currentWidth + horizDistance, currentHeight + vertDistance);

        if(m_Children.size())
        {
            // We need tiles to fill the space created by resizing.
            // For most resizes, we'll be going one direction, but if
            // we get resized vertically and horizontally, this handles
            // that nicely.
            // Is this right?
            ssize_t extendW = horizDistance / static_cast<ssize_t>(m_Children.size());
            ssize_t extendH = vertDistance / static_cast<ssize_t>(m_Children.size());

            syslog(LOG_INFO, "winman: extendW=%d extendH=%d", extendW, extendH);

            // Resize children in the relevant direction for them.
            WObjectList_t::iterator it = m_Children.begin();
            size_t bumpX = 0;
            for(; it != m_Children.end(); ++it)
            {
                if(m_Layout == Stacked)
                {
                    (*it)->resize(horizDistance, extendH);
                }
                else if(m_Layout == SideBySide)
                {
                    (*it)->bump(bumpX, 0);
                    (*it)->resize(extendW, vertDistance);

                    bumpX += extendW;
                }
            }
        }
    }

    // Resize siblings.
    if(m_pParent->getType() == WObject::Container ||
        m_pParent->getType() == WObject::Root)
    {
        syslog(LOG_INFO, "passing resize up to parent");
        Container *pContainer = static_cast<Container*>(m_pParent);

        pContainer->resize(horizDistance, vertDistance, static_cast<WObject*>(this));
    }
}

void Container::render()
{
    WObjectList_t::iterator it = m_Children.begin();
    for(; it != m_Children.end(); ++it)
    {
        if((*it)->getType() == WObject::Container)
        {
            Container *pContainer = static_cast<Container*>(*it);
            pContainer->render();
        }
        else if((*it)->getType() == WObject::Window)
        {
            ::Window *pWindow = static_cast< ::Window*>(*it);
            pWindow->render();
        }
    }
}

WObject *Container::getLeftSibling(const WObject *pChild) const
{
    WObjectList_t::const_iterator it = m_Children.begin();
    for(; it != m_Children.end(); ++it)
    {
        if((*it) == pChild)
        {
            if(it == m_Children.begin())
            {
                break;
            }

            --it;
            return (*it);
        }
    }

    return 0;
}

WObject *Container::getRightSibling(const WObject *pChild) const
{
    WObjectList_t::const_iterator it = m_Children.begin();
    for(; it != m_Children.end(); ++it)
    {
        if((*it) == pChild)
        {
            ++it;

            if(it == m_Children.end())
            {
                break;
            }

            return (*it);
        }
    }

    return 0;
}

WObject *Container::getLeftObject() const
{
    return getLeftSibling(this);
}

WObject *Container::getRightObject() const
{
    return getRightSibling(this);
}

WObject *Container::getLeft(const WObject *obj) const
{
    WObject *sibling = 0;
    if(m_Layout == SideBySide)
    {
        WObject *sibling = getLeftSibling(obj);
        if(sibling)
        {
            return sibling;
        }
    }

    // No sibling to the child. If we are inside a side-by-side layout,
    // this could be trivial.
    if(m_pParent && (m_pParent->getType() == WObject::Container))
    {
        const Container *pContainer = static_cast<const Container*>(m_pParent);
        if(pContainer->getLayout() == SideBySide)
        {
            sibling = getLeftObject();
            if(sibling)
            {
                return sibling;
            }
        }
    }

    // Root has no parent.
    if(getType() != WObject::Root)
    {
        const Container *pContainer = static_cast<const Container*>(m_pParent);
        return pContainer->getLeft(this);
    }

    return 0;
}

WObject *Container::getRight(const WObject *obj) const
{
    WObject *sibling = 0;
    if(m_Layout == SideBySide)
    {
        WObject *sibling = getRightSibling(obj);
        if(sibling)
        {
            return sibling;
        }
    }

    // No sibling to the child. If we are inside a side-by-side layout,
    // this could be trivial.
    if(m_pParent && (m_pParent->getType() == WObject::Container))
    {
        const Container *pContainer = static_cast<const Container*>(m_pParent);
        if(pContainer->getLayout() == SideBySide)
        {
            sibling = getRightObject();
            if(sibling)
            {
                return sibling;
            }
        }
    }

    // Root has no parent.
    if(getType() != WObject::Root)
    {
        const Container *pContainer = static_cast<const Container*>(m_pParent);
        return pContainer->getRight(this);
    }

    return 0;
}

WObject *Container::getUp(const WObject *obj) const
{
    WObject *sibling = 0;
    if(m_Layout == Stacked)
    {
        syslog(LOG_INFO, "finding left sibling of object");
        WObject *sibling = getLeftSibling(obj);
        if(sibling)
        {
            return sibling;
        }
    }

    // No sibling to the child. If we are inside a stacked layout,
    // this could be trivial.
    syslog(LOG_INFO, "parent: %p", m_pParent);
    if(m_pParent && (m_pParent->getType() == WObject::Container))
    {
        syslog(LOG_INFO, "winman: A trying parent %p", m_pParent);
        const Container *pContainer = static_cast<const Container*>(m_pParent);
        if(pContainer->getLayout() == Stacked)
        {
            sibling = getLeftObject();
            if(sibling)
            {
                return sibling;
            }
        }
    }

    // Root has no parent.
    if(getType() != WObject::Root)
    {
        syslog(LOG_INFO, "winman: B trying parent %p", m_pParent);
        const Container *pContainer = static_cast<const Container*>(m_pParent);
        return pContainer->getUp(this);
    }

    return 0;
}

WObject *Container::getDown(const WObject *obj) const
{
    WObject *sibling = 0;
    if(m_Layout == Stacked)
    {
        WObject *sibling = getRightSibling(obj);
        if(sibling)
        {
            return sibling;
        }
    }

    // No sibling to the child. If we are inside a stacked layout,
    // this could be trivial.
    if(m_pParent && (m_pParent->getType() == WObject::Container))
    {
        const Container *pContainer = static_cast<const Container*>(m_pParent);
        if(pContainer->getLayout() == Stacked)
        {
            sibling = getRightObject();
            if(sibling)
            {
                return sibling;
            }
        }
    }

    // Root has no parent.
    if(getType() != WObject::Root)
    {
        const Container *pContainer = static_cast<const Container*>(m_pParent);
        return pContainer->getDown(this);
    }

    return 0;
}

void RootContainer::resize(ssize_t horizDistance, ssize_t vertDistance, WObject *pChild)
{
    syslog(LOG_INFO, "RootContainer resize");
    if(pChild)
    {
        // Use the Container implementation to resize siblings of the
        // child that we've been passed.
        Container::resize(horizDistance, vertDistance, pChild);
    }
}

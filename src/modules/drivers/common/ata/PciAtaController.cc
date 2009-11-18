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

#include "PciAtaController.h"
#include <Log.h>
#include <machine/Machine.h>
#include <processor/Processor.h>

PciAtaController::PciAtaController(Controller *pDev, int nController) :
    AtaController(pDev, nController)
{
    NOTICE("PciAtaController::initialise");
}
PciAtaController::~PciAtaController()
{
}

uint64_t PciAtaController::executeRequest(uint64_t p1, uint64_t p2, uint64_t p3, uint64_t p4,
                                          uint64_t p5, uint64_t p6, uint64_t p7, uint64_t p8)
{
    NOTICE("PciAtaController::executeRequest");
    return 0;
}

bool PciAtaController::irq(irq_id_t number, InterruptState &state)
{
    NOTICE("PciAtaController::irq");
    return false;
}

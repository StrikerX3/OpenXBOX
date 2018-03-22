/*
 * Portions of the code are based on QEMU's USB API.
 * The original copyright header is included below.
 */
/*
 * QEMU USB API
 *
 * Copyright (c) 2005 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#pragma once

#include <cstdint>
#include <vector>

#include "desc.h"

namespace openxbox {

typedef struct BusChild {
    DeviceState *child;
    int index;
    QTAILQ_ENTRY(BusChild) sibling;
} BusChild;

struct USBBus {
    char *name;
    HotplugHandler *hotplug_handler;
    int max_index;
    bool realized;
    std::vector<BusChild> children;
    QLIST_ENTRY(BusState) sibling;

    int busnr;
    int nfree;
    int nused;
    std::vector<USBPort *> free;
    std::vector<USBPort *> used;
    QTAILQ_ENTRY(USBBus) next;
};

}

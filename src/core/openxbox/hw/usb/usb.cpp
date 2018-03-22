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
#include "usb.h"
#include "bus.h"

namespace openxbox {

void USBPacket::SetState(USBPacketState state) {
    this->state = state;
}

void USBPacket::Cancel() {
    bool callback = (state == USB_PACKET_ASYNC);
    //assert(usb_packet_is_inflight(p));
    SetState(USB_PACKET_CANCELED);
    ep->queue.pop();
    if (callback) {
        ep->dev->CancelPacket(this);
    }
}

USBDevice::~USBDevice() {
    Unrealize();
}

void USBDevice::Reset() {
    // FIXME: this == nullptr... ugh
    if (this == nullptr || !m_attached) {
        return;
    }
    m_remoteWakeup = 0;
    m_addr = 0;
    m_state = USB_STATE_DEFAULT;
    HandleReset();
}

void USBPort::Reset() {
    Detach();
    Attach();
    m_dev->Reset();
}

}

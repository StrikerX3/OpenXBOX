//
// Portions of the code are based on QEMU's USB API.
// The original copyright header is included below.
//
// QEMU USB API
//
// Copyright (c) 2005 Fabrice Bellard
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
#pragma once

#include <cstdint>
#include <vector>
#include <queue>

#include "desc.h"

namespace openxbox {

#define USB_MAX_ENDPOINTS  15
#define USB_MAX_INTERFACES 16

#define USB_STATE_NOTATTACHED 0
#define USB_STATE_ATTACHED    1
//#define USB_STATE_POWERED     2
#define USB_STATE_DEFAULT     3
//#define USB_STATE_ADDRESS     4
//#define	USB_STATE_CONFIGURED  5
#define USB_STATE_SUSPENDED   6

class USBEndpoint;
class USBDevice;
class USBPort;
class USBPacket;

struct USBDescString {
    uint8_t index;
    char *str;
};

struct USBEndpoint {
    uint8_t nr;
    uint8_t pid;
    uint8_t type;
    uint8_t ifnum;
    int maxPacketSize;
    int maxStreams;
    bool pipeline;
    bool halted;
    USBDevice *dev;
    std::queue<USBPacket> queue;
};

class USBDevice {
public:
    virtual ~USBDevice();

    virtual void Realize() = 0; // TODO: error handling
    virtual void Unrealize() = 0; // TODO: error handling

    // Walk (enabled) downstream ports, check for a matching device.
    // Only hubs implement this.
    virtual USBDevice *FindDevice(uint8_t addr) = 0;
    
    // Called when a packet is canceled.
    virtual void CancelPacket(USBPacket *p) = 0;

    // Attach the device
    virtual void HandleAttach() = 0;

    // Reset the device
    virtual void HandleReset() = 0;

    //
    // Process control request.
    // Called from handle_packet().
    //
    // Status gets stored in p->status, and if p->status == USB_RET_SUCCESS
    // then the number of bytes transferred is stored in p->actual_length
    virtual void HandleControl(USBPacket *p, int request, int value, int index, int length, uint8_t *data) = 0;

    // Process data transfers (both BULK and ISOC).
    // Called from handle_packet().
    //
    // Status gets stored in p->status, and if p->status == USB_RET_SUCCESS
    // then the number of bytes transferred is stored in p->actual_length
    virtual void HandleData(USBPacket *p) = 0;

    virtual void SetInterface(int interface, int alt_old, int alt_new) = 0;

    // Called when the hcd is done queuing packets for an endpoint, only
    // necessary for devices which can return USB_RET_ADD_TO_QUEUE.
    virtual void FlushEndpointQueue(USBEndpoint *ep) = 0;

    // Called by the hcd to let the device know the queue for an endpoint
    // has been unlinked / stopped. Optional may be NULL.
    virtual void EndpointStopped(USBEndpoint *ep) = 0;

    // Called by the hcd to alloc / free streams on a bulk endpoint.
    // Optional may be NULL.
    virtual int AllocStreams(USBEndpoint **eps, int nr_eps, int streams) = 0;
    virtual void FreeStreams(USBEndpoint **eps, int nr_eps) = 0;

    void Reset();

    inline bool IsAttached() { return m_attached; }

    inline USBEndpoint *GetControlEndpoint() { return &m_epCtl; }
    inline USBEndpoint *GetInEndpoint(uint8_t index) { return &m_epIn[index]; }
    inline USBEndpoint *GetOutEndpoint(uint8_t index) { return &m_epOut[index]; }

protected:
    USBPort *m_port;
    char *m_portPath;
    char *m_serial;
    void *m_opaque;
    uint32_t m_flags;

    // Actual connected speed
    int m_speed;
    // Supported speeds, not in info because it may be variable (hostdevs)
    int m_speedmask;
    uint8_t m_addr;
    char m_productDesc[32];
    int m_autoAttach;
    bool m_attached;

    int32_t m_state;
    uint8_t m_setupBuf[8];
    uint8_t m_dataBuf[4096];
    int32_t m_remoteWakeup;
    int32_t m_setupState;
    int32_t m_setupLen;
    int32_t m_setupIndex;

    USBEndpoint m_epCtl;
    USBEndpoint m_epIn[USB_MAX_ENDPOINTS];
    USBEndpoint m_epOut[USB_MAX_ENDPOINTS];

    std::vector<USBDescString> m_strings;
    const USBDesc *m_usbDesc; // Overrides class usb_desc if not NULL
    const USBDescDevice *m_device;

    int m_configuration;
    int m_ninterfaces;
    int m_altsetting[USB_MAX_INTERFACES];
    const USBDescConfig *m_config;
    const USBDescIface  *m_ifaces[USB_MAX_INTERFACES];
};

class USBPort {
public:
    virtual ~USBPort() = 0;
    virtual void Attach() = 0;
    virtual void Detach() = 0;

    //
    // This gets called when a device downstream from the device attached to
    // the port (iow attached through a hub) gets detached.
    virtual void ChildDetach(USBDevice *child) = 0;
    virtual void Wakeup() = 0;
    
    //
    // Note that port->dev will be different then the device from which
    // the packet originated when a hub is involved.
    virtual void Complete(USBPacket *p) = 0;

    void Reset();

    inline USBDevice *GetDevice() { return m_dev; }

protected:
    USBDevice *m_dev;
    int m_speedmask;
    int m_hubcount;
    char m_path[16];
    int index;
};

typedef enum USBPacketState {
    USB_PACKET_UNDEFINED = 0,
    USB_PACKET_SETUP,
    USB_PACKET_QUEUED,
    USB_PACKET_ASYNC,
    USB_PACKET_COMPLETE,
    USB_PACKET_CANCELED,
} USBPacketState;

struct USBCombinedPacket {
    USBPacket *first;
    std::vector<USBPacket *> packets;
    QEMUIOVector iov;
};

struct USBPacket {
    // Data fields for use by the driver.
    int pid;
    uint64_t id;
    USBEndpoint *ep;
    unsigned int stream;
    QEMUIOVector iov;
    uint64_t parameter; // control transfers
    bool short_not_ok;
    bool int_req;
    int status; // USB_RET_* status code
    int actual_length; // Number of bytes actually transferred
    // Internal use by the USB layer.
    USBPacketState state;
    USBCombinedPacket *combined;
    
    void SetState(USBPacketState expected);
    void Cancel();
};

}

/*
 * Portions of the code are based on Cxbx-Reloaded's OHCI LLE implementation.
 * The original copyright header is included below.
 */
// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;;
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   Cxbx->devices->USBController->USBDevice.h
// *
// *  This file is part of the Cxbx project.
// *
// *  Cxbx and Cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file COPYING.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2018 ergo720
// *
// *  All rights reserved
// *
// ******************************************************************

#pragma once

#include <cstdint>

#include "../defs.h"
#include "pci.h"
#include "openxbox/queue.h"
#include "openxbox/iovec.h"

namespace openxbox {

#define USB_MAX_ENDPOINTS  15
#define USB_MAX_INTERFACES 16

#define USB_STATE_NOTATTACHED 0
#define USB_STATE_ATTACHED    1
#define USB_STATE_DEFAULT     2

typedef enum USBPacketState {
    USB_PACKET_UNDEFINED = 0,
    USB_PACKET_SETUP,
    USB_PACKET_QUEUED,
    USB_PACKET_ASYNC,
    USB_PACKET_COMPLETE,
    USB_PACKET_CANCELED,
}
USBPacketState;

typedef enum USBDeviceFlags {
    USB_DEV_FLAG_FULL_PATH,
    USB_DEV_FLAG_IS_HOST,
    USB_DEV_FLAG_MSOS_DESC_ENABLE,
    USB_DEV_FLAG_MSOS_DESC_IN_USE,
}
USBDeviceFlags;

typedef struct _USBPacket USBPacket;
typedef struct _XboxDevice XboxDevice;

/* USB endpoint */
typedef struct _USBEndpoint {
    uint8_t Num;                      // endpoint number
    uint8_t pid;
    uint8_t Type;                     // the type of this endpoint
    uint8_t ifnum;
    int max_packet_size;
    bool Pipeline;
    bool Halted;                      // indicates that the endpoint is halted
    XboxDevice* Dev;                  // device this endpoint belongs to
    QTAILQ_HEAD(, _USBPacket) Queue;  // queue of packets to this endpoint
}
USBEndpoint;

/* definition of an Xbox usb device */
typedef struct _XboxDevice {
    DeviceState qdev;
    USBPort *port;
    char *port_path;
    char *serial;
    void *opaque;
    uint32_t flags;

    // Actual connected speed
    int speed;
    // Supported speeds, not in info because it may be variable (hostdevs)
    int speedmask;
    uint8_t Addr;                          // device function address
    char product_desc[32];
    int auto_attach;
    int Attached;                          // device is attached

    int32_t State;                         // current state of device
    uint8_t SetupBuffer[8];                // holds the IoVec structs copied (control transfers only?)
    uint8_t data_buf[4096];
    int32_t RemoteWakeup;                  // wakeup flag
    int32_t SetupState;                    // result of a setup tken processing operation
    int32_t SetupLength;                   // number of bytes to transfer as specified by a setup token
    int32_t SetupIndex;                    // index of the parameter in a setup token?

    USBEndpoint EP_ctl;                    // endpoints for SETUP tokens
    USBEndpoint EP_in[USB_MAX_ENDPOINTS];  // endpoints for OUT tokens
    USBEndpoint EP_out[USB_MAX_ENDPOINTS]; // endpoints for IN tokens

    //QLIST_HEAD(, USBDescString) strings;
    const USBDesc *usb_desc;               // Overrides class usb_desc if not NULL
    const USBDescDevice *device;

    int configuration;
    int ninterfaces;
    int altsetting[USB_MAX_INTERFACES];
    const USBDescConfig *config;
    const USBDescIface  *ifaces[USB_MAX_INTERFACES];
}
XboxDevice;

// ergo720: this could be merged in the OHCI or USBDevice class if possible
typedef struct USBDeviceClass {
    DeviceClass parent_class;

    int(*init)(USBDev *dev);

    // Walk (enabled) downstream ports, check for a matching device.
    // Only hubs implement this.
    USBDev *(*find_device)(USBDev *dev, uint8_t addr);

    // Called when a packet is canceled.
    void(*cancel_packet)(USBDev *dev, USBPacket *p);

    // Called when device is destroyed.
    void(*handle_destroy)(USBDev *dev);

    // Attach the device
    void(*handle_attach)(USBDev *dev);

    // Reset the device
    void(*handle_reset)(USBDev *dev);

    // Process control request.
    // Called from handle_packet().
    // Status gets stored in p->status, and if p->status == USB_RET_SUCCESS
    // then the number of bytes transferred is stored in p->actual_length
    void(*handle_control)(USBDev *dev, USBPacket *p, int request, int value,
        int index, int length, uint8_t *data);

    // Process data transfers (both BULK and ISOC).
    // Called from handle_packet().
    // Status gets stored in p->status, and if p->status == USB_RET_SUCCESS
    // then the number of bytes transferred is stored in p->actual_length
    void(*handle_data)(USBDev *dev, USBPacket *p);

    void(*set_interface)(USBDev *dev, int Interface,
        int alt_old, int alt_new);

    // Called when the hcd is done queuing packets for an endpoint, only
    // necessary for devices which can return USB_RET_ADD_TO_QUEUE.
    void(*flush_ep_queue)(USBDev *dev, USBEndpoint *ep);

    // Called by the hcd to let the device know the queue for an endpoint
    // has been unlinked / stopped. Optional may be NULL.
    void(*EP_Stopped)(USBDev* Dev, USBEndpoint* EP);

    const char *product_desc;
    const USBDesc *usb_desc;
}
USBDeviceClass;

typedef struct _USBCombinedPacket {
    _USBPacket* First;
    QTAILQ_HEAD(packets_head, _USBPacket) Packets;
    IOVector IoVec;
}
USBCombinedPacket;

/* Structure used to hold information about an active USB packet */
struct _USBPacket {
    int Pid;                                 // Packet ID (used to identify the type of packet that is being sent)
    uint32_t Id;                             // Paddr of the TD for this packet 
    USBEndpoint* Endpoint;                   // endpoint this packet is transferred to
    unsigned int Stream;
    IOVector IoVec;                          // used to perform vectored I/O
    uint64_t Parameter;                      // control transfers
    bool ShortNotOK;                         // the bufferRounding mode of the TD for this packet
    bool IntReq;                             // whether or not to generate an interrupt for this packet (DelayInterrupt of the TD is zero)
    int Status;                              // USB_RET_* status code
    int ActualLength;                        // before copy: offset inside IoVec structs; after copy: number of bytes actually transferred
                                             // Internal use by the USB layer
    USBPacketState State;
    USBCombinedPacket* Combined;
    QTAILQ_ENTRY(_USBPacket) Queue;
    QTAILQ_ENTRY(_USBPacket) CombinedEntry;
};

/* Struct describing the status of a usb port */
typedef struct _USBPort {
    XboxDevice* Dev;              // usb device (if present)
    int SpeedMask;                // usb speeds supported
    int HubCount;                 // number of hubs attached
    char Path[16];                // the number of the port
    int PortIndex;                // internal port index
    QTAILQ_ENTRY(_USBPort) Next;
}
USBPort;

// Forward declare OHCI class for USBDevice class
class OHCI;

class USBPCIDevice : public PCIDevice {
public:
    // constructor
    USBPCIDevice(uint16_t vendorID, uint16_t deviceID, uint8_t revisionID, uint8_t m_irqn);
    virtual ~USBPCIDevice();

    // PCI Device functions
    void Init();
    void Reset();

    void PCIMMIORead(int barIndex, uint32_t addr, uint32_t *value, uint8_t size) override;
    void PCIMMIOWrite(int barIndex, uint32_t addr, uint32_t value, uint8_t size) override;



    // USBDevice-specific functions/variables
    // pointer to the host controller this device refers to
    OHCI* m_HostController = nullptr;

    // register a port with the HC
    void USB_RegisterPort(USBPort* Port, int Index, int SpeedMask);
    //
    void USB_DeviceEPstopped(XboxDevice* Dev, USBEndpoint* Ep);
    // reset a usb port
    void USB_PortReset(USBPort* Port);
    // a device is attched
    void USB_Attach(USBPort* Port);
    // a device is detached
    void USB_Detach(USBPort* Port);
    // a device downstream from the device attached to the port (attached through a hub) is detached
    void ChildDetach(USBPort* Port, XboxDevice* Child);
    // TODO
    void Wakeup(USBPort* Port);
    // TODO
    void Complete(USBPort* Port, USBPacket *P);
    // reset a device
    void USB_DeviceReset(XboxDevice* Dev);
    // find the usb device with the supplied address
    XboxDevice* USB_FindDevice(USBPort* Port, uint8_t Addr);
    // ergo720: can probably be removed by calling directly usb_hub_find_device
    XboxDevice* USB_DeviceFindDevice(XboxDevice* Dev, uint8_t Addr);
    // find the requested endpoint in the supplied device
    USBEndpoint* USB_GetEP(XboxDevice* Dev, int Pid, int Ep);
    // setup a packet for transfer
    void USB_PacketSetup(USBPacket* p, int Pid, USBEndpoint* Ep, unsigned int Stream,
        uint64_t Id, bool ShortNotOK, bool IntReq);
    // check if the state of the packet is queued or async
    bool USB_IsPacketInflight(USBPacket* p);
    // append the user buffer to the packet
    void USB_PacketAddBuffer(USBPacket* p, void* ptr, size_t len);
    // transfer and process the packet
    void USB_HandlePacket(XboxDevice* dev, USBPacket* p);
    // check if the packet has the expected state and assert if not
    void USB_PacketCheckState(USBPacket* p, USBPacketState expected);
    // process the packet
    void USB_ProcessOne(USBPacket* p);
    //
    void USB_DoParameter(XboxDevice* s, USBPacket* p);
    // process a setup token
    void USB_DoTokenSetup(XboxDevice* s, USBPacket* p);
    // process an input token
    void DoTokenIn(XboxDevice* s, USBPacket* p);
    // process an output token
    void DoTokenOut(XboxDevice* s, USBPacket* p);
    // copy the packet data to the buffer pointed to by ptr
    void USB_PacketCopy(USBPacket* p, void* ptr, size_t bytes);
    // queue a packet to an endpoint
    void USB_QueueOne(USBPacket* p);
    //
    void USB_DeviceHandleControl(XboxDevice* dev, USBPacket* p, int request, int value, int index, int length, uint8_t* data);
    //
    void USB_DeviceHandleData(XboxDevice* dev, USBPacket* p);
    //
    void USB_DeviceFlushEPqueue(XboxDevice* dev, USBEndpoint* ep);
    //
    void USB_DeviceCancelPacket(XboxDevice* dev, USBPacket* p);
    // Cancel an active packet.  The packed must have been deferred by
    // returning USB_RET_ASYNC from handle_packet, and not yet completed
    void USB_CancelPacket(USBPacket* p);
private:
    uint8_t m_irqn;
};

}

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
// *   Cxbx->devices->usb->Hub.cpp
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

#include <cassert>

#include "openxbox/log.h"

#include "ohci.h"
#include "ohci_hub.h"

namespace openxbox {

// This array is used to translate an xbox player to the corresponding usb port
// The port associations are taken from XQEMU
int PlayerToUsbArray[] = {
    -1,
    3,
    4,
    1,
    2,
};

USBDescIface::USBDescIface(bool bDefault) {
    std::memset(this, 0, sizeof(USBDescIface));
    if (bDefault) {
        descs = new USBDescOther();
        eps = new USBDescEndpoint();
        bInterfaceNumber = 0;
        bNumEndpoints = 1;
        bInterfaceClass = USB_CLASS_HUB;
        eps = new USBDescEndpoint();
        eps->bEndpointAddress = USB_DIR_IN | 0x01;
        eps->bmAttributes = USB_ENDPOINT_XFER_INT;
        eps->wMaxPacketSize = 1 + (NUM_PORTS + 7) / 8;
        eps->bInterval = 0xFF;
    }
}

USBDescIface::~USBDescIface() {
    delete descs;
    if (bNumEndpoints != 1) {
        delete[] eps;
        return;
    }
    delete eps;
}

static const USBDescIface desc_iface_hub(true);

USBDescDevice::USBDescDevice(bool bDefault) {
    std::memset(this, 0, sizeof(USBDescDevice));
    if (bDefault) {
        USBDescConfig* pUSBDescConfig = new USBDescConfig();
        bcdUSB = 0x0110;
        bDeviceClass = USB_CLASS_HUB;
        bMaxPacketSize0 = 8;
        bNumConfigurations = 1;
        pUSBDescConfig->bNumInterfaces = 1;
        pUSBDescConfig->bConfigurationValue = 1;
        pUSBDescConfig->bmAttributes = 0xE0;
        pUSBDescConfig->nif = 1;
        pUSBDescConfig->ifs = &desc_iface_hub;
        confs = pUSBDescConfig;
    }
}

USBDescDevice::~USBDescDevice() {
    delete confs;
    if (bNumConfigurations != 1) {
        delete[] confs;
        return;
    }
}

static const USBDescDevice desc_device_hub(true);

USBDesc::USBDesc(bool bDefault) {
    std::memset(this, 0, sizeof(USBDesc));
    if (bDefault) {
        id.idVendor = 0x0409;
        id.idProduct = 0x55AA;
        id.bcdDevice = 0x0101;
        id.iManufacturer = STR_MANUFACTURER;
        id.iProduct = STR_PRODUCT;
        id.iSerialNumber = STR_SERIALNUMBER;
        full = &desc_device_hub;
    }
}

static const USBDesc desc_hub(true);

int Hub::Init(int pport) {
    XboxDeviceState* dev = ClassInitFn();
    m_UsbDev->USB_EpInit(dev);
    int rc = UsbHubClaimPort(dev, pport);
    if (rc != 0) {
        return rc;
    }
    rc = m_UsbDev->USB_DeviceInit(dev);
    if (rc != 0) {
        UsbHubReleasePort(dev);
        return rc;
    }
    m_UsbDev->USB_DeviceAttach(dev);
    return 0;
}

Hub::~Hub() {
    delete m_pPeripheralFuncStruct;
    delete m_HubState->ports[0].port.Operations;
    delete m_HubState;
    m_pPeripheralFuncStruct = nullptr;
    m_HubState = nullptr;
}

XboxDeviceState* Hub::ClassInitFn() {
    m_pPeripheralFuncStruct = new USBDeviceClass();
    m_HubState = new USBHubState();
    XboxDeviceState* dev = &m_HubState->dev;

    dev->ProductDesc = "Cxbx-Reloaded USB Hub";
    QLIST_INIT(&dev->Strings);
    dev->klass = m_pPeripheralFuncStruct;

    {
        using namespace std::placeholders;

        m_pPeripheralFuncStruct->init = std::bind(&Hub::UsbHub_Initfn, this, _1);
        m_pPeripheralFuncStruct->find_device = std::bind(&Hub::UsbHub_FindDevice, this, _1, _2);
        m_pPeripheralFuncStruct->handle_reset = std::bind(&Hub::UsbHub_HandleReset, this);
        m_pPeripheralFuncStruct->handle_control = std::bind(&Hub::UsbHub_HandleControl, this, _1, _2, _3, _4, _5, _6, _7);
        m_pPeripheralFuncStruct->handle_data = std::bind(&Hub::UsbHub_HandleData, this, _1, _2);
        m_pPeripheralFuncStruct->handle_destroy = std::bind(&Hub::UsbHub_HandleDestroy, this, _1);
        m_pPeripheralFuncStruct->product_desc = dev->ProductDesc.c_str();
        m_pPeripheralFuncStruct->usb_desc = &desc_hub;
    }

    return dev;
}

int Hub::UsbHubClaimPort(XboxDeviceState* dev, int pport) {
    int usb_port;

    assert(dev->Port == nullptr);

    if (pport > 4 || pport < 1) { return -1; };

    usb_port = PlayerToUsbArray[pport];
    if (usb_port > 2) {
        m_UsbDev = g_USB0; // FIXME: how to retrieve these?
        m_UsbDev->m_HostController->OHCI_AssignUsbPortStruct(usb_port - 3, dev);
    }
    else {
        m_UsbDev = g_USB1; // FIXME: how to retrieve these?
        m_UsbDev->m_HostController->OHCI_AssignUsbPortStruct(usb_port - 1, dev);
    }

    return 0;
}

void Hub::UsbHubReleasePort(XboxDeviceState* dev) {
    USBPort * port = dev->Port;

    assert(port != nullptr);

    port->Dev = nullptr;
    dev->Port = nullptr;
}

int Hub::UsbHub_Initfn(XboxDeviceState* dev) {
    USBHubPort* port;
    USBPortOps* ops;
    int i;

    if (dev->Port->HubCount == 5) {
        log_debug("Hub: chain too deep");
        return -1;
    }

    m_UsbDev->USB_CreateSerial(dev, "314159");
    m_UsbDev->USBDesc_SetString(dev, STR_MANUFACTURER, "Cxbx-Reloaded");
    m_UsbDev->USBDesc_SetString(dev, STR_PRODUCT, "Cxbx-Reloaded USB Hub");
    m_UsbDev->USBDesc_Init(dev);
    m_HubState->intr = m_UsbDev->USB_GetEP(dev, USB_TOKEN_IN, 1);

    ops = new USBPortOps();
    {
        using namespace std::placeholders;

        ops->attach = std::bind(&Hub::UsbHub_Attach, this, _1);
        ops->detach = std::bind(&Hub::UsbHub_Detach, this, _1);
        ops->child_detach = std::bind(&Hub::UsbHub_ChildDetach, this, _1);
        ops->wakeup = std::bind(&Hub::UsbHub_Wakeup, this, _1);
        ops->complete = std::bind(&Hub::UsbHub_Complete, this, _1, _2);
    }

    for (i = 0; i < NUM_PORTS; i++) {
        port = &m_HubState->ports[i];
        m_UsbDev->USB_RegisterPort(&port->port, i, USB_SPEED_MASK_LOW | USB_SPEED_MASK_FULL, ops);
        m_UsbDev->USB_PortLocation(&port->port, dev->Port, i + 1);
    }
    UsbHub_HandleReset();
    return 0;
}

XboxDeviceState* Hub::UsbHub_FindDevice(XboxDeviceState* dev, uint8_t addr) {
    USBHubPort * port;
    XboxDeviceState * downstream;

    for (int i = 0; i < NUM_PORTS; i++) {
        port = &m_HubState->ports[i];
        if (!(port->wPortStatus & PORT_STAT_ENABLE)) {
            continue;
        }
        downstream = m_UsbDev->USB_FindDevice(&port->port, addr);
        if (downstream != nullptr) {
            return downstream;
        }
    }
    return nullptr;
}

void Hub::UsbHub_HandleReset() {
    USBHubPort* port;

    for (int i = 0; i < NUM_PORTS; i++) {
        port = m_HubState->ports + i;
        port->wPortStatus = PORT_STAT_POWER;
        port->wPortChange = 0;
        if (port->port.Dev && port->port.Dev->Attached) {
            port->wPortStatus |= PORT_STAT_CONNECTION;
            port->wPortChange |= PORT_STAT_C_CONNECTION;
            if (port->port.Dev->Speed == USB_SPEED_LOW) {
                port->wPortStatus |= PORT_STAT_LOW_SPEED;
            }
        }
    }
}
void Hub::UsbHub_HandleControl(XboxDeviceState* dev, USBPacket* p, int request, int value, int index, int length, uint8_t* data) {
    USBHubState * s = (USBHubState *)dev;
    int ret;
    ret = m_UsbDev->USBDesc_HandleControl(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
    case EndpointOutRequest | USB_REQ_CLEAR_FEATURE:
        if (value == 0 && index != 0x81) { /* clear ep halt */
            goto fail;
        }
        break;
        /* usb specific requests */
    case GetHubStatus:
        data[0] = 0;
        data[1] = 0;
        data[2] = 0;
        data[3] = 0;
        p->actual_length = 4;
        break;
    case GetPortStatus:
    {
        unsigned int n = index - 1;
        USBHubPort * port;
        if (n >= NUM_PORTS) {
            goto fail;
        }
        port = &s->ports[n];
        trace_usb_hub_get_port_status(s->dev.addr, index,
            port->wPortStatus,
            port->wPortChange);
        data[0] = port->wPortStatus;
        data[1] = port->wPortStatus >> 8;
        data[2] = port->wPortChange;
        data[3] = port->wPortChange >> 8;
        p->actual_length = 4;
    }
    break;
    case SetHubFeature:
    case ClearHubFeature:
        if (value != 0 && value != 1) {
            goto fail;
        }
        break;
    case SetPortFeature:
    {
        unsigned int n = index - 1;
        USBHubPort * port;
        USBDevice * dev;
        trace_usb_hub_set_port_feature(s->dev.addr, index, feature_name(value));
        if (n >= NUM_PORTS) {
            goto fail;
        }
        port = &s->ports[n];
        dev = port->port.dev;
        switch (value) {
        case PORT_SUSPEND:
            port->wPortStatus |= PORT_STAT_SUSPEND;
            break;
        case PORT_RESET:
            if (dev && dev->attached) {
                usb_device_reset(dev);
                port->wPortChange |= PORT_STAT_C_RESET;
                /* set enable bit */
                port->wPortStatus |= PORT_STAT_ENABLE;
                usb_wakeup(s->intr, 0);
            }
            break;
        case PORT_POWER:
            break;
        default:
            goto fail;
        }
    }
    break;
    case GetHubDescriptor:
    {
        unsigned int n, limit, var_hub_size = 0;
        memcpy(data, qemu_hub_hub_descriptor,
            sizeof(qemu_hub_hub_descriptor));
        data[2] = NUM_PORTS;
        /* fill DeviceRemovable bits */
        limit = ((NUM_PORTS + 1 + 7) / 8) + 7;
        for (n = 7; n < limit; n++) {
            data[n] = 0x00;
            var_hub_size++;
        }
        /* fill PortPwrCtrlMask bits */
        limit = limit + ((NUM_PORTS + 7) / 8);
        for (; n < limit; n++) {
            data[n] = 0xff;
            var_hub_size++;
        }	p->actual_length = sizeof(qemu_hub_hub_descriptor) + var_hub_size;
        data[0] = p->actual_length;
        break;
    }
    default:
    fail:
        p->status = USB_RET_STALL;
        break;
    }
}

void Hub::UsbHub_Attach(USBPort* port1) {
    USBHubPort* port = &m_HubState->ports[port1->PortIndex];

    port->wPortStatus |= PORT_STAT_CONNECTION;
    port->wPortChange |= PORT_STAT_C_CONNECTION;
    if (port->port.Dev->Speed == USB_SPEED_LOW) {
        port->wPortStatus |= PORT_STAT_LOW_SPEED;
    }
    else {
        port->wPortStatus &= ~PORT_STAT_LOW_SPEED;
    }
    m_UsbDev->USB_Wakeup(m_HubState->intr);
}

void Hub::UsbHub_Detach(USBPort* port1) {
    USBHubPort* port = &m_HubState->ports[port1->PortIndex];

    m_UsbDev->USB_Wakeup(m_HubState->intr);

    // Let upstream know the device on this port is gone
    m_HubState->dev.Port->Operations->child_detach(port1->Dev);

    port->wPortStatus &= ~PORT_STAT_CONNECTION;
    port->wPortChange |= PORT_STAT_C_CONNECTION;
    if (port->wPortStatus & PORT_STAT_ENABLE) {
        port->wPortStatus &= ~PORT_STAT_ENABLE;
        port->wPortChange |= PORT_STAT_C_ENABLE;
    }
    m_UsbDev->USB_Wakeup(m_HubState->intr);
}

void Hub::UsbHub_ChildDetach(XboxDeviceState* child) {
    // Pass along to upstream
    m_HubState->dev.Port->Operations->child_detach(child);
}

void Hub::UsbHub_Wakeup(USBPort* port1) {
    USBHubPort* port = &m_HubState->ports[port1->PortIndex];

    if (port->wPortStatus & PORT_STAT_SUSPEND) {
        port->wPortChange |= PORT_STAT_C_SUSPEND;
        m_UsbDev->USB_Wakeup(m_HubState->intr);
    }
}

void Hub::UsbHub_Complete(USBPort* port, USBPacket* packet) {
    // Just pass it along to upstream
    m_HubState->dev.Port->Operations->complete(m_HubState->dev.Port, packet);
}

}

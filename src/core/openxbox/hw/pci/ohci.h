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
// *   Cxbx->devices->USBController->OHCI.h
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

#include "usb_pci.h"
#include "openxbox/cxbxtimer.h"
#include "../basic/irq.h"

namespace openxbox {

#define USB_TOKEN_SETUP 0x2D
#define USB_TOKEN_IN    0x69 // device -> host
#define USB_TOKEN_OUT   0xE1 // host -> device

#define USB_RET_SUCCESS           (0)
#define USB_RET_NODEV             (-1)
#define USB_RET_NAK               (-2)
#define USB_RET_STALL             (-3)
#define USB_RET_BABBLE            (-4)
#define USB_RET_IOERROR           (-5)
#define USB_RET_ASYNC             (-6)
#define USB_RET_ADD_TO_QUEUE      (-7)
#define USB_RET_REMOVE_FROM_QUEUE (-8)


// Abbreviations used:
// OHCI: Open Host Controller Interface; the standard used on the Xbox to comunicate with the usb devices
// HC: Host Controller; the hardware which interfaces with the usb device and the usb driver
// HCD: Host Controller Driver; software which talks to the HC, it's linked in the xbe
// SOF: start of frame; the beginning of a USB-defined frame
// EOF: end of frame; the end of a USB-defined frame
// ED: endpoint descriptor; a memory structure used by the HC to communicate with an endpoint
// TD: transfer descriptor; a memory structure used by the HC to transfer a block of data to/from a device endpoint


/* endpoint descriptor */
typedef struct _OHCI_ED {
    uint32_t Flags;
    uint32_t TailP;
    uint32_t HeadP;
    uint32_t NextED;
}
OHCI_ED;

/* general transfer descriptor */
typedef struct _OHCI_TD {
    uint32_t Flags;
    uint32_t CurrentBufferPointer;
    uint32_t NextTD;
    uint32_t BufferEnd;
}
OHCI_TD;

/* enum indicating the current HC state */
typedef enum _OHCI_State {
    Reset = 0x00,
    Resume = 0x40,
    Operational = 0x80,
    Suspend = 0xC0,
}
OHCI_State;

/* Host Controller Communications Area */
typedef struct _OHCI_HCCA {
    uint32_t HccaInterrruptTable[32];
    uint16_t HccaFrameNumber, HccaPad1;
    uint32_t HccaDoneHead;
}
OHCI_HCCA;

/* Small struct used to hold the HcRhPortStatus register and the usb port status */
typedef struct _OHCIPort {
    USBPort UsbPort;
    uint32_t HcRhPortStatus;
}
OHCIPort;

/* All these registers are well documented in the OHCI standard */
typedef struct _OHCI_Registers {
    // Control and Status partition
    uint32_t HcRevision;
    uint32_t HcControl;
    uint32_t HcCommandStatus;
    uint32_t HcInterruptStatus;
    // HcInterruptEnable/Disable are the same so we can merge them together
    uint32_t HcInterrupt;

    // Memory Pointer partition
    uint32_t HcHCCA;
    uint32_t HcPeriodCurrentED;
    uint32_t HcControlHeadED;
    uint32_t HcControlCurrentED;
    uint32_t HcBulkHeadED;
    uint32_t HcBulkCurrentED;
    uint32_t HcDoneHead;

    // Frame Counter partition
    uint32_t HcFmInterval;
    uint32_t HcFmRemaining;
    uint32_t HcFmNumber;
    uint32_t HcPeriodicStart;
    uint32_t HcLSThreshold;

    // Root Hub partition
    uint32_t HcRhDescriptorA;
    uint32_t HcRhDescriptorB;
    uint32_t HcRhStatus;
    // ergo720: I have some doubts here. Both XQEMU and OpenXbox set 4 ports per HC, for a total of 8 usb ports.
    // Could it be becasue each gamepad can host 2 memory units?
    OHCIPort RhPort[2]; // 2 ports per HC, for a total of 4 USB ports
}
OHCI_Registers;


/* OHCI class representing the state of the HC */
class OHCI {
public:
    // constructor
    OHCI(int Irqn, USBPCIDevice* UsbObj);
    // destructor
    ~OHCI() {}
    // read a register
    uint32_t OHCI_ReadRegister(uint32_t Addr);
    // write a register
    void OHCI_WriteRegister(uint32_t Addr, uint32_t Value);
    // update ohci registers during a device attach
    void OHCI_Attach(USBPort* Port);
    // update ohci registers during a device detach
    void OHCI_Detach(USBPort* Port);

private:
    IRQ * m_irq = nullptr;
    // pointer to g_USB0 or g_USB1
    USBPCIDevice * m_UsbDevice = nullptr;
    // all the registers available on the OHCI standard
    OHCI_Registers m_Registers;
    // end-of-frame timer
    TimerObject* m_pEOFtimer;
    // time at which a SOF was sent
    uint64_t m_SOFtime;
    // the duration of a usb frame
    uint64_t m_UsbFrameTime;
    // ticks per usb tick
    uint64_t m_TicksPerUsbTick;
    // pending usb packet to process
    USBPacket m_UsbPacket = {};
    // temporary buffer that holds the user data to transfer in a packet
    uint8_t m_UsbBuffer[8192] = {};
    // the value of HcControl in the previous frame
    uint32_t m_OldHcControl;
    // irq number
    int m_IrqNum;
    // ergo720: I think it's the DelayInterrupt flag in a TD
    // -> num of frames to wait before generating an interrupt for this TD
    int m_DoneCount;
    // the address of the pending TD
    uint32_t m_AsyncTD;
    // ergo720: I think it signals that a TD has been processed completely
    bool m_AsyncComplete;

    // EOF callback wrapper
    static void OHCI_FrameBoundaryWrapper(void* pVoid);
    // EOF callback function
    void OHCI_FrameBoundaryWorker();
    // inform the HCD that we got a problem here...
    void OHCI_FatalError();
    // initialize packet struct
    void OHCI_PacketInit(USBPacket* packet);
    // change usb state mode
    void OHCI_ChangeState(uint32_t Value);
    // switch the HC to the reset state
    void OHCI_StateReset();
    // start sending SOF tokens across the usb bus
    void OHCI_BusStart();
    // stop sending SOF tokens across the usb bus
    void OHCI_BusStop();
    // generate a SOF event, and start a timer for EOF
    void OHCI_SOF(bool bCreate);
    // change interrupt status
    void OHCI_UpdateInterrupt();
    // fire an interrupt
    void OHCI_SetInterrupt(uint32_t Value);
    // calculate frame time remaining
    uint32_t OHCI_GetFrameRemaining();
    //
    void OHCI_StopEndpoints();
    // set root hub status
    void OHCI_SetHubStatus(uint32_t Value);
    // update power related bits in HcRhPortStatus
    void OHCI_PortPower(int i, int p);
    // set root hub port status
    void OHCI_PortSetStatus(int PortNum, uint32_t Value);
    // set a flag in a port status register but only set it if the port is connected,
    // if not set ConnectStatusChange flag; if flag is enabled return 1
    int OHCI_PortSetIfConnected(int i, uint32_t Value);
    // read the HCCA structure in memory
    bool OHCI_ReadHCCA(uint32_t Paddr, OHCI_HCCA* Hcca);
    // write the HCCA structure in memory
    bool OHCI_WriteHCCA(uint32_t Paddr, OHCI_HCCA* Hcca);
    // read an ED in memory
    bool OHCI_ReadED(uint32_t Paddr, OHCI_ED* Ed);
    // write an ED in memory
    bool OHCI_WriteED(uint32_t Paddr, OHCI_ED* Ed);
    // read an TD in memory
    bool OHCI_ReadTD(uint32_t Paddr, OHCI_TD* Td);
    // write a TD in memory
    bool OHCI_WriteTD(uint32_t Paddr, OHCI_TD* Td);
    // read/write the user buffer pointed to by a TD from/to main memory
    bool OHCI_CopyTD(OHCI_TD* Td, uint8_t* Buffer, int Length, bool bIsWrite);
    // find a TD buffer in memory and copy it
    bool OHCI_FindAndCopyTD(uint32_t Paddr, uint8_t* Buffer, int Length, bool bIsWrite);
    // read an array of DWORDs in memory
    bool OHCI_GetDwords(uint32_t Paddr, uint32_t* Buffer, int Number);
    // write an array of DWORDs in memory
    bool OHCI_WriteDwords(uint32_t Paddr, uint32_t* Buffer, int Number);
    // process an ED list. Returns nonzero if active TD was found
    int OHCI_ServiceEDlist(uint32_t Head, int Completion);
    // process a TD. Returns nonzero to terminate processing of this endpoint
    int OHCI_ServiceTD(OHCI_ED* Ed);
    // find the usb device with the supplied address
    XboxDevice* OHCI::OHCI_FindDevice(uint8_t Addr);
    // cancel a packet when a device is removed
    void OHCI_AsyncCancelDevice(XboxDevice* dev);
    // Process Control and Bulk lists
    void OHCI_ProcessLists(int completion);
};

}

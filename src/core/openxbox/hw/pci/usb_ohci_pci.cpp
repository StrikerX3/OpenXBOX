/*
 * Portions of the code are based on QEMU's USB OHCI Emulation.
 * The original copyright header is included below.
 */
/*
 * QEMU USB OHCI Emulation
 * Copyright (c) 2004 Gianni Tedesco
 * Copyright (c) 2006 CodeSourcery
 * Copyright (c) 2006 Openedhand Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * TODO:
 *  o Isochronous transfers
 *  o Allocate bandwidth in frames properly
 *  o Disable timers when nothing needs to be done, or remove timer usage
 *    all together.
 *  o BIOS work to boot from USB storage
 */
#include "usb_ohci_pci.h"
#include "openxbox/log.h"
#include "openxbox/util.h"

namespace openxbox {

static int64_t g_usbFrameTime;
static int64_t g_usbBitTime;

USBOHCIDevice::USBOHCIDevice(uint16_t vendorID, uint16_t deviceID, uint8_t revisionID, uint8_t numPorts, uint8_t firstPort)
    : PCIDevice(PCI_HEADER_TYPE_NORMAL, vendorID, deviceID, revisionID,
        0x0c, 0x03, 0x10) // USB OHCI
    , m_numPorts(numPorts)
    , m_firstPort(firstPort)
{
    m_eofTimer = new InvokeLater(FrameBoundaryFunc, this);
}

USBOHCIDevice::~USBOHCIDevice() {
    StopBus();

    if (m_async_td) {
        m_usbPacket.Cancel();
        m_async_td = 0;
    }
    StopEndpoints();

    // TODO: master bus
    // TODO: device should be owned by an USB bus
    /*if (!m_masterbus) {
        usb_bus_release(&m_usbBus);
    }*/

    m_eofTimer->Stop();
    delete m_eofTimer;
}

// PCI Device functions

void USBOHCIDevice::Init() {
    RegisterBAR(0, 4096, PCI_BAR_TYPE_MEMORY); // 0xFED00000 - 0xFED00FFF  and  0xFED08000 - 0xFED08FFF

    Write8(m_configSpace, PCI_INTERRUPT_PIN, 1);

    if (m_numPorts > OHCI_MAX_PORTS) {
        log_error("USBOHCIDevice::Init: OHCI num-ports=%d is too big (limit is %d ports)\n", m_numPorts, OHCI_MAX_PORTS);
        return;
    }

    if (g_usbFrameTime == 0) {
        g_usbFrameTime = NANOSECONDS_PER_SECOND / 1000;
        if (NANOSECONDS_PER_SECOND >= USB_HZ) {
            g_usbBitTime = NANOSECONDS_PER_SECOND / USB_HZ;
        }
        else {
            g_usbBitTime = 1;
        }
        log_spew("USBOHCIDevice::Init: USB frame time = %llu,  bit time = %llu\n", g_usbFrameTime, g_usbBitTime);
    }

    // TODO: port the rest of usb_ohci_init
    // TODO: port what's missing from usb_ohci_realize_pci (IRQ allocation basically)
}

void USBOHCIDevice::Reset() {
    HardReset();
}

void USBOHCIDevice::HardReset() {
    SoftReset();
    m_ctl = 0;
    RootHubReset();
}

void USBOHCIDevice::SoftReset() {
    StopBus();
    m_ctl = (m_ctl & OHCI_CTL_IR) | OHCI_USB_SUSPEND;
    m_oldCtl = 0;
    m_status = 0;
    m_intrStatus = 0;
    m_intr = OHCI_INTR_MIE;

    m_hcca = 0;
    m_ctrlHead = m_ctrlCur = 0;
    m_bulkHead = m_bulkCur = 0;
    m_perCur = 0;
    m_done = 0;
    m_doneCount = 7;

    m_fsmps = 0x2778;
    m_fi = 0x2edf;
    m_fit = 0;
    m_frt = 0;
    m_frameNumber = 0;
    m_pstart = 0;
    m_lst = OHCI_LS_THRESH;
}

void USBOHCIDevice::RootHubReset() {
    StopBus();

    m_rhdesc_a = OHCI_RHA_NPS | m_numPorts;
    m_rhdesc_b = 0x0; // Impl. specific
    m_rhstatus = 0;

    for (uint8_t i = 0; i < m_numPorts; i++) {
        OHCIPort *port = &m_rhport[i];
        port->m_ctrl = 0;
        if (port->GetDevice() && port->GetDevice()->IsAttached()) {
            port->Reset();
        }
    }
    if (m_async_td) {
        m_usbPacket.Cancel();
        m_async_td = 0;
    }
    StopEndpoints();
}

void USBOHCIDevice::SetCtl(uint32_t value) {
    uint32_t oldState;
    uint32_t newState;

    oldState = m_ctl & OHCI_CTL_HCFS;
    m_ctl = value;
    newState = m_ctl & OHCI_CTL_HCFS;

    // No state change
    if (oldState == newState) {
        return;
    }

    log_spew("USBOHCIDevice::SetCtl: Switching to new state: 0x%x\n", newState);
    switch (newState) {
    case OHCI_USB_OPERATIONAL:
        StartBus();
        break;
    case OHCI_USB_SUSPEND:
        StopBus();
        // Clear pending SF otherwise linux driver loops in ohci_irq()
        m_intrStatus &= ~OHCI_INTR_SF;
        UpdateInterrupt();
        break;
    case OHCI_USB_RESUME:
        log_spew("USBOHCIDevice::SetCtl: Resuming\n");
        break;
    case OHCI_USB_RESET:
        RootHubReset();
        break;
    }
}

void USBOHCIDevice::SetHubStatus(uint32_t value) {
    uint32_t old_state;

    old_state = m_rhstatus;

    // Write 1 to clear OCIC
    if (value & OHCI_RHS_OCIC) {
        m_rhstatus &= ~OHCI_RHS_OCIC;
    }

    if (value & OHCI_RHS_LPS) {
        for (uint8_t i = 0; i < m_numPorts; i++) {
            PortPower(i, false);
        }
        log_spew("USBOHCIDevice::SetHubStatus: Hub powered down\n");
    }

    if (value & OHCI_RHS_LPSC) {
        for (uint8_t i = 0; i < m_numPorts; i++) {
            PortPower(i, true);
        }
        log_spew("USBOHCIDevice::SetHubStatus: Hub powered up\n");
    }

    if (value & OHCI_RHS_DRWE) {
        m_rhstatus |= OHCI_RHS_DRWE;
    }

    if (value & OHCI_RHS_CRWE) {
        m_rhstatus &= ~OHCI_RHS_DRWE;
    }

    if (old_state != m_rhstatus) {
        SetInterrupt(OHCI_INTR_RHSC);
    }
}

void USBOHCIDevice::SetFrameInterval(uint16_t value) {
    m_fi = value & OHCI_FMI_FI;
}

void USBOHCIDevice::SetPortStatus(uint8_t portNum, uint32_t value) {
    OHCIPort *port = &m_rhport[portNum];
    uint32_t oldState = port->m_ctrl;

    // Write to clear CSC, PESC, PSSC, OCIC, PRSC
    if (value & OHCI_PORT_WTC) {
        port->m_ctrl &= ~(value & OHCI_PORT_WTC);
    }

    if (value & OHCI_PORT_CCS) {
        port->m_ctrl &= ~OHCI_PORT_PES;
    }

    ohci_port_set_if_connected(ohci, portNum, value & OHCI_PORT_PES);

    if (ohci_port_set_if_connected(ohci, portNum, value & OHCI_PORT_PSS)) {
        log_spew("USBOHCIDevice::SetPortStatus: Port %u suspended\n", portNum);
    }

    if (ohci_port_set_if_connected(ohci, portNum, value & OHCI_PORT_PRS)) {
        log_spew("USBOHCIDevice::SetPortStatus: Port %u reset\n", portNum);
        port->GetDevice()->Reset();
        port->m_ctrl &= ~OHCI_PORT_PRS;
        // Should this also set OHCI_PORT_PESC?
        port->m_ctrl |= OHCI_PORT_PES | OHCI_PORT_PRSC;
    }

    // Invert order here to ensure in ambiguous case, device is powered up
    if (value & OHCI_PORT_LSDA) {
        PortPower(portNum, false);
    }
    if (value & OHCI_PORT_PPS) {
        PortPower(portNum, true);
    }

    if (oldState != port->m_ctrl) {
        SetInterrupt(OHCI_INTR_RHSC);
    }
}

void USBOHCIDevice::PortPower(uint8_t index, bool powered) {
    if (powered) {
        m_rhport[index].m_ctrl |= OHCI_PORT_PPS;
    }
    else {
        m_rhport[index].m_ctrl &= ~(OHCI_PORT_PPS |
            OHCI_PORT_CCS |
            OHCI_PORT_PSS |
            OHCI_PORT_PRS);
    }
}

void USBOHCIDevice::StartBus() {
    SetEndOfFrameTimer();
}

void USBOHCIDevice::StopBus() {
    m_eofTimer->Cancel();
}

void USBOHCIDevice::StopEndpoints() {
    for (uint8_t i = 0; i < m_numPorts; i++) {
        USBDevice *dev = m_rhport[i].GetDevice();
        if (dev && dev->IsAttached()) {
            dev->EndpointStopped(dev->GetControlEndpoint());
            for (uint8_t j = 0; j < USB_MAX_ENDPOINTS; j++) {
                dev->EndpointStopped(dev->GetInEndpoint(j));
                dev->EndpointStopped(dev->GetOutEndpoint(j));
            }
        }
    }
}

void USBOHCIDevice::SetInterrupt(uint32_t intr) {
    m_intrStatus |= intr;
    UpdateInterrupt();
}

void USBOHCIDevice::UpdateInterrupt() {
    int level = 0;

    if ((m_intr & OHCI_INTR_MIE) && (m_intrStatus & m_intr)) {
        level = 1;
    }

    m_irq->Handle(level);
}

void USBOHCIDevice::ProcessLists(bool completion) {
    if ((m_ctl & OHCI_CTL_CLE) && (m_status & OHCI_STATUS_CLF)) {
        if (m_ctrlCur && m_ctrlCur != m_ctrlHead) {
            log_spew("USBOHCIDevice::ProcessLists: Processing lists. Head = %u, current = %u\n", m_ctrlHead, m_ctrlCur);
        }
        if (!ServiceEndpointList(m_ctrlHead, completion)) {
            m_ctrlCur = 0;
            m_status &= ~OHCI_STATUS_CLF;
        }
    }

    if ((m_ctl & OHCI_CTL_BLE) && (m_status & OHCI_STATUS_BLF)) {
        if (!ServiceEndpointList(m_bulkHead, completion)) {
            m_bulkCur = 0;
            m_status &= ~OHCI_STATUS_BLF;
        }
    }
}

bool USBOHCIDevice::ServiceEndpointList(uint32_t head, bool completion) {
    struct ohci_ed ed;
    uint32_t next_ed;
    uint32_t linkCnt = 0;
    bool active = false;

    if (head == 0) {
        return false;
    }

    for (uint32_t cur = head; cur; cur = next_ed) {
        if (ohci_read_ed(cur, &ed)) {
            log_warning("USBOHCIDevice::ServiceEndpointList: Endpoint read error at %u\n", cur);
            Die();
            return false;
        }

        next_ed = ed.next & OHCI_DPTR_MASK;

        if (++linkCnt > ED_LINK_LIMIT) {
            Die();
            return false;
        }

        if ((ed.head & OHCI_ED_H) || (ed.flags & OHCI_ED_K)) {
            // Cancel pending packets for ED that have been paused
            uint32_t addr = ed.head & OHCI_DPTR_MASK;
            if (m_async_td && addr == m_async_td) {
                m_usbPacket.Cancel();
                m_async_td = 0;
                m_usbPacket.ep->dev->EndpointStopped(m_usbPacket.ep);
            }
            continue;
        }

        while ((ed.head & OHCI_DPTR_MASK) != ed.tail) {
            active = true;

            if ((ed.flags & OHCI_ED_F) == 0) {
                if (ohci_service_td(&ed)) {
                    break;
                }
            }
            else {
                // Handle isochronous endpoints
                if (ohci_service_iso_td(&ed, completion)) {
                    break;
                }
            }
        }

        if (ohci_put_ed(cur, &ed)) {
            Die();
            return false;
        }
    }

    return active;
}

void USBOHCIDevice::SignalStartOfFrame() {
    SetEndOfFrameTimer();
    SetInterrupt(OHCI_INTR_SF);
}

void USBOHCIDevice::SetEndOfFrameTimer() {
    auto now = std::chrono::high_resolution_clock::now();
    auto target = now + std::chrono::nanoseconds(g_usbFrameTime);

    m_sofTime = now.time_since_epoch().count();
    m_eofTimer->Set(target);
}

uint32_t USBOHCIDevice::GetFrameRemaining() {
    uint16_t fr;
    int64_t tks;

    if ((m_ctl & OHCI_CTL_HCFS) != OHCI_USB_OPERATIONAL) {
        return (m_frt << 31);
    }

    // Being in USB operational state guarnatees sof_time was set already
    tks = GetNanos() - m_sofTime;

    // Avoid muldiv if possible
    if (tks >= g_usbFrameTime) {
        return (m_frt << 31);
    }

    tks = tks / g_usbBitTime;
    fr = (uint16_t)(m_fi - tks);

    return (m_frt << 31) | fr;
}

void USBOHCIDevice::OnFrameBoundary() {
    struct ohci_hcca hcca;

    if (ohci_read_hcca(ohci, m_hcca, &hcca)) {
        Die();
        return;
    }

    // Process all the lists at the end of the frame
    if (m_ctl & OHCI_CTL_PLE) {
        int n;

        n = m_frameNumber & 0x1f;
        ohci_service_ed_list(ohci, hcca.intr[n], 0);
    }

    // Cancel all pending packets if either of the lists has been disabled.
    if (m_oldCtl & (~m_ctl) & (OHCI_CTL_BLE | OHCI_CTL_CLE)) {
        if (m_async_td) {
            m_usbPacket.Cancel();
            m_async_td = 0;
        }
        StopEndpoints();
    }
    m_oldCtl = m_ctl;
    ProcessLists(false);

    // Stop if UnrecoverableError happened or ohci_sof will crash
    if (m_intrStatus & OHCI_INTR_UE) {
        return;
    }

    // Frame boundary, so do EOF stuf here
    m_frt = m_fit;

    // Increment frame number and take care of endianness.
    m_frameNumber = (m_frameNumber + 1) & 0xffff;
    hcca.frame = m_frameNumber;

    if (m_doneCount == 0 && !(m_intrStatus & OHCI_INTR_WD)) {
        if (!m_done)
            abort();
        if (m_intr & m_intrStatus)
            m_done |= 1;
        hcca.done = m_done;
        m_done = 0;
        m_doneCount = 7;
        SetInterrupt(OHCI_INTR_WD);
    }

    if (m_doneCount != 7 && m_doneCount != 0) {
        m_doneCount--;
    }

    // Do SOF stuff here
    SignalStartOfFrame();

    // Writeback HCCA
    if (ohci_put_hcca(m_hcca, &hcca)) {
        Die();
    }
}

void USBOHCIDevice::FrameBoundaryFunc(void *userData) {
    ((USBOHCIDevice *)userData)->OnFrameBoundary();
}

void USBOHCIDevice::Die() {
    SetInterrupt(OHCI_INTR_UE);
    StopBus();

    Write16(m_configSpace, PCI_STATUS, PCI_STATUS_DETECTED_PARITY);
}


void USBOHCIDevice::PCIMMIORead(int barIndex, uint32_t addr, uint32_t *value, uint8_t size) {
    // Only aligned reads are allowed on OHCI
    if (addr & 3) {
        log_warning("USBOHCIDevice::PCIMMIORead:  Unaligned read!   bar = %d,  addr = 0x%x,  size = %u\n", barIndex, addr, size);
        *value = 0xffffffff;
        return;
    }
    
    if (addr >= 0x54 && addr < 0x54 + m_numPorts * 4) {
        // HcRhPortStatus
        *value = m_rhport[(addr - 0x54) >> 2].m_ctrl | OHCI_PORT_PPS;
    }
    else {
        switch (addr >> 2) {
        case 0: // HcRevision
            *value = 0x10;
            break;

        case 1: // HcControl
            *value = m_ctl;
            break;

        case 2: // HcCommandStatus
            *value = m_status;
            break;

        case 3: // HcInterruptStatus
            *value = m_intrStatus;
            break;

        case 4: // HcInterruptEnable
        case 5: // HcInterruptDisable
            *value = m_intr;
            break;

        case 6: // HcHCCA
            *value = m_hcca;
            break;

        case 7: // HcPeriodCurrentED
            *value = m_perCur;
            break;

        case 8: // HcControlHeadED
            *value = m_ctrlHead;
            break;

        case 9: // HcControlCurrentED
            *value = m_ctrlCur;
            break;

        case 10: // HcBulkHeadED
            *value = m_bulkHead;
            break;

        case 11: // HcBulkCurrentED
            *value = m_bulkCur;
            break;

        case 12: // HcDoneHead
            *value = m_done;
            break;

        case 13: // HcFmInter*value
            *value = (m_fit << 31) | (m_fsmps << 16) | (m_fi);
            break;

        case 14: // HcFmRemaining
            *value = GetFrameRemaining();
            break;

        case 15: // HcFmNumber
            *value = m_frameNumber;
            break;

        case 16: // HcPeriodicStart
            *value = m_pstart;
            break;

        case 17: // HcLSThreshold
            *value = m_lst;
            break;

        case 18: // HcRhDescriptorA
            *value = m_rhdesc_a;
            break;

        case 19: // HcRhDescriptorB
            *value = m_rhdesc_b;
            break;

        case 20: // HcRhStatus
            *value = m_rhstatus;
            break;

        // PXA27x specific registers
        case 24: // HcStatus
            *value = m_hstatus & m_hmask;
            break;

        case 25: // HcHReset
            *value = m_hreset;
            break;

        case 26: // HcHInterruptEnable
            *value = m_hmask;
            break;

        case 27: // HcHInterruptTest
            *value = m_htest;
            break;

        default:
            log_warning("USBOHCIDevice::PCIMMIORead:  Unexpected read!   bar = %d,  addr = 0x%x,  size = %u\n", barIndex, addr, size);
            *value = 0xffffffff;
            break;
        }
    }
}

void USBOHCIDevice::PCIMMIOWrite(int barIndex, uint32_t addr, uint32_t value, uint8_t size) {
    // Only aligned reads are allowed on OHCI
    if (addr & 3) {
        log_warning("USBOHCIDevice::PCIMMIOWrite: Unaligned write!  bar = %d,  addr = 0x%x,  size = %u,  value = 0x%x\n", barIndex, addr, size, value);
        return;
    }

    if (addr >= 0x54 && addr < 0x54 + m_numPorts * 4) {
        // HcRhPortStatus
        SetPortStatus((addr - 0x54) >> 2, value);
        return;
    }

    switch (addr >> 2) {
    case 1: // HcControl
        SetCtl(value);
        break;

    case 2: // HcCommandStatus
        // SOC is read-only
        value = (value & ~OHCI_STATUS_SOC);

        // Bits written as '0' remain unchanged in the register
        m_status |= value;

        if (m_status & OHCI_STATUS_HCR) {
            SoftReset();
        }
        break;

    case 3: // HcInterruptStatus
        m_intrStatus &= ~value;
        UpdateInterrupt();
        break;

    case 4: // HcInterruptEnable
        m_intr |= value;
        UpdateInterrupt();
        break;

    case 5: // HcInterruptDisable
        m_intr &= ~value;
        UpdateInterrupt();
        break;

    case 6: // HcHCCA
        m_hcca = value & OHCI_HCCA_MASK;
        break;

    case 7: // HcPeriodCurrentED
        // Ignore writes to this read-only register, Linux does them
        break;

    case 8: // HcControlHeadED
        m_ctrlHead = value & OHCI_EDPTR_MASK;
        break;

    case 9: // HcControlCurrentED
        m_ctrlCur = value & OHCI_EDPTR_MASK;
        break;

    case 10: // HcBulkHeadED
        m_bulkHead = value & OHCI_EDPTR_MASK;
        break;

    case 11: // HcBulkCurrentED
        m_bulkCur = value & OHCI_EDPTR_MASK;
        break;

    case 13: // HcFmInterval
        m_fsmps = (value & OHCI_FMI_FSMPS) >> 16;
        m_fit = (value & OHCI_FMI_FIT) >> 31;
        SetFrameInterval(value);
        break;

    case 15: // HcFmNumber
        break;

    case 16: // HcPeriodicStart
        m_pstart = value & 0xffff;
        break;

    case 17: // HcLSThreshold
        m_lst = value & 0xffff;
        break;

    case 18: // HcRhDescriptorA
        m_rhdesc_a &= ~OHCI_RHA_RW_MASK;
        m_rhdesc_a |= value & OHCI_RHA_RW_MASK;
        break;

    case 19: // HcRhDescriptorB
        break;

    case 20: // HcRhStatus
        SetHubStatus(value);
        break;

        // PXA27x specific registers
    case 24: // HcStatus
        m_hstatus &= ~(value & m_hmask);
        break;

    case 25: // HcHReset
        m_hreset = value & ~OHCI_HRESET_FSBIR;
        if (value & OHCI_HRESET_FSBIR) {
            HardReset();
        }
        break;

    case 26: // HcHInterruptEnable
        m_hmask = value;
        break;

    case 27: // HcHInterruptTest
        m_htest = value;
        break;

    default:
        log_warning("USBOHCIDevice::PCIMMIOWrite: Unexpected write!  bar = %d,  addr = 0x%x,  size = %u\n", barIndex, addr, size);
        break;
    }
}

}

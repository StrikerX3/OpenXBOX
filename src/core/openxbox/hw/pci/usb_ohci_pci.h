#pragma once

#include <cstdint>

#include "../defs.h"
#include "../usb/defs.h"
#include "../basic/irq.h"
#include "pci.h"
#include "openxbox/util/invoke_later.h"

namespace openxbox {

#define USB_MAX_ENDPOINTS  15
#define USB_MAX_INTERFACES 16

#define OHCI_MAX_PORTS 15

#define ED_LINK_LIMIT 32


class USBOHCIDevice : public PCIDevice {
public:
    // constructor
    USBOHCIDevice(uint16_t vendorID, uint16_t deviceID, uint8_t revisionID, uint8_t numPorts = 3, uint8_t firstPort = 0);
    virtual ~USBOHCIDevice();

    // PCI Device functions
    void Init();
    void Reset();

    void PCIMMIORead(int barIndex, uint32_t addr, uint32_t *value, uint8_t size) override;
    void PCIMMIOWrite(int barIndex, uint32_t addr, uint32_t value, uint8_t size) override;
private:
    uint8_t m_numPorts;
    uint8_t m_firstPort;

    // ---- OHCI state ----------

    InvokeLater *m_eofTimer;
    int64_t m_sofTime = 0;

    // Control partition
    uint32_t m_ctl = 0;
    uint32_t m_status = 0;
    uint32_t m_intrStatus = 0;
    uint32_t m_intr = 0;
    
    // Memory pointer partition
    uint32_t m_hcca = 0;
    uint32_t m_ctrlHead = 0;
    uint32_t m_ctrlCur = 0;
    uint32_t m_bulkHead = 0;
    uint32_t m_bulkCur = 0;
    uint32_t m_perCur = 0;
    uint32_t m_done = 0;
    int32_t m_doneCount = 0;
    
    // Frame counter partition
    uint16_t m_fsmps = 0;
    uint8_t m_fit = 0;
    uint16_t m_fi = 0;
    uint8_t m_frt = 0;
    uint16_t m_frameNumber = 0;
    uint16_t m_padding = 0;
    uint32_t m_pstart = 0;
    uint32_t m_lst = 0;

    // Root Hub partition
    uint32_t m_rhdesc_a = 0;
    uint32_t m_rhdesc_b = 0;
    uint32_t m_rhstatus = 0;
    OHCIPort m_rhport[OHCI_MAX_PORTS];

    // PXA27x Non-OHCI events
    uint32_t m_hstatus = 0;
    uint32_t m_hmask = 0;
    uint32_t m_hreset = 0;
    uint32_t m_htest = 0;

    // SM501 local memory offset
    dma_addr_t m_localmemBase;

    // Active packets
    uint32_t m_oldCtl = 0;
    USBPacket m_usbPacket = { 0 };
    uint8_t m_usbBuf[8192] = { 0 };
    uint32_t m_async_td = 0;
    bool m_asyncComplete = false;

    // ----- OHCI functions -----
    void SetInterrupt(uint32_t intr);
    void UpdateInterrupt();

    void SetCtl(uint32_t value);
    void SetHubStatus(uint32_t value);
    void SetFrameInterval(uint16_t value);

    void HardReset();
    void SoftReset();
    void RootHubReset();
    
    void StartBus();
    void StopBus();

    void StopEndpoints();

    void SignalStartOfFrame();
    void SetEndOfFrameTimer();
    uint32_t GetFrameRemaining();

    void OnFrameBoundary();
    static void FrameBoundaryFunc(void *userData);

    void Die();
};

}

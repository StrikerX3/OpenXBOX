#pragma once

#include "../pci/pci.h"
#include "openxbox/io.h"

#include <map>

namespace openxbox {

#define PORT_PCI_CONFIG_ADDRESS   0xCF8
#define PORT_PCI_CONFIG_DATA      0xCFC
#define PCI_CONFIG_REGISTER_MASK  0xFC

#define PCI_DEVFN(slot, func) ((((slot) & 0x1f) << 3) | ((func) & 0x07))

#define PCI_SLOT(devfn)	(((devfn) >> 3) & 0x1f) // 5 bits (PCIConfigAddressRegister.deviceNumber)
#define PCI_FUNC(devfn)	((devfn) & 0x07) // 3 bits (PCIConfigAddressRegister.functionNumber)

#define PCI_DEVID(bus, devfn)  ((((uint16_t)(bus)) << 8) | (devfn))

#define PCI_BUS_NUM(x) (((x) >> 8) & 0xff)

typedef struct {
    uint8_t registerNumber : 8;
    uint8_t functionNumber : 3; // PCI_FUNC
    uint8_t deviceNumber : 5; // PCI_SLOT
    uint8_t busNumber : 8; // PCI_BUS_NUM
    uint8_t reserved : 7;
    uint8_t enable : 1;
} PCIConfigAddressRegister;

class PCIBus : public IODevice {
public:
    virtual ~PCIBus();
    bool MapIO(IOMapper *mapper);
    
    void ConnectDevice(uint32_t deviceId, PCIDevice *pDevice);

    bool IORead(uint32_t port, uint32_t *value, uint8_t size) override;
    bool IOWrite(uint32_t port, uint32_t value, uint8_t size) override;

    bool MMIORead(uint32_t addr, uint32_t *value, uint8_t size) override;
    bool MMIOWrite(uint32_t addr, uint32_t value, uint8_t size) override;

    void Reset();
private:
    void IOWriteConfigAddress(uint32_t pData);
    void IOWriteConfigData(uint32_t pData, uint8_t size, uint8_t regOffset);
    uint32_t IOReadConfigData(uint8_t size, uint8_t regOffset);

    std::map<uint32_t, PCIDevice*> m_Devices;
    PCIConfigAddressRegister m_configAddressRegister;
};

}

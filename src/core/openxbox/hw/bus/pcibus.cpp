#include "pcibus.h"
#include "openxbox/log.h"

#include <cassert>

namespace openxbox {

PCIBus::~PCIBus() {
}

bool PCIBus::MapIO(IOMapper *mapper) {
    if (!mapper->MapIODevice(PORT_PCI_CONFIG_ADDRESS, 1, this)) return false;
    if (!mapper->MapIODevice(PORT_PCI_CONFIG_DATA, 4, this)) return false;
    if (!mapper->AddDevice(this)) return false;

    return true;
}

void PCIBus::ConnectDevice(uint32_t deviceId, PCIDevice *pDevice) {
    if (m_Devices.find(deviceId) != m_Devices.end()) {
        log_warning("PCIBus: Attempting to connect two devices to the same device address\n");
        return;
    }

    m_Devices[deviceId] = pDevice;
    pDevice->Init();
}

void PCIBus::IOWriteConfigAddress(uint32_t pData) {
    memcpy(&m_configAddressRegister, &pData, sizeof(PCIConfigAddressRegister));
}

uint32_t PCIBus::IOReadConfigData(uint8_t size, uint8_t regOffset) {
    log_spew("PCIBus::IOReadConfigData:  (%d:%d:%d reg 0x%x offset %d size %d)\n",
        m_configAddressRegister.busNumber,
        m_configAddressRegister.deviceNumber,
        m_configAddressRegister.functionNumber,
        m_configAddressRegister.registerNumber,
		regOffset,
        size
    );

    auto it = m_Devices.find(
        PCI_DEVID(m_configAddressRegister.busNumber,
            PCI_DEVFN(m_configAddressRegister.deviceNumber, m_configAddressRegister.functionNumber)
        )
    );
    if (it != m_Devices.end()) {
        uint32_t value = 0;
        it->second->ReadConfig((m_configAddressRegister.registerNumber & PCI_CONFIG_REGISTER_MASK) + regOffset, reinterpret_cast<uint8_t *>(&value), size);
        return value;
    }

    log_warning("PCIBus::IOReadConfigData:  Invalid Device Read  (%d:%d:%d reg 0x%x offset %d size %d)\n",
        m_configAddressRegister.busNumber,
        m_configAddressRegister.deviceNumber,
        m_configAddressRegister.functionNumber,
        m_configAddressRegister.registerNumber,
		regOffset,
        size
    );

    // Unpopulated PCI slots return 0xFFFFFFFF
    return 0xFFFFFFFF;
}

void PCIBus::IOWriteConfigData(uint32_t pData, uint8_t size, uint8_t regOffset) {
    log_spew("PCIBus::IOWriteConfigData: (%d:%d:%d reg 0x%x offset %d size %d) = 0x%x\n",
        m_configAddressRegister.busNumber,
        m_configAddressRegister.deviceNumber,
        m_configAddressRegister.functionNumber,
        m_configAddressRegister.registerNumber,
		regOffset,
        size,
        pData
    );

    auto it = m_Devices.find(
        PCI_DEVID(m_configAddressRegister.busNumber,
            PCI_DEVFN(m_configAddressRegister.deviceNumber, m_configAddressRegister.functionNumber)
        )
    );
    if (it != m_Devices.end()) {
        it->second->WriteConfig((m_configAddressRegister.registerNumber & PCI_CONFIG_REGISTER_MASK) + regOffset, pData, size);
        return;
    }

    log_warning("PCIBus::IOWriteConfigData: Invalid Device Write (%d:%d:%d reg 0x%x offset %d size %d) = 0x%x\n",
        m_configAddressRegister.busNumber,
        m_configAddressRegister.deviceNumber,
        m_configAddressRegister.functionNumber,
        m_configAddressRegister.registerNumber,
		regOffset,
        size,
        pData
    );
}

bool PCIBus::IORead(uint32_t port, uint32_t *value, uint8_t size) {
    switch (port) {
	case PORT_PCI_CONFIG_DATA: // 0xCFC
	case PORT_PCI_CONFIG_DATA + 1: // 0xCFD
	case PORT_PCI_CONFIG_DATA + 2: // 0xCFE
	case PORT_PCI_CONFIG_DATA + 3: // 0xCFF
		*value = IOReadConfigData(size, port - PORT_PCI_CONFIG_DATA);
        return true;
    default:
        for (auto it = m_Devices.begin(); it != m_Devices.end(); ++it) {
            uint8_t barIndex;
            uint32_t baseAddress;
            if (it->second->GetIOBar(port, &barIndex, &baseAddress)) {
                it->second->PCIIORead(barIndex, port - baseAddress, value, size);
                return true;
            }
        }
    }

    return false;
}

bool PCIBus::IOWrite(uint32_t port, uint32_t value, uint8_t size) {
    switch (port) {
    case PORT_PCI_CONFIG_ADDRESS: // 0xCF8
        if (size == sizeof(uint32_t)) {
            IOWriteConfigAddress(value);
            return true;
        }
        else {
            log_warning("PCIBus:IOWrite: Writing %d-bit PCI config address,  address 0x%x,  value 0x%x\n", size << 3, port, value);
            IOWriteConfigAddress(value);
            return true;
        }
        break;
	case PORT_PCI_CONFIG_DATA: // 0xCFC
	case PORT_PCI_CONFIG_DATA + 1: // 0xCFD
	case PORT_PCI_CONFIG_DATA + 2: // 0xCFE
	case PORT_PCI_CONFIG_DATA + 3: // 0xCFF
        IOWriteConfigData(value, size, port - PORT_PCI_CONFIG_DATA);
        return true; // TODO : Should IOWriteConfigData() success/failure be returned?
    default:
        for (auto it = m_Devices.begin(); it != m_Devices.end(); ++it) {
            uint8_t barIndex;
            uint32_t baseAddress;
            if (it->second->GetIOBar(port, &barIndex, &baseAddress)) {
                it->second->PCIIOWrite(barIndex, port - baseAddress, value, size);
                return true;
            }
        }
    }

    return false;
}

bool PCIBus::MMIORead(uint32_t addr, uint32_t *value, uint8_t size) {
    for (auto it = m_Devices.begin(); it != m_Devices.end(); ++it) {
        uint8_t barIndex;
        uint32_t baseAddress;
        if (it->second->GetMMIOBar(addr, &barIndex, &baseAddress)) {
            it->second->PCIMMIORead(barIndex, addr - baseAddress, value, size);
            return true;
        }
    }

    return false;
}

bool PCIBus::MMIOWrite(uint32_t addr, uint32_t value, uint8_t size) {
    for (auto it = m_Devices.begin(); it != m_Devices.end(); ++it) {
        PCIDevice *dev = it->second;
        uint8_t barIndex;
        uint32_t baseAddress;
        if (dev->GetMMIOBar(addr, &barIndex, &baseAddress)) {
            dev->PCIMMIOWrite(barIndex, addr - baseAddress, value, size);
            return true;
        }
    }

    return false;
}

void PCIBus::Reset() {
    for (auto it = m_Devices.begin(); it != m_Devices.end(); ++it) {
        it->second->Reset();
    }
}

}

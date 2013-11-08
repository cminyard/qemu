/*
 * IPMI SMBIOS firmware handling
 *
 * Copyright (c) 2015 Corey Minyard, MontaVista Software, LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "hw/ipmi/ipmi.h"
#include "hw/smbios/ipmi.h"
#include "hw/smbios/smbios.h"
#include "qemu/error-report.h"
#include "smbios_build.h"

/* SMBIOS type 38 - IPMI */
struct smbios_type_38 {
    struct smbios_structure_header header;
    uint8_t interface_type;
    uint8_t ipmi_spec_revision;
    uint8_t i2c_slave_address;
    uint8_t nv_storage_device_address;
    uint64_t base_address;
    uint8_t base_address_modifier;
    uint8_t interrupt_number;
} QEMU_PACKED;

static void ipmi_encode_one_smbios(IPMIFwInfo *info)
{
    uint64_t baseaddr = info->base_address;
    SMBIOS_BUILD_TABLE_PRE(38, 0x3000, true);

    t->interface_type = info->interface_type;
    t->ipmi_spec_revision = ((info->ipmi_spec_major_revision << 4)
                             | info->ipmi_spec_minor_revision);
    t->i2c_slave_address = info->i2c_slave_address;
    t->nv_storage_device_address = 0;

    /* or 1 to set it to I/O space */
    switch (info->memspace) {
    case IPMI_MEMSPACE_IO: baseaddr |= 1; break;
    case IPMI_MEMSPACE_MEM32: break;
    case IPMI_MEMSPACE_MEM64: break;
    case IPMI_MEMSPACE_SMBUS: baseaddr <<= 1; break;
    }

    t->base_address = cpu_to_le64(baseaddr);
    
    t->base_address_modifier = 0;
    if (info->irq_type == IPMI_LEVEL_IRQ) {
        t->base_address_modifier |= 1;
    }
    switch (info->register_spacing) {
    case 1: break;
    case 4: t->base_address_modifier |= 1 << 6; break;
    case 16: t->base_address_modifier |= 2 << 6; break;
    default:
        error_report("IPMI register spacing %d is not compatible with"
                     " SMBIOS, ignoring this entry.", info->register_spacing);
        return;
    }
    t->interrupt_number = info->interrupt_number;

    SMBIOS_BUILD_TABLE_POST;
}

void smbios_build_type_38_table(void)
{
    IPMIFwInfo *info = ipmi_first_fwinfo();

    while (info) {
        ipmi_encode_one_smbios(info);
        info = ipmi_next_fwinfo(info);
    }
}

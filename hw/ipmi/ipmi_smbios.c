/*
 * IPMI SMBIOS firmware handling
 *
 * Copyright (c) 2015 Corey Minyard, MontaVista Software, LLC
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

#include "hw/ipmi/ipmi.h"
#include <hw/i386/smbios.h>
#include <qemu/error-report.h>

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

static void ipmi_encode_one_smbios(IPMIFwInfo *info, void *opaque, Error **errp)
{
    struct smbios_type_38 smb38;
    uint64_t baseaddr = info->base_address;

    smb38.header.type = 38;
    smb38.header.length = sizeof(smb38);
    smb38.header.handle = cpu_to_le16(0x3000);
    smb38.interface_type = info->interface_type;
    smb38.ipmi_spec_revision = ((info->ipmi_spec_major_revision << 4)
                                | info->ipmi_spec_minor_revision);
    smb38.i2c_slave_address = info->i2c_slave_address;
    smb38.nv_storage_device_address = 0;

    /* or 1 to set it to I/O space */
    switch (info->memspace) {
    case IPMI_MEMSPACE_IO: baseaddr |= 1; break;
    case IPMI_MEMSPACE_MEM32: break;
    case IPMI_MEMSPACE_MEM64: break;
    case IPMI_MEMSPACE_SMBUS: baseaddr <<= 1; break;
    }

    smb38.base_address = cpu_to_le64(baseaddr);
    
    smb38.base_address_modifier = 0;
    if (info->irq_type == IPMI_LEVEL_IRQ) {
        smb38.base_address_modifier |= 1;
    }
    switch (info->register_spacing) {
    case 1: break;
    case 4: smb38.base_address_modifier |= 1 << 6; break;
    case 16: smb38.base_address_modifier |= 2 << 6; break;
    default:
        error_report("IPMI register spacing %d is not compatible with"
                     " SMBIOS, ignoring this entry.", info->register_spacing);
        return;
    }
    smb38.interrupt_number = info->interrupt_number;

    smbios_add_table_entry((struct smbios_structure_header *) &smb38,
                           sizeof(smb38), true);
}

static void smbios_init(void)
{
    ipmi_register_fwinfo_handler(ipmi_encode_one_smbios, NULL);
}

type_init(smbios_init);

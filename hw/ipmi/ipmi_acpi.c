/*
 * IPMI ACPI firmware handling
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

#include "ipmi.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/acpi-dev-tables.h"
#include <qemu/error-report.h>

static Aml *aml_ipmi_crs(IPMIFwInfo *info)
{
    Aml *crs = aml_resource_template();
    uint8_t regspacing = info->register_spacing;

    if (regspacing == 1) {
        regspacing = 0;
    }

    switch (info->memspace) {
    case IPMI_MEMSPACE_IO:
        aml_append(crs, aml_io(aml_decode16, info->base_address,
                               info->base_address + info->register_length - 1,
                               regspacing, info->register_length));
        break;
    case IPMI_MEMSPACE_MEM32:
        aml_append(crs,
                   aml_dword_memory(aml_pos_decode,
                            aml_min_fixed, aml_max_fixed,
                            aml_non_cacheable, aml_ReadWrite,
                            0xffffffff,
                            info->base_address,
                            info->base_address + info->register_length - 1,
                            regspacing, info->register_length));
        break;
    case IPMI_MEMSPACE_MEM64:
        aml_append(crs,
                   aml_qword_memory(aml_pos_decode,
                            aml_min_fixed, aml_max_fixed,
                            aml_non_cacheable, aml_ReadWrite,
                            0xffffffffffffffffULL,
                            info->base_address,
                            info->base_address + info->register_length - 1,
                            regspacing, info->register_length));
        break;
    case IPMI_MEMSPACE_SMBUS:
        aml_append(crs, aml_return(aml_int(info->base_address)));
        break;
    }

    if (info->interrupt_number) {
        aml_append(crs, aml_irq_no_flags(info->interrupt_number));
    }

    return crs;
}

static void
ipmi_encode_one_acpi(IPMIFwInfo *info, void *opaque)
{
    Aml *ssdt, *scope, *dev, *method;
    char *name;
    int version = ((info->ipmi_spec_major_revision << 8)
                   | (info->ipmi_spec_minor_revision << 4));

    if (!info->acpi_parent) {
        ipmi_debug("device %s not compatible with ACPI, no parent given.",
                   info->interface_name);
        return;
    }

    ssdt = init_aml_allocator();

    scope = aml_scope("%s", info->acpi_parent);
    name = g_strdup_printf("ipmi_%s", info->interface_name);

    dev = aml_device("MI0");
    aml_append(dev, aml_name_decl("_HID", aml_eisaid("IPI0001")));
    aml_append(dev, aml_name_decl("_STR", aml_string("%s", name)));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));
    aml_append(dev, aml_name_decl("_CRS", aml_ipmi_crs(info)));
    method = aml_method("_IFT", 0);
    aml_append(method, aml_return(aml_int(info->interface_type)));
    aml_append(dev, method);
    method = aml_method("_SRV", 0);
    aml_append(method, aml_return(aml_int(version)));
    aml_append(dev, method);

    aml_append(scope, dev);

    aml_append(ssdt, scope);

    add_acpi_dev_table(ssdt->buf->data, ssdt->buf->len, "SSDT", 1);
    free_aml_allocator();
}

static void ipmi_acpi_init(void)
{
    ipmi_register_fwinfo_handler(ipmi_encode_one_acpi, NULL);
}

type_init(ipmi_acpi_init);

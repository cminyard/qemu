/*
 * IPMI ACPI firmware handling
 *
 * Copyright (c) 2015 Corey Minyard, MontaVista Software, LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "hw/ipmi/ipmi.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/ipmi.h"

static Aml *aml_ipmi_crs(IPMIFwInfo *info)
{
    Aml *crs = aml_resource_template();
    uint8_t regspacing = info->register_spacing;

    /*
     * The base address is fixed and cannot change.  That may be different
     * if someone does PCI, but we aren't there yet.
     */
    switch (info->memspace) {
    case IPMI_MEMSPACE_IO:
        aml_append(crs, aml_io(AML_DECODE16, info->base_address,
                               info->base_address + info->register_length - 1,
                               regspacing, info->register_length));
        break;
    case IPMI_MEMSPACE_MEM32:
        aml_append(crs,
                   aml_dword_memory(AML_POS_DECODE,
                            AML_MIN_FIXED, AML_MAX_FIXED,
                            AML_NON_CACHEABLE, AML_READ_WRITE,
                            0xffffffff,
                            info->base_address,
                            info->base_address + info->register_length - 1,
                            regspacing, info->register_length));
        break;
    case IPMI_MEMSPACE_MEM64:
        aml_append(crs,
                   aml_qword_memory(AML_POS_DECODE,
                            AML_MIN_FIXED, AML_MAX_FIXED,
                            AML_NON_CACHEABLE, AML_READ_WRITE,
                            0xffffffffffffffffULL,
                            info->base_address,
                            info->base_address + info->register_length - 1,
                            regspacing, info->register_length));
        break;
    case IPMI_MEMSPACE_SMBUS:
        aml_append(crs, aml_return(aml_int(info->base_address)));
        break;
    default:
        abort();
    }

    if (info->interrupt_number) {
        aml_append(crs, aml_irq_no_flags(info->interrupt_number));
    }

    return crs;
}

static void
ipmi_encode_one_acpi(Aml *ssdt, IPMIFwInfo *info)
{
    Aml *scope, *dev, *method;
    uint16_t version = ((info->ipmi_spec_major_revision << 8)
                        | (info->ipmi_spec_minor_revision << 4));

    /*
     * The ACPI parent is a little bit of a pain.  It could be in
     * different places depending on the device.  It could be an SMBus,
     * it could be ISA, it could be PCI, etc.  Only the device really
     * knows, so it has to pass it in.
     */
    if (!info->acpi_parent) {
        ipmi_debug("device %s not compatible with ACPI, no parent given.",
                   info->interface_name);
        return;
    }

    scope = aml_scope("%s", info->acpi_parent);

    dev = aml_device("MI0");
    aml_append(dev, aml_name_decl("_HID", aml_eisaid("IPI0001")));
    aml_append(dev, aml_name_decl("_STR", aml_string("ipmi_%s",
                                                     info->interface_name)));
    aml_append(dev, aml_name_decl("_UID", aml_int(info->uuid)));
    aml_append(dev, aml_name_decl("_CRS", aml_ipmi_crs(info)));

    /*
     * The spec seems to require these to be methods.  All the examples
     * show them this way and it doesn't seem to work if they are not.
     */
    method = aml_method("_IFT", 0);
    aml_append(method, aml_return(aml_int(info->interface_type)));
    aml_append(dev, method);
    method = aml_method("_SRV", 0);
    aml_append(method, aml_return(aml_int(version)));
    aml_append(dev, method);

    aml_append(scope, dev);

    aml_append(ssdt, scope);
}

void acpi_add_ipmi(Aml *ssdt)
{
    IPMIFwInfo *info = ipmi_first_fwinfo();

    while (info) {
        ipmi_encode_one_acpi(ssdt, info);
        info = ipmi_next_fwinfo(info);
    }
}

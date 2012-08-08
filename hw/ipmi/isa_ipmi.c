/*
 * QEMU ISA IPMI emulation
 *
 * Copyright (c) 2012 Corey Minyard, MontaVista Software, LLC
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
#include "hw/hw.h"
#include "hw/isa/isa.h"
#include "hw/i386/pc.h"
#include "qemu/timer.h"
#include "sysemu/char.h"
#include "sysemu/sysemu.h"
#include "ipmi.h"

/* This is the type the user specifies on the -device command line */
#define TYPE_ISA_IPMI           "isa-ipmi"
#define ISA_IPMI(obj) OBJECT_CHECK(ISAIPMIDevice, (obj), TYPE_ISA_IPMI)

typedef struct ISAIPMIDevice {
    ISADevice dev;
    char *interface;
    uint32_t iobase;
    int32 isairq;
    uint8_t slave_addr;
    CharDriverState *chr;
    IPMIInterface *intf;
} ISAIPMIDevice;

static void ipmi_isa_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    ISAIPMIDevice *ipmi = ISA_IPMI(dev);
    char typename[20];
    Object *intfobj;
    IPMIInterface *intf;
    Object *bmcobj;
    IPMIBmc *bmc;

    if (!ipmi->interface) {
        ipmi->interface = g_strdup("kcs");
    }

    if (ipmi->chr) {
        bmcobj = object_new(TYPE_IPMI_BMC_EXTERN);
    } else {
        bmcobj = object_new(TYPE_IPMI_BMC_SIMULATOR);
    }
    bmc = IPMI_BMC(bmcobj);
    bmc->chr = ipmi->chr;
    snprintf(typename, sizeof(typename),
             TYPE_IPMI_INTERFACE_PREFIX "%s", ipmi->interface);
    intfobj = object_new(typename);
    intf = IPMI_INTERFACE(intfobj);
    bmc->intf = intf;
    intf->bmc = bmc;
    intf->io_base = ipmi->iobase;
    intf->slave_addr = ipmi->slave_addr;
    ipmi_interface_init(intf, errp);
    if (*errp) {
        return;
    }
    ipmi_bmc_init(bmc, errp);
    if (*errp) {
        return;
    }

    /* These may be set by the interface. */
    ipmi->iobase = intf->io_base;
    ipmi->slave_addr = intf->slave_addr;

    if (ipmi->isairq > 0) {
        isa_init_irq(isadev, &intf->irq, ipmi->isairq);
        intf->use_irq = 1;
    }

    ipmi->intf = intf;
    object_property_add_child(OBJECT(isadev), "intf", OBJECT(intf), errp);
    if (*errp) {
        return;
    }
    object_property_add_child(OBJECT(isadev), "bmc", OBJECT(bmc), errp);
    if (*errp) {
        return;
    }

    qdev_set_legacy_instance_id(dev, intf->io_base, intf->io_length);

    isa_register_ioport(isadev, &intf->io, intf->io_base);
}

static void ipmi_isa_reset(DeviceState *qdev)
{
    ISAIPMIDevice *isa = ISA_IPMI(qdev);

    ipmi_interface_reset(isa->intf);
}

static Property ipmi_isa_properties[] = {
    DEFINE_PROP_STRING("interface", ISAIPMIDevice, interface),
    DEFINE_PROP_UINT32("iobase", ISAIPMIDevice, iobase,  0),
    DEFINE_PROP_INT32("irq",   ISAIPMIDevice, isairq,  5),
    DEFINE_PROP_UINT8("slave_addr", ISAIPMIDevice, slave_addr,  0),
    DEFINE_PROP_CHR("chardev",  ISAIPMIDevice, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_isa_ipmi = {
    .name = TYPE_ISA_IPMI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_STRUCT_POINTER(intf, ISAIPMIDevice, vmstate_IPMIInterface,
                               IPMIInterface),
        VMSTATE_END_OF_LIST()
    }
};

static void ipmi_isa_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = ipmi_isa_realizefn;
    dc->reset = ipmi_isa_reset;
    dc->vmsd = &vmstate_isa_ipmi;
    dc->props = ipmi_isa_properties;
}

static const TypeInfo ipmi_isa_info = {
    .name          = TYPE_ISA_IPMI,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISAIPMIDevice),
    .class_init    = ipmi_isa_class_initfn,
};

static void ipmi_register_types(void)
{
    type_register_static(&ipmi_isa_info);
}

type_init(ipmi_register_types)

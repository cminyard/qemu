/*
 * QEMU ISA named IRQ
 *
 * Copyright (c) 2015,2016 Corey Minyard, MontaVista Software, LLC
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
#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/irqif.h"
#include "hw/isa/isa.h"

/* This is the type the user specifies on the -device command line */
#define TYPE_ISA_IRQ           "isa-irq"
#define ISA_IRQ(obj) OBJECT_CHECK(ISAIRQDevice, (obj), TYPE_ISA_IRQ)

typedef struct ISAIRQDevice {
    ISADevice dev;
    int32_t isairq;
    qemu_irq irq;
} ISAIRQDevice;

static qemu_irq *irq_isa_get_irq(IRQInterface *ii)
{
    ISAIRQDevice *iid = ISA_IRQ(ii);

    return &iid->irq;
}

static void irq_isa_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    ISAIRQDevice *iid = ISA_IRQ(dev);

    isa_init_irq(isadev, &iid->irq, iid->isairq);
}

static Property irq_isa_properties[] = {
    DEFINE_PROP_INT32("irq",   ISAIRQDevice, isairq,  5),
    DEFINE_PROP_END_OF_LIST(),
};

static void irq_isa_class_initfn(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    IRQInterfaceClass *ii = IRQ_INTERFACE_CLASS(oc);

    dc->realize = irq_isa_realizefn;
    dc->props_ = irq_isa_properties;
    ii->get_irq = irq_isa_get_irq;
}

static const TypeInfo irq_isa_info = {
    .name          = TYPE_ISA_IRQ,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISAIRQDevice),
    .class_init    = irq_isa_class_initfn,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_IRQ_INTERFACE },
        { }
    }
};

static void irq_register_types(void)
{
    type_register_static(&irq_isa_info);
}

type_init(irq_register_types)

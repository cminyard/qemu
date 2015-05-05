/*
 * QEMU SMBus alert
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
#include "migration/vmstate.h"
#include "hw/i2c/smbus_alert.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

static void smbus_alert(SMBusAlertDevice *sad, uint8_t devaddr)
{
    unsigned int i;

    for (i = 0; i < sad->qlen; i++) {
        /* Reject duplicates. */
        if (devaddr == sad->q[i]) {
            return;
        }
    }

    if (sad->qlen >= SMBUS_ALERT_QLEN) {
        return;  /* Shouldn't be able to happen, but just in case. */
    }

    sad->q[sad->qlen++] = devaddr;
    qemu_irq_raise(*sad->irq);
}

static uint8_t alert_receive_byte(SMBusDevice *dev)
{
    SMBusAlertDevice *sad = (SMBusAlertDevice *) dev;
    uint8_t val;

    if (sad->qlen == 0) {
        return -1;
    }

    val = sad->q[0];
    memcpy(sad->q, sad->q + 1, --sad->qlen);
    if (sad->qlen == 0) {
            qemu_irq_lower(*sad->irq);
    }

    return val;
}

static int smbus_alert_event(SMBusDevice *dev, enum i2c_event event)
{
    SMBusAlertDevice *sad = (SMBusAlertDevice *) dev;

    if (event == I2C_START_RECV && sad->qlen == 0) {
        return -1;
    }

    return 0;
}

static const VMStateDescription vmstate_smbus_alert = {
    .name = TYPE_SMBUS_ALERT_DEVICE,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_SMBUS_DEVICE(parent, SMBusAlertDevice),
        VMSTATE_UINT32(qlen, SMBusAlertDevice),
        VMSTATE_VARRAY_UINT32(q, SMBusAlertDevice, qlen, 1,
                              vmstate_info_uint8, uint8_t),
        VMSTATE_END_OF_LIST()
    }
};

static void smbus_alert_realize(DeviceState *dev, Error **errp)
{
    SMBusAlertDevice *sad = SMBUS_ALERT_DEVICE(dev);
    IRQInterfaceClass *iic;

    if (!sad->irqdev) {
        error_setg(errp, "SMBus alert device created without irqid");
        return;
    }

    sad->q = g_malloc(SMBUS_ALERT_QLEN);

    iic = IRQ_INTERFACE_GET_CLASS(sad->irqdev);

    sad->irq = iic->get_irq(sad->irqdev);
}

static void irq_check(const Object *obj, const char *name,
                      Object *val, Error **errp)
{
    /* Always succeed. */
}

static void smbus_alert_init(Object *obj)
{
    SMBusAlertDevice *sad = SMBUS_ALERT_DEVICE(obj);

    object_property_add_link(obj, "irqid", TYPE_IRQ_INTERFACE,
                             (Object **) &sad->irqdev, irq_check,
                             OBJ_PROP_LINK_STRONG);
}

static void smbus_alert_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    SMBusDeviceClass *sc = SMBUS_DEVICE_CLASS(oc);
    SMBusAlertDeviceClass *sadc = SMBUS_ALERT_DEVICE_CLASS(oc);

    dc->realize = smbus_alert_realize;
    dc->vmsd = &vmstate_smbus_alert;
    sc->receive_byte = alert_receive_byte;
    sc->event = smbus_alert_event;
    sadc->alert = smbus_alert;
}

static const TypeInfo smbus_alert_info = {
    .name          = TYPE_SMBUS_ALERT_DEVICE,
    .parent        = TYPE_SMBUS_DEVICE,
    .instance_size = sizeof(SMBusAlertDevice),
    .instance_init = smbus_alert_init,
    .class_init    = smbus_alert_class_init,
    .class_size    = sizeof(SMBusAlertDeviceClass),
};

static void smbus_alert_register_types(void)
{
    type_register_static(&smbus_alert_info);
}

type_init(smbus_alert_register_types)

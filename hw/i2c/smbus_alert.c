/*
 * QEMU SMBus alert
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
#include "hw/hw.h"
#include "hw/i2c/smbus_alert.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

static void smbus_alert(SMBusAlertDevice *sad, SMBusAlertEntry *alert)
{
    if (alert->enqueued)
        return;

    alert->enqueued = true;
    QSIMPLEQ_INSERT_TAIL(&sad->alert_queue, alert, link);
    qemu_irq_raise(*sad->irq);
}

static uint8_t alert_receive_byte(SMBusDevice *dev)
{
    SMBusAlertDevice *sad = (SMBusAlertDevice *) dev;
    SMBusAlertEntry *alert = QSIMPLEQ_FIRST(&sad->alert_queue);

    if (!alert)
        return -1;

    alert->enqueued = false;
    QSIMPLEQ_REMOVE_HEAD(&sad->alert_queue, link);

    if (QSIMPLEQ_EMPTY(&sad->alert_queue)) {
            qemu_irq_lower(*sad->irq);
    }

    return alert->val;
}

static int smbus_alert_event(I2CSlave *s, enum i2c_event event)
{
    SMBusAlertDevice *sad = (SMBusAlertDevice *) s;
    I2CSlaveClass *sc = I2C_SLAVE_GET_CLASS(s);

    if (event == I2C_START_RECV && QSIMPLEQ_EMPTY(&sad->alert_queue))
        return -1;

    sc->event(s, event);
    return 0;
}

static void smbus_alert_realize(DeviceState *dev, Error **errp)
{
    SMBusAlertDevice *sad = SMBUS_ALERT_DEVICE(dev);
    IRQInterfaceClass *iic;

    if (!sad->irqdev) {
        error_setg(errp, "SMBus alert device created without irqid");
        return;
    }

    iic = IRQ_INTERFACE_GET_CLASS(sad->irqdev);

    sad->irq = iic->get_irq(sad->irqdev);
}

static void irq_check(Object *obj, const char *name,
                      Object *val, Error **errp)
{
    /* Always succeed. */
}

static void smbus_alert_init(Object *obj)
{
    SMBusAlertDevice *sad = SMBUS_ALERT_DEVICE(obj);

    QSIMPLEQ_INIT(&sad->alert_queue);

    object_property_add_link(obj, "irqid", TYPE_IRQ_INTERFACE,
                             (Object **) &sad->irqdev, irq_check,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
}

static void smbus_alert_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    SMBusDeviceClass *sc = SMBUS_DEVICE_CLASS(oc);
    I2CSlaveClass *ic = I2C_SLAVE_CLASS(oc);
    SMBusAlertDeviceClass *sadc = SMBUS_ALERT_DEVICE_CLASS(oc);

    dc->realize = smbus_alert_realize;
    sc->receive_byte = alert_receive_byte;
    ic->event_check = smbus_alert_event;
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

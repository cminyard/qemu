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

#include "hw/irqif.h"
#include "hw/i2c/smbus_slave.h"

#ifndef HW_I2C_SMBUS_ALERT
#define HW_I2C_SMBUS_ALERT

#define SMBUS_ALERT_QLEN 256

typedef struct SMBusAlertDevice {
    SMBusDevice parent;

    IRQInterface *irqdev;
    qemu_irq *irq;

    uint32_t qlen;
    uint8_t *q;
} SMBusAlertDevice;

typedef struct SMBusAlertDeviceClass {
    SMBusDeviceClass parent;

    void (*alert)(SMBusAlertDevice *sad, uint8_t devaddr);
} SMBusAlertDeviceClass;

#define TYPE_SMBUS_ALERT_DEVICE "smbus-alert"
#define SMBUS_ALERT_DEVICE(obj) OBJECT_CHECK(SMBusAlertDevice, (obj),    \
                                             TYPE_SMBUS_ALERT_DEVICE)
#define SMBUS_ALERT_DEVICE_CLASS(obj) OBJECT_CLASS_CHECK(SMBusAlertDeviceClass,\
                                             (obj),            \
                                             TYPE_SMBUS_ALERT_DEVICE)
#define SMBUS_ALERT_DEVICE_GET_CLASS(obj) \
                OBJECT_GET_CLASS(SMBusAlertDeviceClass,  (obj),            \
                                 TYPE_SMBUS_ALERT_DEVICE)

#endif /* HW_I2C_SMBUS_ALERT */

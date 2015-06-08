/*
 * Interrupt Interface class
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

#ifndef HW_IRQIF_H
#define HW_IRQIF_H

#include "qom/object.h"
#include "hw/irq.h"

#define TYPE_IRQ_INTERFACE "irq-interface"
#define IRQ_INTERFACE(obj) \
     INTERFACE_CHECK(IRQInterface, (obj), TYPE_IRQ_INTERFACE)
#define IRQ_INTERFACE_CLASS(class) \
     OBJECT_CLASS_CHECK(IRQInterfaceClass, (class), TYPE_IRQ_INTERFACE)
#define IRQ_INTERFACE_GET_CLASS(class) \
     OBJECT_GET_CLASS(IRQInterfaceClass, (class), TYPE_IRQ_INTERFACE)

typedef struct IRQInterface {
    Object parent;
} IRQInterface;

typedef struct IRQInterfaceClass {
    InterfaceClass parent;

    qemu_irq *(*get_irq)(struct IRQInterface *ii);
} IRQInterfaceClass;

#endif /* HW_IRQIF_H */

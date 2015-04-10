/*
 * QEMU IPMI emulation
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
#include "ipmi.h"
#include "sysemu/sysemu.h"
#include "qmp-commands.h"

static int ipmi_do_hw_op(IPMIInterface *s, enum ipmi_op op, int checkonly)
{
    switch (op) {
    case IPMI_RESET_CHASSIS:
        if (checkonly) {
            return 0;
        }
        qemu_system_reset_request();
        return 0;

    case IPMI_POWEROFF_CHASSIS:
        if (checkonly) {
            return 0;
        }
        qemu_system_powerdown_request();
        return 0;

    case IPMI_SEND_NMI:
        if (checkonly) {
            return 0;
        }
        qemu_mutex_lock_iothread();
        qmp_inject_nmi(NULL);
        qemu_mutex_unlock_iothread();
        return 0;

    case IPMI_POWERCYCLE_CHASSIS:
    case IPMI_PULSE_DIAG_IRQ:
    case IPMI_SHUTDOWN_VIA_ACPI_OVERTEMP:
    case IPMI_POWERON_CHASSIS:
    default:
        return IPMI_CC_COMMAND_NOT_SUPPORTED;
    }
}

static void ipmi_set_irq_enable(IPMIInterface *s, int val)
{
    s->irqs_enabled = val;
}

void ipmi_interface_reset(IPMIInterface *s)
{
    IPMIBmcClass *bk = IPMI_BMC_GET_CLASS(s->bmc);

    if (bk->handle_reset) {
        bk->handle_reset(s->bmc);
    }
}

void ipmi_interface_init(IPMIInterface *s, Error **errp)
{
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);

    if (k->init) {
        k->init(s, errp);
        if (*errp) {
            return;
        }
    }

    if (!s->slave_addr) {
        s->slave_addr = 0x20;
    }
}

static void ipmi_interface_class_init(ObjectClass *class, void *data)
{
    IPMIInterfaceClass *ik = IPMI_INTERFACE_CLASS(class);

    ik->do_hw_op = ipmi_do_hw_op;
    ik->set_irq_enable = ipmi_set_irq_enable;
}

static TypeInfo ipmi_interface_type_info = {
    .name = TYPE_IPMI_INTERFACE,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(IPMIInterface),
    .abstract = true,
    .class_size = sizeof(IPMIInterfaceClass),
    .class_init = ipmi_interface_class_init,
};

void ipmi_bmc_init(IPMIBmc *s, Error **errp)
{
    IPMIBmcClass *k = IPMI_BMC_GET_CLASS(s);

    if (k->init) {
        k->init(s, errp);
    }
}

const VMStateDescription vmstate_IPMIInterface = {
    .name = TYPE_IPMI_INTERFACE,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_BOOL(obf_irq_set, IPMIInterface),
        VMSTATE_BOOL(atn_irq_set, IPMIInterface),
        VMSTATE_BOOL(use_irq, IPMIInterface),
        VMSTATE_BOOL(irqs_enabled, IPMIInterface),
        VMSTATE_UINT32(outpos, IPMIInterface),
        VMSTATE_UINT32(outlen, IPMIInterface),
        VMSTATE_VBUFFER_UINT32(inmsg, IPMIInterface, 1, NULL, 0, inlen),
        VMSTATE_BOOL(write_end, IPMIInterface),
        VMSTATE_END_OF_LIST()
    }
};

static TypeInfo ipmi_bmc_type_info = {
    .name = TYPE_IPMI_BMC,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(IPMIBmc),
    .abstract = true,
    .class_size = sizeof(IPMIBmcClass),
};

static void ipmi_register_types(void)
{
    type_register_static(&ipmi_interface_type_info);
    type_register_static(&ipmi_bmc_type_info);
}

type_init(ipmi_register_types)

struct fw_entry_handlers {
    IPMIFwHandler handler;
    void *opaque;
    QSLIST_ENTRY(fw_entry_handlers) next;
};
static QSLIST_HEAD(, fw_entry_handlers) fw_entries;

void ipmi_add_fwinfo(IPMIFwInfo *info)
{
    struct fw_entry_handlers *e;

    QSLIST_FOREACH(e, &fw_entries, next) {
        e->handler(info, e->opaque);
    }
}

void ipmi_register_fwinfo_handler(IPMIFwHandler handler, void *opaque)
{
    struct fw_entry_handlers *e = g_malloc(sizeof(*e));

    e->handler = handler;
    e->opaque = opaque;
    QSLIST_INSERT_HEAD(&fw_entries, e, next);
}

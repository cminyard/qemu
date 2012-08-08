/*
 * QEMU IPMI BT emulation
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

#define TYPE_IPMI_INTERFACE_BT TYPE_IPMI_INTERFACE_PREFIX "bt"
#define IPMI_INTERFACE_BT(obj) OBJECT_CHECK(IPMIBtInterface, (obj), \
                                        TYPE_IPMI_INTERFACE_BT)

/* Control register */
#define IPMI_BT_CLR_WR_BIT        0
#define IPMI_BT_CLR_RD_BIT        1
#define IPMI_BT_H2B_ATN_BIT        2
#define IPMI_BT_B2H_ATN_BIT        3
#define IPMI_BT_SMS_ATN_BIT        4
#define IPMI_BT_HBUSY_BIT        6
#define IPMI_BT_BBUSY_BIT        7

#define IPMI_BT_CLR_WR_MASK        (1 << IPMI_BT_CLR_WR_BIT)
#define IPMI_BT_GET_CLR_WR(d)      (((d) >> IPMI_BT_CLR_WR_BIT) & 0x1)
#define IPMI_BT_SET_CLR_WR(d, v)   (d) = (((d) & ~IPMI_BT_CLR_WR_MASK) | \
                                       (((v & 1) << IPMI_BT_CLR_WR_BIT)))

#define IPMI_BT_CLR_RD_MASK        (1 << IPMI_BT_CLR_RD_BIT)
#define IPMI_BT_GET_CLR_RD(d)      (((d) >> IPMI_BT_CLR_RD_BIT) & 0x1)
#define IPMI_BT_SET_CLR_RD(d, v)   (d) = (((d) & ~IPMI_BT_CLR_RD_MASK) | \
                                       (((v & 1) << IPMI_BT_CLR_RD_BIT)))

#define IPMI_BT_H2B_ATN_MASK       (1 << IPMI_BT_H2B_ATN_BIT)
#define IPMI_BT_GET_H2B_ATN(d)     (((d) >> IPMI_BT_H2B_ATN_BIT) & 0x1)
#define IPMI_BT_SET_H2B_ATN(d, v)  (d) = (((d) & ~IPMI_BT_H2B_ATN_MASK) | \
                                        (((v & 1) << IPMI_BT_H2B_ATN_BIT)))

#define IPMI_BT_B2H_ATN_MASK       (1 << IPMI_BT_B2H_ATN_BIT)
#define IPMI_BT_GET_B2H_ATN(d)     (((d) >> IPMI_BT_B2H_ATN_BIT) & 0x1)
#define IPMI_BT_SET_B2H_ATN(d, v)  (d) = (((d) & ~IPMI_BT_B2H_ATN_MASK) | \
                                        (((v & 1) << IPMI_BT_B2H_ATN_BIT)))

#define IPMI_BT_SMS_ATN_MASK       (1 << IPMI_BT_SMS_ATN_BIT)
#define IPMI_BT_GET_SMS_ATN(d)     (((d) >> IPMI_BT_SMS_ATN_BIT) & 0x1)
#define IPMI_BT_SET_SMS_ATN(d, v)  (d) = (((d) & ~IPMI_BT_SMS_ATN_MASK) | \
                                        (((v & 1) << IPMI_BT_SMS_ATN_BIT)))

#define IPMI_BT_HBUSY_MASK         (1 << IPMI_BT_HBUSY_BIT)
#define IPMI_BT_GET_HBUSY(d)       (((d) >> IPMI_BT_HBUSY_BIT) & 0x1)
#define IPMI_BT_SET_HBUSY(d, v)    (d) = (((d) & ~IPMI_BT_HBUSY_MASK) | \
                                       (((v & 1) << IPMI_BT_HBUSY_BIT)))

#define IPMI_BT_BBUSY_MASK         (1 << IPMI_BT_BBUSY_BIT)
#define IPMI_BT_GET_BBUSY(d)       (((d) >> IPMI_BT_BBUSY_BIT) & 0x1)
#define IPMI_BT_SET_BBUSY(d, v)    (d) = (((d) & ~IPMI_BT_BBUSY_MASK) | \
                                       (((v & 1) << IPMI_BT_BBUSY_BIT)))


/* Mask register */
#define IPMI_BT_B2H_IRQ_EN_BIT     0
#define IPMI_BT_B2H_IRQ_BIT        1

#define IPMI_BT_B2H_IRQ_EN_MASK      (1 << IPMI_BT_B2H_IRQ_EN_BIT)
#define IPMI_BT_GET_B2H_IRQ_EN(d)    (((d) >> IPMI_BT_B2H_IRQ_EN_BIT) & 0x1)
#define IPMI_BT_SET_B2H_IRQ_EN(d, v) (d) = (((d) & ~IPMI_BT_B2H_IRQ_EN_MASK) | \
                                        (((v & 1) << IPMI_BT_B2H_IRQ_EN_BIT)))

#define IPMI_BT_B2H_IRQ_MASK         (1 << IPMI_BT_B2H_IRQ_BIT)
#define IPMI_BT_GET_B2H_IRQ(d)       (((d) >> IPMI_BT_B2H_IRQ_BIT) & 0x1)
#define IPMI_BT_SET_B2H_IRQ(d, v)    (d) = (((d) & ~IPMI_BT_B2H_IRQ_MASK) | \
                                        (((v & 1) << IPMI_BT_B2H_IRQ_BIT)))

typedef struct IPMIBtInterface {
    IPMIInterface intf;

    uint8_t control_reg;
    uint8_t mask_reg;

    /*
     * This is a response number that we send with the command to make
     * sure that the response matches the command.
     */
    uint8_t waiting_rsp;
    uint8_t waiting_seq;
} IPMIBtInterface;

#define IPMI_CMD_GET_BT_INTF_CAP        0x36

static void ipmi_bt_handle_event(IPMIInterface *s)
{
    IPMIBtInterface *bt = IPMI_INTERFACE_BT(s);

    if (s->inlen < 4) {
        goto out;
    }
    /* Note that overruns are handled by handle_command */
    if (s->inmsg[0] != (s->inlen - 1)) {
        /* Length mismatch, just ignore. */
        IPMI_BT_SET_BBUSY(bt->control_reg, 1);
        s->inlen = 0;
        goto out;
    }
    if ((s->inmsg[1] == (IPMI_NETFN_APP << 2)) &&
                        (s->inmsg[3] == IPMI_CMD_GET_BT_INTF_CAP)) {
        /* We handle this one ourselves. */
        s->outmsg[0] = 9;
        s->outmsg[1] = s->inmsg[1] | 0x04;
        s->outmsg[2] = s->inmsg[2];
        s->outmsg[3] = s->inmsg[3];
        s->outmsg[4] = 0;
        s->outmsg[5] = 1; /* Only support 1 outstanding request. */
        if (sizeof(s->inmsg) > 0xff) { /* Input buffer size */
            s->outmsg[6] = 0xff;
        } else {
            s->outmsg[6] = (unsigned char) sizeof(s->inmsg);
        }
        if (sizeof(s->outmsg) > 0xff) { /* Output buffer size */
            s->outmsg[7] = 0xff;
        } else {
            s->outmsg[7] = (unsigned char) sizeof(s->outmsg);
        }
        s->outmsg[8] = 10; /* Max request to response time */
        s->outmsg[9] = 0; /* Don't recommend retries */
        s->outlen = 10;
        IPMI_BT_SET_BBUSY(bt->control_reg, 0);
        IPMI_BT_SET_B2H_ATN(bt->control_reg, 1);
        if (s->use_irq && s->irqs_enabled &&
                !IPMI_BT_GET_B2H_IRQ(bt->mask_reg) &&
                IPMI_BT_GET_B2H_IRQ_EN(bt->mask_reg)) {
            IPMI_BT_SET_B2H_IRQ(bt->mask_reg, 1);
            qemu_irq_raise(s->irq);
        }
        goto out;
    }
    bt->waiting_seq = s->inmsg[2];
    s->inmsg[2] = s->inmsg[1];
    {
        IPMIBmcClass *bk = IPMI_BMC_GET_CLASS(s->bmc);
        bk->handle_command(s->bmc, s->inmsg + 2, s->inlen - 2, sizeof(s->inmsg),
                           bt->waiting_rsp);
    }
 out:
    return;
}

static void ipmi_bt_handle_rsp(IPMIInterface *s, uint8_t msg_id,
                                unsigned char *rsp, unsigned int rsp_len)
{
    IPMIBtInterface *bt = IPMI_INTERFACE_BT(s);

    if (bt->waiting_rsp == msg_id) {
        bt->waiting_rsp++;
        if (rsp_len > (sizeof(s->outmsg) - 2)) {
            s->outmsg[0] = 4;
            s->outmsg[1] = rsp[0];
            s->outmsg[2] = bt->waiting_seq;
            s->outmsg[3] = rsp[1];
            s->outmsg[4] = IPMI_CC_CANNOT_RETURN_REQ_NUM_BYTES;
            s->outlen = 5;
        } else {
            s->outmsg[0] = rsp_len + 1;
            s->outmsg[1] = rsp[0];
            s->outmsg[2] = bt->waiting_seq;
            memcpy(s->outmsg + 3, rsp + 1, rsp_len - 1);
            s->outlen = rsp_len + 2;
        }
        IPMI_BT_SET_BBUSY(bt->control_reg, 0);
        IPMI_BT_SET_B2H_ATN(bt->control_reg, 1);
        if (s->use_irq && s->irqs_enabled &&
                !IPMI_BT_GET_B2H_IRQ(bt->mask_reg) &&
                IPMI_BT_GET_B2H_IRQ_EN(bt->mask_reg)) {
            IPMI_BT_SET_B2H_IRQ(bt->mask_reg, 1);
            qemu_irq_raise(s->irq);
        }
    }
}


static uint64_t ipmi_bt_ioport_read(void *opaque, hwaddr addr, unsigned size)
{
    IPMIBtInterface *bt = opaque;
    IPMIInterface *s = &bt->intf;
    uint32_t ret = 0xff;

    switch (addr & 3) {
    case 0:
        ret = bt->control_reg;
        break;
    case 1:
        if (s->outpos < s->outlen) {
            ret = s->outmsg[s->outpos];
            s->outpos++;
            if (s->outpos == s->outlen) {
                s->outpos = 0;
                s->outlen = 0;
            }
        } else {
            ret = 0xff;
        }
        break;
    case 2:
        ret = bt->mask_reg;
        break;
    }
    return ret;
}

static void ipmi_bt_ioport_write(void *opaque, hwaddr addr, uint64_t val,
                                 unsigned size)
{
    IPMIBtInterface *bt = opaque;
    IPMIInterface *s = &bt->intf;

    switch (addr & 3) {
    case 0:
        if (IPMI_BT_GET_CLR_WR(val)) {
            s->inlen = 0;
        }
        if (IPMI_BT_GET_CLR_RD(val)) {
            s->outpos = 0;
        }
        if (IPMI_BT_GET_B2H_ATN(val)) {
            IPMI_BT_SET_B2H_ATN(bt->control_reg, 0);
        }
        if (IPMI_BT_GET_SMS_ATN(val)) {
            IPMI_BT_SET_SMS_ATN(bt->control_reg, 0);
        }
        if (IPMI_BT_GET_HBUSY(val)) {
            /* Toggle */
            IPMI_BT_SET_HBUSY(bt->control_reg,
                              !IPMI_BT_GET_HBUSY(bt->control_reg));
        }
        if (IPMI_BT_GET_H2B_ATN(val)) {
            IPMI_BT_SET_BBUSY(bt->control_reg, 1);
            ipmi_signal(s);
        }
        break;

    case 1:
        if (s->inlen < sizeof(s->inmsg)) {
            s->inmsg[s->inlen] = val;
        }
        s->inlen++;
        break;

    case 2:
        if (IPMI_BT_GET_B2H_IRQ_EN(val) !=
                        IPMI_BT_GET_B2H_IRQ_EN(bt->mask_reg)) {
            if (IPMI_BT_GET_B2H_IRQ_EN(val)) {
                if (IPMI_BT_GET_B2H_ATN(bt->control_reg) ||
                        IPMI_BT_GET_SMS_ATN(bt->control_reg)) {
                    IPMI_BT_SET_B2H_IRQ(bt->mask_reg, 1);
                    qemu_irq_raise(s->irq);
                }
                IPMI_BT_SET_B2H_IRQ_EN(bt->mask_reg, 1);
            } else {
                if (IPMI_BT_GET_B2H_IRQ(bt->mask_reg)) {
                    IPMI_BT_SET_B2H_IRQ(bt->mask_reg, 0);
                    qemu_irq_lower(s->irq);
                }
                IPMI_BT_SET_B2H_IRQ_EN(bt->mask_reg, 0);
            }
        }
        if (IPMI_BT_GET_B2H_IRQ(val) && IPMI_BT_GET_B2H_IRQ(bt->mask_reg)) {
            IPMI_BT_SET_B2H_IRQ(bt->mask_reg, 0);
            qemu_irq_lower(s->irq);
        }
        break;
    }
}

static const MemoryRegionOps ipmi_bt_io_ops = {
    .read = ipmi_bt_ioport_read,
    .write = ipmi_bt_ioport_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void ipmi_bt_set_atn(IPMIInterface *s, int val, int irq)
{
    IPMIBtInterface *bt = IPMI_INTERFACE_BT(s);

    if (!!val == IPMI_BT_GET_SMS_ATN(bt->control_reg)) {
        return;
    }

    IPMI_BT_SET_SMS_ATN(bt->control_reg, val);
    if (val) {
        if (irq && s->use_irq && s->irqs_enabled &&
                !IPMI_BT_GET_B2H_ATN(bt->control_reg) &&
                IPMI_BT_GET_B2H_IRQ_EN(bt->mask_reg)) {
            IPMI_BT_SET_B2H_IRQ(bt->mask_reg, 1);
            qemu_irq_raise(s->irq);
        }
    } else {
        if (!IPMI_BT_GET_B2H_ATN(bt->control_reg) &&
                IPMI_BT_GET_B2H_IRQ(bt->mask_reg)) {
            IPMI_BT_SET_B2H_IRQ(bt->mask_reg, 0);
            qemu_irq_lower(s->irq);
        }
    }
}

static void ipmi_bt_handle_reset(IPMIInterface *s, bool is_cold)
{
    IPMIBtInterface *bt = IPMI_INTERFACE_BT(s);

    if (is_cold) {
        /* Disable the BT interrupt on reset */
        if (IPMI_BT_GET_B2H_IRQ(bt->mask_reg)) {
            IPMI_BT_SET_B2H_IRQ(bt->mask_reg, 0);
            qemu_irq_lower(s->irq);
        }
        IPMI_BT_SET_B2H_IRQ_EN(bt->mask_reg, 0);
    }
}

static const VMStateDescription vmstate_ipmi_bt = {
    .name = TYPE_IPMI_INTERFACE_BT,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT8(control_reg, IPMIBtInterface),
        VMSTATE_UINT8(mask_reg, IPMIBtInterface),
        VMSTATE_UINT8(waiting_rsp, IPMIBtInterface),
        VMSTATE_UINT8(waiting_seq, IPMIBtInterface),
        VMSTATE_END_OF_LIST()
    }
};

static void ipmi_bt_init(IPMIInterface *s, Error **errp)
{
    IPMIBtInterface *bt = IPMI_INTERFACE_BT(s);

    if (!s->io_base) {
        s->io_base = 0xe4;
    }
    s->io_length = 3;

    memory_region_init_io(&s->io, NULL, &ipmi_bt_io_ops, bt, "ipmi-bt", 3);
    vmstate_register(NULL, 0, &vmstate_ipmi_bt, bt);
}

static void ipmi_bt_class_init(ObjectClass *klass, void *data)
{
    IPMIInterfaceClass *k = IPMI_INTERFACE_CLASS(klass);

    k->init = ipmi_bt_init;
    k->smbios_type = IPMI_SMBIOS_BT;
    k->set_atn = ipmi_bt_set_atn;
    k->handle_rsp = ipmi_bt_handle_rsp;
    k->handle_if_event = ipmi_bt_handle_event;
    k->reset = ipmi_bt_handle_reset;
}

static const TypeInfo ipmi_bt_type = {
    .name          = TYPE_IPMI_INTERFACE_BT,
    .parent        = TYPE_IPMI_INTERFACE,
    .instance_size = sizeof(IPMIBtInterface),
    .class_init    = ipmi_bt_class_init,
};

static void ipmi_bt_register_types(void)
{
    type_register_static(&ipmi_bt_type);
}

type_init(ipmi_bt_register_types)

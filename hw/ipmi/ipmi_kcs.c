/*
 * QEMU IPMI KCS emulation
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

#define TYPE_IPMI_INTERFACE_KCS TYPE_IPMI_INTERFACE_PREFIX "kcs"
#define IPMI_INTERFACE_KCS(obj) OBJECT_CHECK(IPMIKcsInterface, (obj), \
                                        TYPE_IPMI_INTERFACE_KCS)

#define IPMI_KCS_OBF_BIT        0
#define IPMI_KCS_IBF_BIT        1
#define IPMI_KCS_SMS_ATN_BIT    2
#define IPMI_KCS_CD_BIT         3

#define IPMI_KCS_OBF_MASK          (1 << IPMI_KCS_OBF_BIT)
#define IPMI_KCS_GET_OBF(d)        (((d) >> IPMI_KCS_OBF_BIT) & 0x1)
#define IPMI_KCS_SET_OBF(d, v)     (d) = (((d) & ~IPMI_KCS_OBF_MASK) | \
                                       (((v) & 1) << IPMI_KCS_OBF_BIT))
#define IPMI_KCS_IBF_MASK          (1 << IPMI_KCS_IBF_BIT)
#define IPMI_KCS_GET_IBF(d)        (((d) >> IPMI_KCS_IBF_BIT) & 0x1)
#define IPMI_KCS_SET_IBF(d, v)     (d) = (((d) & ~IPMI_KCS_IBF_MASK) | \
                                       (((v) & 1) << IPMI_KCS_IBF_BIT))
#define IPMI_KCS_SMS_ATN_MASK      (1 << IPMI_KCS_SMS_ATN_BIT)
#define IPMI_KCS_GET_SMS_ATN(d)    (((d) >> IPMI_KCS_SMS_ATN_BIT) & 0x1)
#define IPMI_KCS_SET_SMS_ATN(d, v) (d) = (((d) & ~IPMI_KCS_SMS_ATN_MASK) | \
                                       (((v) & 1) << IPMI_KCS_SMS_ATN_BIT))
#define IPMI_KCS_CD_MASK           (1 << IPMI_KCS_CD_BIT)
#define IPMI_KCS_GET_CD(d)         (((d) >> IPMI_KCS_CD_BIT) & 0x1)
#define IPMI_KCS_SET_CD(d, v)      (d) = (((d) & ~IPMI_KCS_CD_MASK) | \
                                       (((v) & 1) << IPMI_KCS_CD_BIT))

#define IPMI_KCS_IDLE_STATE        0
#define IPMI_KCS_READ_STATE        1
#define IPMI_KCS_WRITE_STATE       2
#define IPMI_KCS_ERROR_STATE       3

#define IPMI_KCS_GET_STATE(d)    (((d) >> 6) & 0x3)
#define IPMI_KCS_SET_STATE(d, v) ((d) = ((d) & ~0xc0) | (((v) & 0x3) << 6))

#define IPMI_KCS_ABORT_STATUS_CMD       0x60
#define IPMI_KCS_WRITE_START_CMD        0x61
#define IPMI_KCS_WRITE_END_CMD          0x62
#define IPMI_KCS_READ_CMD               0x68

#define IPMI_KCS_STATUS_NO_ERR          0x00
#define IPMI_KCS_STATUS_ABORTED_ERR     0x01
#define IPMI_KCS_STATUS_BAD_CC_ERR      0x02
#define IPMI_KCS_STATUS_LENGTH_ERR      0x06

typedef struct IPMIKcsInterface {
    IPMIInterface intf;

    uint8_t status_reg;
    uint8_t data_out_reg;

    int16_t data_in_reg; /* -1 means not written */
    int16_t cmd_reg;

    /*
     * This is a response number that we send with the command to make
     * sure that the response matches the command.
     */
    uint8_t waiting_rsp;
} IPMIKcsInterface;

#define SET_OBF() \
    do {                                                                      \
        IPMI_KCS_SET_OBF(kcs->status_reg, 1);                                 \
        if (s->use_irq && s->irqs_enabled && !s->obf_irq_set) {               \
            s->obf_irq_set = 1;                                               \
            if (!s->atn_irq_set) {                                            \
                qemu_irq_raise(s->irq);                                       \
            }                                                                 \
        }                                                                     \
    } while (0)

static void ipmi_kcs_handle_event(IPMIInterface *s)
{
    IPMIKcsInterface *kcs = IPMI_INTERFACE_KCS(s);

    if (kcs->cmd_reg == IPMI_KCS_ABORT_STATUS_CMD) {
        if (IPMI_KCS_GET_STATE(kcs->status_reg) != IPMI_KCS_ERROR_STATE) {
            kcs->waiting_rsp++; /* Invalidate the message */
            s->outmsg[0] = IPMI_KCS_STATUS_ABORTED_ERR;
            s->outlen = 1;
            s->outpos = 0;
            IPMI_KCS_SET_STATE(kcs->status_reg, IPMI_KCS_ERROR_STATE);
            SET_OBF();
        }
        goto out;
    }

    switch (IPMI_KCS_GET_STATE(kcs->status_reg)) {
    case IPMI_KCS_IDLE_STATE:
        if (kcs->cmd_reg == IPMI_KCS_WRITE_START_CMD) {
            IPMI_KCS_SET_STATE(kcs->status_reg, IPMI_KCS_WRITE_STATE);
            kcs->cmd_reg = -1;
            s->write_end = 0;
            s->inlen = 0;
            SET_OBF();
        }
        break;

    case IPMI_KCS_READ_STATE:
    handle_read:
        if (s->outpos >= s->outlen) {
            IPMI_KCS_SET_STATE(kcs->status_reg, IPMI_KCS_IDLE_STATE);
            SET_OBF();
        } else if (kcs->data_in_reg == IPMI_KCS_READ_CMD) {
            kcs->data_out_reg = s->outmsg[s->outpos];
            s->outpos++;
            SET_OBF();
        } else {
            s->outmsg[0] = IPMI_KCS_STATUS_BAD_CC_ERR;
            s->outlen = 1;
            s->outpos = 0;
            IPMI_KCS_SET_STATE(kcs->status_reg, IPMI_KCS_ERROR_STATE);
            SET_OBF();
            goto out;
        }
        break;

    case IPMI_KCS_WRITE_STATE:
        if (kcs->data_in_reg != -1) {
            /*
             * Don't worry about input overrun here, that will be
             * handled in the BMC.
             */
            if (s->inlen < sizeof(s->inmsg)) {
                s->inmsg[s->inlen] = kcs->data_in_reg;
            }
            s->inlen++;
        }
        if (s->write_end) {
            IPMIBmcClass *bk = IPMI_BMC_GET_CLASS(s->bmc);
            s->outlen = 0;
            s->write_end = 0;
            s->outpos = 0;
            bk->handle_command(s->bmc, s->inmsg, s->inlen, sizeof(s->inmsg),
                               kcs->waiting_rsp);
            goto out_noibf;
        } else if (kcs->cmd_reg == IPMI_KCS_WRITE_END_CMD) {
            kcs->cmd_reg = -1;
            s->write_end = 1;
        }
        SET_OBF();
        break;

    case IPMI_KCS_ERROR_STATE:
        if (kcs->data_in_reg != -1) {
            IPMI_KCS_SET_STATE(kcs->status_reg, IPMI_KCS_READ_STATE);
            kcs->data_in_reg = IPMI_KCS_READ_CMD;
            goto handle_read;
        }
        break;
    }

    if (kcs->cmd_reg != -1) {
        /* Got an invalid command */
        s->outmsg[0] = IPMI_KCS_STATUS_BAD_CC_ERR;
        s->outlen = 1;
        s->outpos = 0;
        IPMI_KCS_SET_STATE(kcs->status_reg, IPMI_KCS_ERROR_STATE);
    }

 out:
    kcs->cmd_reg = -1;
    kcs->data_in_reg = -1;
    IPMI_KCS_SET_IBF(kcs->status_reg, 0);
 out_noibf:
    return;
}

static void ipmi_kcs_handle_rsp(IPMIInterface *s, uint8_t msg_id,
                                unsigned char *rsp, unsigned int rsp_len)
{
    IPMIKcsInterface *kcs = IPMI_INTERFACE_KCS(s);

    if (kcs->waiting_rsp == msg_id) {
        kcs->waiting_rsp++;
        if (rsp_len > sizeof(s->outmsg)) {
            s->outmsg[0] = rsp[0];
            s->outmsg[1] = rsp[1];
            s->outmsg[2] = IPMI_CC_CANNOT_RETURN_REQ_NUM_BYTES;
            s->outlen = 3;
        } else {
            memcpy(s->outmsg, rsp, rsp_len);
            s->outlen = rsp_len;
        }
        IPMI_KCS_SET_STATE(kcs->status_reg, IPMI_KCS_READ_STATE);
        kcs->data_in_reg = IPMI_KCS_READ_CMD;
        ipmi_signal(&kcs->intf);
    }
}


static uint64_t ipmi_kcs_ioport_read(void *opaque, hwaddr addr, unsigned size)
{
    IPMIKcsInterface *kcs = opaque;
    uint32_t ret;

    switch (addr & 1) {
    case 0:
        ret = kcs->data_out_reg;
        IPMI_KCS_SET_OBF(kcs->status_reg, 0);
        if (kcs->intf.obf_irq_set) {
            kcs->intf.obf_irq_set = 0;
            if (!kcs->intf.atn_irq_set) {
                qemu_irq_lower(kcs->intf.irq);
            }
        }
        break;
    case 1:
        ret = kcs->status_reg;
        if (kcs->intf.atn_irq_set) {
            kcs->intf.atn_irq_set = 0;
            if (!kcs->intf.obf_irq_set) {
                qemu_irq_lower(kcs->intf.irq);
            }
        }
        break;
    }
    return ret;
}

static void ipmi_kcs_ioport_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    IPMIKcsInterface *kcs = opaque;
    IPMIInterface *s = &kcs->intf;

    if (IPMI_KCS_GET_IBF(kcs->status_reg)) {
        return;
    }

    switch (addr & 1) {
    case 0:
        kcs->data_in_reg = val;
        break;

    case 1:
        kcs->cmd_reg = val;
        break;
    }
    IPMI_KCS_SET_IBF(kcs->status_reg, 1);
    ipmi_signal(s);
}

const MemoryRegionOps ipmi_kcs_io_ops = {
    .read = ipmi_kcs_ioport_read,
    .write = ipmi_kcs_ioport_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void ipmi_kcs_set_atn(IPMIInterface *s, int val, int irq)
{
    IPMIKcsInterface *kcs = IPMI_INTERFACE_KCS(s);

    IPMI_KCS_SET_SMS_ATN(kcs->status_reg, val);
    if (val) {
        if (irq && !s->atn_irq_set && s->use_irq && s->irqs_enabled) {
            s->atn_irq_set = 1;
            if (!s->obf_irq_set) {
                qemu_irq_raise(s->irq);
            }
        }
    } else {
        if (s->atn_irq_set) {
            s->atn_irq_set = 0;
            if (!s->obf_irq_set) {
                qemu_irq_lower(s->irq);
            }
        }
    }
}

static void ipmi_kcs_init(IPMIInterface *s, Error **errp)
{
    IPMIKcsInterface *kcs = IPMI_INTERFACE_KCS(s);

    if (!s->io_base) {
        s->io_base = 0xca2;
    }
    s->io_length = 2;

    memory_region_init_io(&s->io, NULL, &ipmi_kcs_io_ops, kcs, "ipmi-kcs", 2);
}

static void ipmi_kcs_class_init(ObjectClass *class, void *data)
{
    IPMIInterfaceClass *k = IPMI_INTERFACE_CLASS(class);

    k->init = ipmi_kcs_init;
    k->smbios_type = IPMI_SMBIOS_KCS;
    k->set_atn = ipmi_kcs_set_atn;
    k->handle_rsp = ipmi_kcs_handle_rsp;
    k->handle_if_event = ipmi_kcs_handle_event;
}

static const TypeInfo ipmi_kcs_type = {
    .name          = TYPE_IPMI_INTERFACE_KCS,
    .parent        = TYPE_IPMI_INTERFACE,
    .instance_size = sizeof(IPMIKcsInterface),
    .class_init    = ipmi_kcs_class_init,
};

static void ipmi_kcs_register_types(void)
{
    type_register_static(&ipmi_kcs_type);
}

type_init(ipmi_kcs_register_types)

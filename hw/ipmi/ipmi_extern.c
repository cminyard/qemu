/*
 * IPMI BMC external connection
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

/*
 * This is designed to connect with OpenIPMI's lanserv serial interface
 * using the "VM" connection type.  See that for details.
 */

#include <stdint.h>
#include "qemu/timer.h"
#include "sysemu/char.h"
#include "ipmi.h"

#define VM_MSG_CHAR        0xA0 /* Marks end of message */
#define VM_CMD_CHAR        0xA1 /* Marks end of a command */
#define VM_ESCAPE_CHAR     0xAA /* Set bit 4 from the next byte to 0 */

#define VM_PROTOCOL_VERSION        1
#define VM_CMD_VERSION             0xff /* A version number byte follows */
#define VM_CMD_NOATTN              0x00
#define VM_CMD_ATTN                0x01
#define VM_CMD_ATTN_IRQ            0x02
#define VM_CMD_POWEROFF            0x03
#define VM_CMD_RESET               0x04
#define VM_CMD_ENABLE_IRQ          0x05 /* Enable/disable the messaging irq */
#define VM_CMD_DISABLE_IRQ         0x06
#define VM_CMD_SEND_NMI            0x07
#define VM_CMD_CAPABILITIES        0x08
#define   VM_CAPABILITIES_POWER    0x01
#define   VM_CAPABILITIES_RESET    0x02
#define   VM_CAPABILITIES_IRQ      0x04
#define   VM_CAPABILITIES_NMI      0x08
#define   VM_CAPABILITIES_ATTN     0x10

#define IPMI_BMC_EXTERN(obj) OBJECT_CHECK(IPMIExternBmc, (obj), \
                                        TYPE_IPMI_BMC_EXTERN)
typedef struct IPMIExternBmc {
    IPMIBmc parent;

    int connected;
    int is_listen;

    unsigned char inbuf[MAX_IPMI_MSG_SIZE + 2];
    unsigned int inpos;
    int in_escape;
    int in_too_many;
    int waiting_rsp;
    int sending_cmd;

    unsigned char outbuf[(MAX_IPMI_MSG_SIZE + 2) * 2 + 1];
    unsigned int outpos;
    unsigned int outlen;

    struct QEMUTimer *extern_timer;

    /* A reset event is pending to be sent upstream. */
    bool send_reset;
} IPMIExternBmc;

static int can_receive(void *opaque);
static void receive(void *opaque, const uint8_t *buf, int size);
static void chr_event(void *opaque, int event);

static unsigned char
ipmb_checksum(const unsigned char *data, int size, unsigned char start)
{
        unsigned char csum = start;

        for (; size > 0; size--, data++) {
                csum += *data;
        }
        return csum;
}

static void continue_send(IPMIExternBmc *es)
{
    if (es->outlen == 0) {
        goto check_reset;
    }
 send:
    es->outpos += qemu_chr_fe_write(es->parent.chr, es->outbuf + es->outpos,
                                    es->outlen - es->outpos);
    if (es->outpos < es->outlen) {
        /* Not fully transmitted, try again in a 10ms */
        timer_mod_ns(es->extern_timer,
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 10000000);
    } else {
        /* Sent */
        es->outlen = 0;
        es->outpos = 0;
        if (!es->sending_cmd) {
            es->waiting_rsp = 1;
        } else {
            es->sending_cmd = 0;
        }
    check_reset:
        if (es->connected && es->send_reset) {
            /* Send the reset */
            es->outbuf[0] = VM_CMD_RESET;
            es->outbuf[1] = VM_CMD_CHAR;
            es->outlen = 2;
            es->outpos = 0;
            es->send_reset = 0;
            es->sending_cmd = 1;
            goto send;
        }

        if (es->waiting_rsp) {
            /* Make sure we get a response within 4 seconds. */
            timer_mod_ns(es->extern_timer,
                         qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 4000000000ULL);
        }
    }
    return;
}

static void extern_timeout(void *opaque)
{
    IPMIExternBmc *es = opaque;
    IPMIInterface *s = es->parent.intf;

    if (es->connected) {
        if (es->waiting_rsp && (es->outlen == 0)) {
            IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);
            /* The message response timed out, return an error. */
            es->waiting_rsp = 0;
            es->inbuf[1] = es->outbuf[1] | 0x04;
            es->inbuf[2] = es->outbuf[2];
            es->inbuf[3] = IPMI_CC_TIMEOUT;
            k->handle_rsp(s, es->outbuf[0], es->inbuf + 1, 3);
        } else {
            continue_send(es);
        }
    }
}

static void addchar(IPMIExternBmc *es, unsigned char ch)
{
    switch (ch) {
    case VM_MSG_CHAR:
    case VM_CMD_CHAR:
    case VM_ESCAPE_CHAR:
        es->outbuf[es->outlen] = VM_ESCAPE_CHAR;
        es->outlen++;
        ch |= 0x10;
        /* No break */

    default:
        es->outbuf[es->outlen] = ch;
        es->outlen++;
    }
}

static void ipmi_extern_handle_command(IPMIBmc *b,
                                       uint8_t *cmd, unsigned int cmd_len,
                                       unsigned int max_cmd_len,
                                       uint8_t msg_id)
{
    IPMIExternBmc *es = IPMI_BMC_EXTERN(b);
    IPMIInterface *s = es->parent.intf;
    uint8_t err = 0, csum;
    unsigned int i;

    if (es->outlen) {
        /* We already have a command queued.  Shouldn't ever happen. */
        fprintf(stderr, "IPMI KCS: Got command when not finished with the"
                " previous commmand\n");
        abort();
    }

    /* If it's too short or it was truncated, return an error. */
    if (cmd_len < 2) {
        err = IPMI_CC_REQUEST_DATA_LENGTH_INVALID;
    } else if ((cmd_len > max_cmd_len) || (cmd_len > MAX_IPMI_MSG_SIZE)) {
        err = IPMI_CC_REQUEST_DATA_TRUNCATED;
    } else if (!es->connected) {
        err = IPMI_CC_BMC_INIT_IN_PROGRESS;
    }
    if (err) {
        IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);
        unsigned char rsp[3];
        rsp[0] = cmd[0] | 0x04;
        rsp[1] = cmd[1];
        rsp[2] = err;
        es->waiting_rsp = 0;
        k->handle_rsp(s, msg_id, rsp, 3);
        goto out;
    }

    addchar(es, msg_id);
    for (i = 0; i < cmd_len; i++) {
        addchar(es, cmd[i]);
    }
    csum = ipmb_checksum(&msg_id, 1, 0);
    addchar(es, -ipmb_checksum(cmd, cmd_len, csum));

    es->outbuf[es->outlen] = VM_MSG_CHAR;
    es->outlen++;

    /* Start the transmit */
    continue_send(es);

 out:
    return;
}

static void handle_hw_op(IPMIExternBmc *es, unsigned char hw_op)
{
    IPMIInterface *s = es->parent.intf;
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);

    switch (hw_op) {
    case VM_CMD_VERSION:
        /* We only support one version at this time. */
        break;

    case VM_CMD_NOATTN:
        k->set_atn(s, 0, 0);
        break;

    case VM_CMD_ATTN:
        k->set_atn(s, 1, 0);
        break;

    case VM_CMD_ATTN_IRQ:
        k->set_atn(s, 1, 1);
        break;

    case VM_CMD_POWEROFF:
        k->do_hw_op(s, IPMI_POWEROFF_CHASSIS, 0);
        break;

    case VM_CMD_RESET:
        k->do_hw_op(s, IPMI_RESET_CHASSIS, 0);
        break;

    case VM_CMD_ENABLE_IRQ:
        k->set_irq_enable(s, 1);
        break;

    case VM_CMD_DISABLE_IRQ:
        k->set_irq_enable(s, 0);
        break;

    case VM_CMD_SEND_NMI:
        k->do_hw_op(s, IPMI_SEND_NMI, 0);
        break;
    }
}

static void handle_msg(IPMIExternBmc *es)
{
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(es->parent.intf);

    if (es->in_escape) {
        ipmi_debug("msg escape not ended\n");
        return;
    }
    if (es->inpos < 5) {
        ipmi_debug("msg too short\n");
        return;
    }
    if (es->in_too_many) {
        es->inbuf[3] = IPMI_CC_REQUEST_DATA_TRUNCATED;
        es->inpos = 4;
    } else if (ipmb_checksum(es->inbuf, es->inpos, 0) != 0) {
        ipmi_debug("msg checksum failure\n");
        return;
    } else {
        es->inpos--; /* Remove checkum */
    }

    timer_del(es->extern_timer);
    es->waiting_rsp = 0;
    k->handle_rsp(es->parent.intf, es->inbuf[0], es->inbuf + 1, es->inpos - 1);
}

static int can_receive(void *opaque)
{
    return 1;
}

static void receive(void *opaque, const uint8_t *buf, int size)
{
    IPMIExternBmc *es = opaque;
    int i;
    unsigned char hw_op;

    for (i = 0; i < size; i++) {
        unsigned char ch = buf[i];

        switch (ch) {
        case VM_MSG_CHAR:
            handle_msg(es);
            es->in_too_many = 0;
            es->inpos = 0;
            break;

        case VM_CMD_CHAR:
            if (es->in_too_many) {
                ipmi_debug("cmd in too many\n");
                es->in_too_many = 0;
                es->inpos = 0;
                break;
            }
            if (es->in_escape) {
                ipmi_debug("cmd in escape\n");
                es->in_too_many = 0;
                es->inpos = 0;
                es->in_escape = 0;
                break;
            }
            es->in_too_many = 0;
            if (es->inpos < 1) {
                break;
            }
            hw_op = es->inbuf[0];
            es->inpos = 0;
            goto out_hw_op;
            break;

        case VM_ESCAPE_CHAR:
            es->in_escape = 1;
            break;

        default:
            if (es->in_escape) {
                ch &= ~0x10;
                es->in_escape = 0;
            }
            if (es->in_too_many) {
                break;
            }
            if (es->inpos >= sizeof(es->inbuf)) {
                es->in_too_many = 1;
                break;
            }
            es->inbuf[es->inpos] = ch;
            es->inpos++;
            break;
        }
    }
    return;

 out_hw_op:
    handle_hw_op(es, hw_op);
}

static void chr_event(void *opaque, int event)
{
    IPMIExternBmc *es = opaque;
    IPMIInterface *s = es->parent.intf;
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);
    unsigned char v;

    switch (event) {
    case CHR_EVENT_OPENED:
        es->connected = 1;
        es->outpos = 0;
        es->outlen = 0;
        addchar(es, VM_CMD_VERSION);
        addchar(es, VM_PROTOCOL_VERSION);
        es->outbuf[es->outlen] = VM_CMD_CHAR;
        es->outlen++;
        addchar(es, VM_CMD_CAPABILITIES);
        v = VM_CAPABILITIES_IRQ | VM_CAPABILITIES_ATTN;
        if (k->do_hw_op(es->parent.intf, IPMI_POWEROFF_CHASSIS, 1) == 0) {
            v |= VM_CAPABILITIES_POWER;
        }
        if (k->do_hw_op(es->parent.intf, IPMI_RESET_CHASSIS, 1) == 0) {
            v |= VM_CAPABILITIES_RESET;
        }
        if (k->do_hw_op(es->parent.intf, IPMI_SEND_NMI, 1) == 0) {
            v |= VM_CAPABILITIES_NMI;
        }
        addchar(es, v);
        es->outbuf[es->outlen] = VM_CMD_CHAR;
        es->outlen++;
        es->sending_cmd = 0;
        continue_send(es);
        break;

    case CHR_EVENT_CLOSED:
        if (!es->connected) {
            return;
        }
        es->connected = 0;
        if (es->waiting_rsp) {
            es->waiting_rsp = 0;
            es->inbuf[1] = es->outbuf[1] | 0x04;
            es->inbuf[2] = es->outbuf[2];
            es->inbuf[3] = IPMI_CC_BMC_INIT_IN_PROGRESS;
            k->handle_rsp(s, es->outbuf[0], es->inbuf + 1, 3);
        }
        break;
    }
}

static void ipmi_extern_handle_reset(IPMIBmc *b)
{
    IPMIExternBmc *es = IPMI_BMC_EXTERN(b);

    es->send_reset = 1;
    continue_send(es);
}

static void ipmi_extern_init(IPMIBmc *b, Error **errp)
{
    IPMIExternBmc *es = IPMI_BMC_EXTERN(b);

    es->extern_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, extern_timeout, es);
    qemu_chr_add_handlers(es->parent.chr, can_receive, receive, chr_event, es);
}

static void ipmi_extern_class_init(ObjectClass *klass, void *data)
{
    IPMIBmcClass *bk = IPMI_BMC_CLASS(klass);

    bk->init = ipmi_extern_init;
    bk->handle_command = ipmi_extern_handle_command;
    bk->handle_reset = ipmi_extern_handle_reset;
}

static const TypeInfo ipmi_extern_type = {
    .name          = TYPE_IPMI_BMC_EXTERN,
    .parent        = TYPE_IPMI_BMC,
    .instance_size = sizeof(IPMIExternBmc),
    .class_init    = ipmi_extern_class_init,
};

static void ipmi_extern_register_types(void)
{
    type_register_static(&ipmi_extern_type);
}

type_init(ipmi_extern_register_types)

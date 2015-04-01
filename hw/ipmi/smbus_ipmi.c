/*
 * QEMU IPMI SMBus (SSIF) emulation
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
#include "hw/i2c/smbus.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/ipmi/ipmi.h"

#define TYPE_SMBUS_IPMI "smbus-ipmi"
#define SMBUS_IPMI(obj) OBJECT_CHECK(SMBusIPMIDevice, (obj), TYPE_SMBUS_IPMI)

#define SSIF_IPMI_REQUEST                       2
#define SSIF_IPMI_MULTI_PART_REQUEST_START      6
#define SSIF_IPMI_MULTI_PART_REQUEST_MIDDLE     7
#define SSIF_IPMI_RESPONSE                      3
#define SSIF_IPMI_MULTI_PART_RESPONSE_MIDDLE    9

typedef struct SMBusIPMIDevice {
    SMBusDevice parent;

    IPMIBmc *bmc;

    uint8_t outmsg[MAX_IPMI_MSG_SIZE];
    uint32_t outpos;
    uint32_t outlen;

    uint8_t inmsg[MAX_IPMI_MSG_SIZE];
    uint32_t inlen;

    /*
     * This is a response number that we send with the command to make
     * sure that the response matches the command.
     */
    uint8_t waiting_rsp;

    IPMIFwInfo fwinfo;
} SMBusIPMIDevice;

static void smbus_ipmi_handle_event(IPMIInterface *ii)
{
    /* No interrupts, so nothing to do here. */
}

static void smbus_ipmi_handle_rsp(IPMIInterface *ii, uint8_t msg_id,
                                  unsigned char *rsp, unsigned int rsp_len)
{
    SMBusIPMIDevice *sid = SMBUS_IPMI(ii);

    if (sid->waiting_rsp == msg_id) {
        sid->waiting_rsp++;

        memcpy(sid->outmsg, rsp, rsp_len);
        sid->outlen = rsp_len;
        sid->outpos = 0;
    }
}

static void smbus_ipmi_set_atn(IPMIInterface *ii, int val, int irq)
{
    /* This is where PEC would go. */
}

static void smbus_ipmi_set_irq_enable(IPMIInterface *ii, int val)
{
}

static void ipmi_quick_cmd(SMBusDevice *dev, uint8_t read)
{
}

static void ipmi_send_byte(SMBusDevice *dev, uint8_t val)
{
}

static uint8_t ipmi_receive_byte(SMBusDevice *dev)
{
    SMBusIPMIDevice *sid = SMBUS_IPMI(dev);

    if (sid->outpos >= sid->outlen)
        return 0;

    return sid->outmsg[sid->outpos++];
}

static void ipmi_write_data(SMBusDevice *dev, uint8_t cmd, uint8_t *buf, int len)
{
    SMBusIPMIDevice *sid = SMBUS_IPMI(dev);
    IPMIBmcClass *bk = IPMI_BMC_GET_CLASS(sid->bmc);

    if (cmd != SSIF_IPMI_REQUEST)
        return;

    if (len < 3 || len > MAX_IPMI_MSG_SIZE || buf[0] != len - 1)
        return;

    memcpy(sid->inmsg, buf + 1, len - 1);
    sid->inlen = len;

    sid->outlen = 0;
    sid->outpos = 0;
    bk->handle_command(sid->bmc, sid->inmsg, sid->inlen, sizeof(sid->inmsg),
                       sid->waiting_rsp);
}

static uint8_t ipmi_read_data(SMBusDevice *dev, uint8_t cmd, int n)
{
    SMBusIPMIDevice *sid = SMBUS_IPMI(dev);

    if (cmd != SSIF_IPMI_RESPONSE)
        return 0;

    if (n == 0)
        return sid->outlen;

    return ipmi_receive_byte(dev);
}

static const VMStateDescription vmstate_smbus_ipmi = {
    .name = TYPE_SMBUS_IPMI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT8(waiting_rsp, SMBusIPMIDevice),
        VMSTATE_END_OF_LIST()
    }
};

static void smbus_ipmi_realize(DeviceState *dev, Error **errp)
{
    SMBusIPMIDevice *sid = SMBUS_IPMI(dev);
    IPMIInterface *ii = IPMI_INTERFACE(dev);

    if (!sid->bmc) {
        error_setg(errp, "IPMI device requires a bmc attribute to be set");
        return;
    }

    sid->bmc->intf = ii;

    sid->fwinfo.interface_name = "smbus";
    sid->fwinfo.interface_type = IPMI_SMBIOS_SSIF;
    sid->fwinfo.ipmi_spec_major_revision = 2;
    sid->fwinfo.ipmi_spec_minor_revision = 0;
    sid->fwinfo.i2c_slave_address = sid->bmc->slave_addr;
    sid->fwinfo.base_address = sid->parent.i2c.address;
    sid->fwinfo.memspace = IPMI_MEMSPACE_SMBUS;
    sid->fwinfo.register_spacing = 1;
    ipmi_add_fwinfo(&sid->fwinfo, errp);
}

static void smbus_ipmi_init(Object *obj)
{
    SMBusIPMIDevice *sid = SMBUS_IPMI(obj);

    ipmi_bmc_find_and_link(OBJECT(obj), (Object **) &sid->bmc);
}

static void smbus_ipmi_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    IPMIInterfaceClass *iic = IPMI_INTERFACE_CLASS(oc);
    SMBusDeviceClass *sc = SMBUS_DEVICE_CLASS(oc);

    sc->quick_cmd = ipmi_quick_cmd;
    sc->send_byte = ipmi_send_byte;
    sc->receive_byte = ipmi_receive_byte;
    sc->write_data = ipmi_write_data;
    sc->read_data = ipmi_read_data;
    dc->vmsd = &vmstate_smbus_ipmi;
    dc->realize = smbus_ipmi_realize;
    iic->set_atn = smbus_ipmi_set_atn;
    iic->handle_rsp = smbus_ipmi_handle_rsp;
    iic->handle_if_event = smbus_ipmi_handle_event;
    iic->set_irq_enable = smbus_ipmi_set_irq_enable;
}

static const TypeInfo smbus_ipmi_info = {
    .name          = TYPE_SMBUS_IPMI,
    .parent        = TYPE_SMBUS_DEVICE,
    .instance_size = sizeof(SMBusIPMIDevice),
    .instance_init = smbus_ipmi_init,
    .class_init    = smbus_ipmi_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_IPMI_INTERFACE },
        { }
    }
};

static void smbus_ipmi_register_types(void)
{
    type_register_static(&smbus_ipmi_info);
}

type_init(smbus_ipmi_register_types)

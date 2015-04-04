
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/smbus_slave.h"

/*#define DEBUG*/
#ifdef DEBUG
#define DPRINTF(fmt, ...) \
do { printf("pca9540: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

#define TYPE_PCA9540_MASTER "pca9540-master"
#define PCA9540_MASTER(obj) OBJECT_CHECK(PCA9540Master, (obj), \
                                         TYPE_PCA9540_MASTER)

typedef struct PCA9540Device PCA9540Device;

#define MAX_PCA9540_SLAVES 2
#define MAX_PCA9540_ADDRS 128

typedef struct PCA9540Master {
    I2CSlave dev;
    I2CSlave *slaves[MAX_PCA9540_SLAVES];
    PCA9540Device *pca9540;
} PCA9540Master;

typedef struct PCA9540MasterClass {
    I2CSlaveClass parent_class;
} PCA9540MasterClass;

#define TYPE_PCA9540 "pca9540"
#define PCA9540(obj) OBJECT_CHECK(PCA9540Device, (obj), TYPE_PCA9540)

struct PCA9540Device {
    SMBusDevice dev;
    uint8_t selector;
    I2CBus *master;

    I2CBus *busses[MAX_PCA9540_SLAVES];
    PCA9540Master *masters[MAX_PCA9540_ADDRS];
};

typedef struct PCA9540Class {
    SMBusDeviceClass parent_class;
} PCA9540Class;

static int pca9540_write_data(SMBusDevice *dev, uint8_t *buf, uint8_t len)
{
    PCA9540Device *pca9540 = PCA9540(dev);

    DPRINTF("pca9540_write_data: addr=0x%02x val=0x%02x\n",
            dev->i2c.address, buf[0]);
    pca9540->selector = buf[0];

    return 0;
}

static uint8_t pca9540_receive_byte(SMBusDevice *dev)
{
    PCA9540Device *pca9540 = PCA9540(dev);
    uint8_t val = pca9540->selector;

    DPRINTF("pca9540_receive_byte: addr=0x%02x val=0x%02x\n",
            dev->i2c.address, val);
    return val;
}

static void pca9540_child_added(BusState *bus, DeviceState *dev)
{
    PCA9540Device *pca9540 = PCA9540(bus->parent);
    I2CSlave *slave = I2C_SLAVE(dev);
    I2CBus *buss = I2C_BUS(bus);
    PCA9540Master *master;
    unsigned int busn;
    uint8_t addr = slave->address;

    if (addr >= MAX_PCA9540_ADDRS) {
        error_report("%s: Invalid child bus address for %s: 0x%x",
                     DEVICE(pca9540)->id, DEVICE(bus)->id, addr);
        return;
    }
    for (busn = 0; busn < MAX_PCA9540_SLAVES; busn++) {
        if (buss == pca9540->busses[busn]) {
            break;
        }
    }
    if (busn >= MAX_PCA9540_SLAVES) {
        error_report("%s: unknown child bus %s", DEVICE(pca9540)->id,
                     DEVICE(bus)->id);
        return;
    }
    master = pca9540->masters[addr];
    if (!master) {
        master = PCA9540_MASTER(i2c_slave_create_simple(pca9540->master,
                                                        TYPE_PCA9540_MASTER,
                                                        addr));
        master->pca9540 = pca9540;
        pca9540->masters[addr] = master;
    }
    master->slaves[busn] = slave;
}

static void pca9540_child_removed(BusState *bus, DeviceState *dev)
{
    PCA9540Device *pca9540 = PCA9540(bus->parent);
    I2CSlave *slave = I2C_SLAVE(dev);
    I2CBus *buss = I2C_BUS(bus);
    PCA9540Master *master;
    unsigned int busn;
    uint8_t addr = slave->address;

    if (addr >= MAX_PCA9540_ADDRS) {
        error_report("%s: Invalid remove child bus address for %s: 0x%x",
                     DEVICE(pca9540)->id, DEVICE(bus)->id, addr);
        return;
    }
    for (busn = 0; busn < MAX_PCA9540_SLAVES; busn++) {
        if (buss == pca9540->busses[busn]) {
            break;
        }
    }
    if (busn >= MAX_PCA9540_SLAVES) {
        error_report("%s: unknown remove child bus %s", DEVICE(pca9540)->id,
                     DEVICE(bus)->id);
        return;
    }
    master = pca9540->masters[addr];
    if (!master) {
        return;
    }

    if (!master->slaves[busn]) {
        return;
    }

    master->slaves[busn] = NULL;

    for (busn = 0; busn < MAX_PCA9540_SLAVES; busn++) {
        if (pca9540->busses[busn]) {
            /* Still a device connected, don't delete master. */
            return;
        }
    }

    pca9540->masters[addr] = NULL;

    /* Destroy the master */
    object_unparent(OBJECT(master));
    object_unref(OBJECT(master));
}

static void pca9540_realize(DeviceState *smbdev, Error **errp)
{
    PCA9540Device *pca9540 = PCA9540(smbdev);
    char name[40];
    unsigned int i;

    pca9540->master = I2C_BUS(qdev_get_parent_bus(DEVICE(smbdev)));

    for (i = 0; i < MAX_PCA9540_SLAVES; i++) {
        BusState *bus;

        snprintf(name, sizeof(name), "%s-pca9540-%d",
                 pca9540->master->qbus.name, i);
        pca9540->busses[i] = i2c_init_bus(DEVICE(pca9540), name);
        bus = BUS(pca9540->busses[i]);
        bus->child_added = pca9540_child_added;
        bus->child_removed = pca9540_child_removed;
    }
}

static I2CSlave *pca9540_get_curr_slave(I2CSlave *s)
{
    PCA9540Master *master = PCA9540_MASTER(s);
    unsigned int busn;
    uint8_t sel = master->pca9540->selector;

    DPRINTF("current selector=0x%2.2x\n", sel);
    if (!(sel & 0x4)) {
        return NULL;
    }
    busn = sel & 0x3;
    if (busn > MAX_PCA9540_SLAVES) {
        return NULL;
    }
    return master->slaves[busn];
}

static int pca9540_master_event(I2CSlave *s, enum i2c_event event)
{
    I2CSlave *slave = pca9540_get_curr_slave(s);
    I2CSlaveClass *sc;
    int rv;

    DPRINTF("event check=%d\n", event);

    if (!slave) {
        return 1;
    }

    sc = I2C_SLAVE_GET_CLASS(slave);
    rv = sc->event(slave, event);
    DPRINTF("event check returns=%d\n", rv);
    return rv;
}

static uint8_t pca9540_master_recv(I2CSlave *s)
{
    I2CSlave *slave = pca9540_get_curr_slave(s);
    int rv = -1;

    if (slave) {
        I2CSlaveClass *sc = I2C_SLAVE_GET_CLASS(slave);

        if (sc->recv) {
            rv = sc->recv(slave);
        }
    }

    DPRINTF("recv: %d\n", rv);
    return rv;
}

static int pca9540_master_send(I2CSlave *s, uint8_t val)
{
    I2CSlave *slave = pca9540_get_curr_slave(s);
    int rv = -1;

    if (slave) {
        I2CSlaveClass *sc = I2C_SLAVE_GET_CLASS(slave);

        if (sc->send) {
            rv = sc->send(slave, val);
        }
    }

    DPRINTF("send 0x%2.2x: %d\n", val, rv);
    return rv;
}

static const VMStateDescription vmstate_pca9540_master = {
    .name = TYPE_PCA9540_MASTER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_I2C_SLAVE(dev, PCA9540Master),
        VMSTATE_END_OF_LIST()
    }
};

static void pca9540_master_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(klass);

    dc->vmsd = &vmstate_pca9540_master;
    sc->event = pca9540_master_event;
    sc->recv = pca9540_master_recv;
    sc->send = pca9540_master_send;
}

static const TypeInfo pca9540_master_type_info = {
    .name = TYPE_PCA9540_MASTER,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(PCA9540Master),
    .class_size = sizeof(PCA9540MasterClass),
    .class_init = pca9540_master_class_initfn,
};

static const VMStateDescription vmstate_pca9540 = {
    .name = TYPE_PCA9540,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_SMBUS_DEVICE(dev, PCA9540Device),
        VMSTATE_UINT8(selector, PCA9540Device),
        VMSTATE_END_OF_LIST()
    }
};

static void pca9540_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SMBusDeviceClass *sc = SMBUS_DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_pca9540;
    dc->realize = pca9540_realize;
    sc->write_data = pca9540_write_data;
    sc->receive_byte = pca9540_receive_byte;
}

static const TypeInfo pca9540_type_info = {
    .name          = TYPE_PCA9540,
    .parent        = TYPE_SMBUS_DEVICE,
    .instance_size = sizeof(PCA9540Device),
    .class_size = sizeof(PCA9540Class),
    .class_init    = pca9540_class_initfn,
};

static void pca9540_register_types(void)
{
    type_register_static(&pca9540_type_info);
    type_register_static(&pca9540_master_type_info);
}

type_init(pca9540_register_types)

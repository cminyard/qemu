
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/smbus_slave.h"
#include "hw/irqif.h"

/*#define DEBUG*/
#ifdef DEBUG
#define DPRINTF(fmt, ...) \
do { printf("pca9541: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

#define TYPE_PCA9541_MASTER "pca9541-master"
#define PCA9541_MASTER(obj) OBJECT_CHECK(PCA9541Master, (obj), \
                                         TYPE_PCA9541_MASTER)

typedef struct PCA9541Device PCA9541Device;

#define MAX_PCA9541_ADDRS 128

typedef struct PCA9541Master {
    I2CSlave dev;
    I2CSlave *slave;
    PCA9541Device *pca9541;
} PCA9541Master;

typedef struct PCA9541MasterClass {
    I2CSlaveClass parent_class;
} PCA9541MasterClass;

#define TYPE_PCA9541 "pca9541"
#define PCA9541(obj) OBJECT_CHECK(PCA9541Device, (obj), TYPE_PCA9541)

/*
 * For simulating the other master, what is the other master doing
 * right now.
 */
enum pca9541_other_master_mode {
    /* Just let the driver have the bus. */
    pca9541_other_master_busoff_give_ownership,
    /* Request ownership before the driver does. */
    pca9541_other_master_busoff_request_ownership,
    /* Request ownership before the driver and let the arbitration time out. */
    pca9541_other_master_busoff_arb_timeout,
    /* The driver owns the bus. */
    pca9541_other_master_buson_you_own_it,
    /* The other master owns the bus, give it up eventually. */
    pca9541_other_master_buson_i_own_it,
    /* The other master owns the bus and never gives it up. */
    pca9541_other_master_buson_i_own_it_timeout,
};
#define pca0541_other_master_mode_max \
    pca9541_other_master_buson_i_own_it_timeout

struct PCA9541Device {
    SMBusDevice dev;
    uint8_t curr_regnum;
    uint8_t ienable;
    uint8_t control;
    uint8_t istat;
    I2CBus *master;

    I2CBus *bus;
    PCA9541Master *masters[MAX_PCA9541_ADDRS];

    IRQInterface *irqdev;
    qemu_irq *irq;
    bool irq_raised;

    bool sim_other_master;
    int32_t om_mode; /* Really enum pca9541_other_master_mode */
    int32_t control_reads; /* Number of control reads done in this test. */
};

typedef struct PCA9541Class {
    SMBusDeviceClass parent_class;
} PCA9541Class;

#define PCA9541_AUTO_INC        (1 << 4)

#define IENABLE_REG     0
#define IENABLE_WRITE_MASK 0x0f
#define IENABLE_INTIN   (1 << 0)
#define IENABLE_BUSINIT (1 << 1)
#define IENABLE_BUSOK   (1 << 2)
#define IENABLE_BUSLOST (1 << 3)
#define CONTROL_REG     1
#define CONTROL_MYBUS   (1 << 0)
#define CONTROL_NMYBUS  (1 << 1)
#define CONTROL_BUSON   (1 << 2)
#define CONTROL_NBUSON  (1 << 3)
#define CONTROL_BUSINIT (1 << 4)
#define CONTROL_TESTON  (1 << 6)
#define CONTROL_NTESTON (1 << 7)
/* Bits the user can write to. */
#define CONTROL_WRITE_MASK (CONTROL_NTESTON | CONTROL_TESTON |          \
                            CONTROL_BUSINIT | CONTROL_BUSON | CONTROL_MYBUS)
#define ISTAT_REG       2
#define ISTAT_INTIN     (1 << 0)
#define ISTAT_BUSINIT   (1 << 1)
#define ISTAT_BUSOK     (1 << 2)
#define ISTAT_BUSLOST   (1 << 3)
#define ISTAT_MYTEST    (1 << 6)
#define ISTAT_NMYTEST   (1 << 7)
#define ISTAT_CLEAR_ON_READ_BITS (ISTAT_BUSINIT | ISTAT_BUSOK | ISTAT_BUSLOST)
#define IENABLE_ALWAYS_ON  (ISTAT_MYTEST | ISTAT_NMYTEST)

/* Take t, change the bits in it that are set in mask to the values of v. */
#define SET_ONLY_BITS(t, v, mask) (((t) & ~(mask)) | ((v) & (mask)))

#define FLIP_BITS(v, mask) SET_ONLY_BITS(v, ~v, mask)

/*
 * From the data sheet, sort of, table of the bottom 4 control bits to
 * know if we have control of the bus.
 */
static struct {
    uint8_t enabled;
    uint8_t bus_on;
    uint8_t control;
} pca9541_ctrlstate[16] = {
/* Enabled  Bus On  Control */
    { 0,      0,       1 },     /* NBUSON=0, BUSON=0, NMYBUS=0, MYBUS=0 */
    { 0,      0,       0 },     /* NBUSON=0, BUSON=0, NMYBUS=0, MYBUS=1 */
    { 0,      0,       0 },     /* NBUSON=0, BUSON=0, NMYBUS=1, MYBUS=0 */
    { 0,      0,       1 },     /* NBUSON=0, BUSON=0, NMYBUS=1, MYBUS=1 */
    { 1,      1,       1 },     /* NBUSON=0, BUSON=1, NMYBUS=0, MYBUS=0 */
    { 0,      1,       0 },     /* NBUSON=0, BUSON=1, NMYBUS=0, MYBUS=1 */
    { 0,      1,       0 },     /* NBUSON=0, BUSON=1, NMYBUS=1, MYBUS=0 */
    { 1,      1,       1 },     /* NBUSON=0, BUSON=1, NMYBUS=1, MYBUS=1 */
    { 1,      1,       1 },     /* NBUSON=1, BUSON=0, NMYBUS=0, MYBUS=0 */
    { 0,      1,       0 },     /* NBUSON=1, BUSON=0, NMYBUS=0, MYBUS=1 */
    { 0,      1,       0 },     /* NBUSON=1, BUSON=0, NMYBUS=1, MYBUS=0 */
    { 1,      1,       1 },     /* NBUSON=1, BUSON=0, NMYBUS=1, MYBUS=1 */
    { 0,      0,       1 },     /* NBUSON=1, BUSON=1, NMYBUS=0, MYBUS=0 */
    { 0,      0,       0 },     /* NBUSON=1, BUSON=1, NMYBUS=0, MYBUS=1 */
    { 0,      0,       0 },     /* NBUSON=1, BUSON=1, NMYBUS=1, MYBUS=0 */
    { 0,      0,       1 }      /* NBUSON=1, BUSON=1, NMYBUS=1, MYBUS=1 */
};

enum pca9541_other_master_event {
    PCA9541_om_control_read,
    PCA9541_om_istat_read,
    PCA9541_om_control_write
};

#define BUS_ON(v) (pca9541_ctrlstate[v & 0xf].bus_on)
#define HAVE_CTRL(p) (pca9541_ctrlstate[(p)->control & 0xf].control)
#define SET_BUS_OFF(p) \
    do {                                                                \
        if (BUS_ON(p->control)) {                                       \
            (p)->control = FLIP_BITS((p)->control, CONTROL_NBUSON);     \
        }                                                               \
    } while (0)
#define SET_BUS_ON(p) \
    do {                                                                \
        if (!BUS_ON(p->control)) {                                      \
            (p)->control = FLIP_BITS((p)->control, CONTROL_NBUSON);     \
        }                                                               \
    } while (0)

#define SET_HAVE_CTRL(p)                                                \
    do {                                                                \
        if (!HAVE_CTRL(p)) {                                            \
            (p)->control = FLIP_BITS((p)->control, CONTROL_NMYBUS);     \
        }                                                               \
    } while (0)
#define SET_NOT_HAVE_CTRL(p)                                            \
    do {                                                                \
        if (HAVE_CTRL(p)) {                                             \
            (p)->control = FLIP_BITS((p)->control, CONTROL_NMYBUS);     \
        }                                                               \
    } while (0)

static void pca9541_om_next_state(PCA9541Device *pca9541)
{
    /* Just cycle through the modes. */
    if (pca9541->om_mode >= pca0541_other_master_mode_max) {
        pca9541->om_mode = pca9541_other_master_busoff_give_ownership;
    } else {
        pca9541->om_mode++;
    }

    /* Make sure we are in a clean state. */
    pca9541->istat = SET_ONLY_BITS(pca9541->istat, 0, ISTAT_NMYTEST);
    pca9541->control_reads = 0;
}

static bool pca9541_om_next_state_on_busoff(PCA9541Device *pca9541,
                                            uint8_t newval)
{
    if (!BUS_ON(newval)) {
        pca9541_om_next_state(pca9541);
        return true;
    }

    return false;
}

#ifdef DEBUG
static const char *om_mode[] = {
    "pca9541_other_master_busoff_give_ownership",
    "pca9541_other_master_busoff_request_ownership",
    "pca9541_other_master_busoff_arb_timeout",
    "pca9541_other_master_buson_you_own_it",
    "pca9541_other_master_buson_i_own_it",
    "pca9541_other_master_buson_i_own_it_timeout"
};

static const char *om_event[] = {
    "PCA9541_om_control_read",
    "PCA9541_om_istat_read",
    "PCA9541_om_control_write"
};
#endif

static void pca9541_other_master_work(PCA9541Device *pca9541,
                                      enum pca9541_other_master_event event,
                                      uint8_t newval)
{
    uint8_t om_control;

    DPRINTF("mode: %s  event: %s  ctrl: %2.2x  istat: %2.2x  newval: %2.2x\n",
            om_mode[pca9541->om_mode], om_event[event],
            pca9541->control, pca9541->istat, newval);

    if (!pca9541->sim_other_master) {
        return;
    }

    /* Flip the bits around to give the control value for the other master. */
    om_control = (((pca9541->control >> 1) & 0x5) |
                  ((pca9541->control << 1) & 0xc));
    /* NMYBUS is inverted from the other side. */
    om_control = FLIP_BITS(om_control, CONTROL_MYBUS);

    if (event == PCA9541_om_control_read) {
        pca9541->control_reads++;
    }

    switch (pca9541->om_mode) {
    case pca9541_other_master_busoff_give_ownership:
        /* Just let the driver claim the device. */
        switch (event) {
        case PCA9541_om_control_read:
            if (pca9541->control_reads == 1) {
                SET_BUS_OFF(pca9541);
            }
            break;

        case PCA9541_om_istat_read:
            break;

        case PCA9541_om_control_write:
            pca9541_om_next_state_on_busoff(pca9541, newval);
            break;
        }
        break;

    case pca9541_other_master_busoff_request_ownership:
        /* Other master owns the device the first time through. */
        switch (event) {
        case PCA9541_om_control_read:
            if (pca9541->control_reads == 1) {
                SET_BUS_OFF(pca9541);
            }
            break;

        case PCA9541_om_istat_read:
            /*
             * The first time through, turn this on, then turn it off
             * the second time to let the system have the bus.
             */
            pca9541->istat = FLIP_BITS(pca9541->istat, ISTAT_NMYTEST);
            break;

        case PCA9541_om_control_write:
            pca9541_om_next_state_on_busoff(pca9541, newval);
            break;
        }
        break;

    case pca9541_other_master_busoff_arb_timeout:
        /* Other master owns the device and doesn't give it up. */
        switch (event) {
        case PCA9541_om_control_read:
            if (pca9541->control_reads == 1) {
                SET_BUS_OFF(pca9541);
            }
            pca9541->istat = SET_ONLY_BITS(pca9541->istat, ISTAT_NMYTEST,
                                           ISTAT_NMYTEST);
            break;

        case PCA9541_om_istat_read:
            break;

        case PCA9541_om_control_write:
            pca9541_om_next_state_on_busoff(pca9541, newval);
            break;
        }
        break;

    case pca9541_other_master_buson_you_own_it:
        /* The driver already owns the device the first time through. */
        switch (event) {
        case PCA9541_om_control_read:
            SET_BUS_ON(pca9541);
            SET_HAVE_CTRL(pca9541);
            break;

        case PCA9541_om_istat_read:
            break;

        case PCA9541_om_control_write:
            pca9541_om_next_state_on_busoff(pca9541, newval);
            break;
        }
        break;

    case pca9541_other_master_buson_i_own_it:
        /* The other master owns the device, gives it up the send try */
        switch (event) {
        case PCA9541_om_control_read:
            if (pca9541->control_reads == 1) {
                SET_BUS_ON(pca9541);
                SET_NOT_HAVE_CTRL(pca9541);
            } else {
                SET_BUS_OFF(pca9541);
            }
            break;

        case PCA9541_om_istat_read:
            break;

        case PCA9541_om_control_write:
            pca9541_om_next_state_on_busoff(pca9541, newval);
            break;
        }
        break;

    case pca9541_other_master_buson_i_own_it_timeout:
        switch (event) {
        case PCA9541_om_control_read:
            SET_BUS_ON(pca9541);
            SET_NOT_HAVE_CTRL(pca9541);
            break;

        case PCA9541_om_istat_read:
            break;

        case PCA9541_om_control_write:
            pca9541_om_next_state(pca9541);
            break;
        }
        break;

    }
}

static void pca9541_check_irq(PCA9541Device *pca9541)
{
    bool enabled = pca9541->istat & (~pca9541->ienable | IENABLE_ALWAYS_ON);

    if (enabled != pca9541->irq_raised) {
        pca9541->irq_raised = enabled;
        if (pca9541->irq) {
            if (enabled) {
                qemu_irq_raise(*pca9541->irq);
            } else {
                qemu_irq_lower(*pca9541->irq);
            }
        }
    }
}

static uint8_t pca9541_next_reg(PCA9541Device *pca9541)
{
    uint8_t reg = pca9541->curr_regnum % 3;

    /* Continuous reading wraps at 2. */
    if (pca9541->curr_regnum & PCA9541_AUTO_INC) {
        reg = (reg + 1) % 3;
    }
    pca9541->curr_regnum &= ~3;
    pca9541->curr_regnum |= reg;

    return reg;
}

static void pca9541_write_next(PCA9541Device *pca9541, uint8_t val)
{
    uint8_t reg = pca9541_next_reg(pca9541);
    uint8_t newval;

    DPRINTF("pca9541_write_next: addr=0x%02x reg=0x%02x val=0x%02x\n",
            dev->i2c.address, pca9541->curr_regnum, val);
    switch (reg) {
    case IENABLE_REG:
        pca9541->ienable = val & IENABLE_WRITE_MASK;
        pca9541_check_irq(pca9541);
        break;

    case CONTROL_REG:
        val &= CONTROL_WRITE_MASK;

        /* Set the local test interrupt bit. */
        /* CONTROL_TESTON and ISTAT_MYTEST are the same bit. */
        pca9541->istat = SET_ONLY_BITS(pca9541->istat, val, CONTROL_TESTON);

        newval = SET_ONLY_BITS(pca9541->control, val, CONTROL_WRITE_MASK);
        pca9541_other_master_work(pca9541, PCA9541_om_control_write, newval);
        pca9541->control = newval;

        /*
         * If the user enabled businit and we own the bus, do the
         * interrupt.
         */
        if ((val & CONTROL_BUSINIT) &&
                             pca9541_ctrlstate[pca9541->control & 0xf].enabled)
        {
            pca9541->istat |= ISTAT_BUSINIT;
        }

        pca9541_check_irq(pca9541);
        break;
    }
}

static int pca9541_write_data(SMBusDevice *dev, uint8_t *buf, uint8_t len)
{
    PCA9541Device *pca9541 = PCA9541(dev);
    int i;

    pca9541->curr_regnum = buf[0];
    for (i = 1; i < len; i++) {
        pca9541_write_next(pca9541, buf[i]);
    }

    return 0;
}

static uint8_t pca9541_read_next(PCA9541Device *pca9541)
{
    uint8_t reg = pca9541_next_reg(pca9541);
    uint8_t val;

    switch (reg) {
    case IENABLE_REG:
        val = pca9541->ienable;
        break;

    case CONTROL_REG:
        pca9541_other_master_work(pca9541, PCA9541_om_control_read, 0);
        val = pca9541->control;
        break;

    case ISTAT_REG:
        pca9541_other_master_work(pca9541, PCA9541_om_istat_read, 0);
        val = pca9541->istat;
        pca9541->istat &= ~ISTAT_CLEAR_ON_READ_BITS;
        pca9541_check_irq(pca9541);
        break;

    default:
        val = 0xff;
        break;
    }
    return val;
}

static uint8_t pca9541_read_data(SMBusDevice *dev)
{
    PCA9541Device *pca9541 = PCA9541(dev);

    return pca9541_read_next(pca9541);
}

static void pca9541_child_added(BusState *bus, DeviceState *dev)
{
    PCA9541Device *pca9541 = PCA9541(bus->parent);
    I2CSlave *slave = I2C_SLAVE(dev);
    PCA9541Master *master;
    uint8_t addr = slave->address;

    if (addr >= MAX_PCA9541_ADDRS) {
        error_report("%s: Invalid child bus address for %s: 0x%x",
                     DEVICE(pca9541)->id, DEVICE(bus)->id, addr);
        return;
    }
    master = pca9541->masters[addr];
    if (!master) {
        master = PCA9541_MASTER(i2c_slave_create_simple(pca9541->master,
                                                        TYPE_PCA9541_MASTER,
                                                        addr));
        master->pca9541 = pca9541;
        pca9541->masters[addr] = master;
    }
    master->slave = slave;
}

static void pca9541_child_removed(BusState *bus, DeviceState *dev)
{
    PCA9541Device *pca9541 = PCA9541(bus->parent);
    I2CSlave *slave = I2C_SLAVE(dev);
    PCA9541Master *master;
    uint8_t addr = slave->address;

    if (addr >= MAX_PCA9541_ADDRS) {
        error_report("%s: Invalid remove child bus address for %s: 0x%x",
                     DEVICE(pca9541)->id, DEVICE(bus)->id, addr);
        return;
    }
    master = pca9541->masters[addr];
    if (!master) {
        return;
    }

    if (!master->slave) {
        return;
    }

    master->slave = NULL;

    pca9541->masters[addr] = NULL;

    /* Destroy the master */
    object_unparent(OBJECT(master));
    object_unref(OBJECT(master));
}

static void pca9541_realize(DeviceState *smbdev, Error **errp)
{
    PCA9541Device *pca9541 = PCA9541(smbdev);
    char name[40];
    BusState *bus;

    if (pca9541->irqdev) {
        IRQInterfaceClass *iic = IRQ_INTERFACE_GET_CLASS(pca9541->irqdev);
        pca9541->irq = iic->get_irq(pca9541->irqdev);
    }

    pca9541->master = I2C_BUS(qdev_get_parent_bus(DEVICE(smbdev)));

    snprintf(name, sizeof(name), "%s-pca9541",
             pca9541->master->qbus.name);
    pca9541->bus = i2c_init_bus(DEVICE(pca9541), name);
    bus = BUS(pca9541->bus);
    bus->child_added = pca9541_child_added;
    bus->child_removed = pca9541_child_removed;
}

static int pca9541_master_event(I2CSlave *s, enum i2c_event event)
{
    PCA9541Master *master = PCA9541_MASTER(s);
    I2CSlave *slave = master->slave;
    PCA9541Device *pca9541 = master->pca9541;
    I2CSlaveClass *sc;
    int rv;

    DPRINTF("event check=%d\n", event);

    if (!pca9541_ctrlstate[pca9541->control & 0xf].enabled) {
        return 1;
    }

    if (!slave) {
        return 1;
    }

    sc = I2C_SLAVE_GET_CLASS(slave);
    rv = sc->event(slave, event);
    DPRINTF("event check returns=%d\n", rv);
    return rv;
}

static uint8_t pca9541_master_recv(I2CSlave *s)
{
    PCA9541Master *master = PCA9541_MASTER(s);
    I2CSlave *slave = master->slave;
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

static int pca9541_master_send(I2CSlave *s, uint8_t data)
{
    PCA9541Master *master = PCA9541_MASTER(s);
    I2CSlave *slave = master->slave;
    int rv = -1;

    if (slave) {
        I2CSlaveClass *sc = I2C_SLAVE_GET_CLASS(slave);

        if (sc->send) {
            rv = sc->send(slave, data);
        }
    }

    DPRINTF("send 0x%2.2x: %d\n", data, rv);
    return rv;
}

static const VMStateDescription vmstate_pca9541_master = {
    .name = TYPE_PCA9541_MASTER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_I2C_SLAVE(dev, PCA9541Master),
        VMSTATE_END_OF_LIST()
    }
};

static void pca9541_master_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(klass);

    dc->vmsd = &vmstate_pca9541_master;
    sc->event = pca9541_master_event;
    sc->recv = pca9541_master_recv;
    sc->send = pca9541_master_send;
}

static const TypeInfo pca9541_master_type_info = {
    .name = TYPE_PCA9541_MASTER,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(PCA9541Master),
    .class_size = sizeof(PCA9541MasterClass),
    .class_init = pca9541_master_class_initfn,
};

static void irq_check(const Object *obj, const char *name,
                      Object *val, Error **errp)
{
    /* Always succeed. */
}

static const VMStateDescription vmstate_pca9541 = {
    .name = TYPE_PCA9541,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_SMBUS_DEVICE(dev, PCA9541Device),
        VMSTATE_UINT8(curr_regnum, PCA9541Device),
        VMSTATE_UINT8(ienable, PCA9541Device),
        VMSTATE_UINT8(control, PCA9541Device),
        VMSTATE_UINT8(istat, PCA9541Device),
        VMSTATE_BOOL(irq_raised, PCA9541Device),
        VMSTATE_INT32(om_mode, PCA9541Device),
        VMSTATE_INT32(control_reads, PCA9541Device),
        VMSTATE_END_OF_LIST()
    }
};

static void pca9541_instance_init(Object *obj)
{
    PCA9541Device *pca9541 = PCA9541(obj);

    object_property_add_link(obj, "irqid", TYPE_IRQ_INTERFACE,
                             (Object **) &pca9541->irqdev, irq_check,
                             OBJ_PROP_LINK_STRONG);
}

static Property pca9541_properties[] = {
    DEFINE_PROP_BOOL("sim_other_master", PCA9541Device, sim_other_master,
                     false),
    DEFINE_PROP_END_OF_LIST(),
};

static void pca9541_class_initfn(ObjectClass *oc, void *data)
{
    SMBusDeviceClass *sc = SMBUS_DEVICE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = pca9541_realize;
    sc->write_data = pca9541_write_data;
    sc->receive_byte = pca9541_read_data;
    dc->props_ = pca9541_properties;
    dc->vmsd = &vmstate_pca9541;
}

static const TypeInfo pca9541_type_info = {
    .name          = TYPE_PCA9541,
    .parent        = TYPE_SMBUS_DEVICE,
    .instance_size = sizeof(PCA9541Device),
    .instance_init = pca9541_instance_init,
    .class_size = sizeof(PCA9541Class),
    .class_init    = pca9541_class_initfn,
};

static void pca9541_register_types(void)
{
    type_register_static(&pca9541_type_info);
    type_register_static(&pca9541_master_type_info);
}

type_init(pca9541_register_types)

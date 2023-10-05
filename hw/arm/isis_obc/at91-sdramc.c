#include "at91-sdramc.h"
#include "qemu/error-report.h"

#define SDRAMC_MR       0x00
#define SDRAMC_TR       0x04
#define SDRAMC_CR       0x08
#define SDRAMC_LPR      0x10
#define SDRAMC_IER      0x14
#define SDRAMC_IDR      0x18
#define SDRAMC_IMR      0x1C
#define SDRAMC_ISR      0x20
#define SDRAMC_MDR      0x24

#define ISR_RES         BIT(0)


// TODO: fault-injection for refresh error


static void update_irq(SdramcState *s)
{
    qemu_set_irq(s->irq, !!(s->reg_imr & s->reg_isr));
}

static uint64_t sdramc_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    SdramcState *s = opaque;

    switch (offset) {
    case SDRAMC_MR:
        return s->reg_mr;

    case SDRAMC_TR:
        return s->reg_tr;

    case SDRAMC_CR:
        return s->reg_cr;

    case SDRAMC_LPR:
        return s->reg_lpr;

    case SDRAMC_IMR:
        return s->reg_imr;

    case SDRAMC_ISR:
        {
            uint32_t isr = s->reg_isr;
            s->reg_isr &= ~ISR_RES;
            update_irq(s);
            return isr;
        }

    case SDRAMC_MDR:
        return s->reg_mdr;

    default:
        error_report("at91.sdramc: illegal read access at 0x%02lx", offset);
        abort();
    }
}

static void sdramc_mmio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    SdramcState *s = opaque;

    switch (offset) {
    case SDRAMC_MR:
        s->reg_mr = value;
        break;

    case SDRAMC_TR:
        s->reg_tr = value;
        break;

    case SDRAMC_CR:
        s->reg_cr = value;
        break;

    case SDRAMC_LPR:
        s->reg_lpr = value;
        break;

    case SDRAMC_IER:
        s->reg_imr |= value;
        update_irq(s);
        break;

    case SDRAMC_IDR:
        s->reg_imr &= ~value;
        update_irq(s);
        break;

    case SDRAMC_MDR:
        s->reg_mdr = value;
        break;

    default:
        error_report("at91.sdramc: illegal read access at 0x%02lx", offset);
        abort();
    }
}

static const MemoryRegionOps sdramc_mmio_ops = {
    .read = sdramc_mmio_read,
    .write = sdramc_mmio_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void sdramc_device_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    SdramcState *s = AT91_SDRAMC(obj);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->mmio, OBJECT(s), &sdramc_mmio_ops, s, "at91.sdramc", 0x200);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void sdramc_reset_registers(SdramcState *s)
{
    s->reg_mr  = 0x00;
    s->reg_tr  = 0x00;
    s->reg_cr  = 0x852372C0;
    s->reg_lpr = 0x00;
    s->reg_imr = 0x00;
    s->reg_isr = 0x00;
    s->reg_mdr = 0x00;

    update_irq(s);
}

static void sdramc_device_realize(DeviceState *dev, Error **errp)
{
    SdramcState *s = AT91_SDRAMC(dev);
    sdramc_reset_registers(s);
}

static void sdramc_device_reset(DeviceState *dev)
{
    SdramcState *s = AT91_SDRAMC(dev);
    sdramc_reset_registers(s);
}

static void sdramc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sdramc_device_realize;
    dc->reset = sdramc_device_reset;
}

static const TypeInfo sdramc_device_info = {
    .name = TYPE_AT91_SDRAMC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SdramcState),
    .instance_init = sdramc_device_init,
    .class_init = sdramc_class_init,
};

static void sdramc_register_types(void)
{
    type_register_static(&sdramc_device_info);
}

type_init(sdramc_register_types)

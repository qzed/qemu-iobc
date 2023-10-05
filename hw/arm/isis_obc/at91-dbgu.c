#include "at91-dbgu.h"
#include "qemu/error-report.h"
#include "qemu/log.h"


// TODO(at91.dbgu.chip_id): export chip IDs as properties

#define IOBC_CIDR       0x00000000      // TODO(at91.dbgu.chip_id)
#define IOBC_EXID       0x00000000      // TODO(at91.dbgu.chip_id)

#define DBGU_CR         0x00
#define DBGU_MR         0x04
#define DBGU_IER        0x08
#define DBGU_IDR        0x0C
#define DBGU_IMR        0x10
#define DBGU_SR         0x14
#define DBGU_RHR        0x18
#define DBGU_THR        0x1C
#define DBGU_BRGR       0x20
#define DBGU_CIDR       0x40
#define DBGU_EXID       0x44
#define DBGU_FNR        0x48

#define PDC_REG_FIRST  0x100
#define PDC_REG_LAST   0x124


#define CR_RSTRX        (1 << 2)
#define CR_RSTTX        (1 << 3)
#define CR_RXEN         (1 << 4)
#define CR_RXDIS        (1 << 5)
#define CR_TXEN         (1 << 6)
#define CR_TXDIS        (1 << 7)
#define CR_RSTSTA       (1 << 8)

#define SR_RXRDY        (1 << 0)        // RHR ready to be read
#define SR_TXRDY        (1 << 1)        // THR ready to be written
#define SR_ENDRX        (1 << 3)        // PDC: finished receiving (RCR == 0)
#define SR_ENDTX        (1 << 4)        // PDC: finished transmission (TCR == 0)
#define SR_OVRE         (1 << 5)        // receiver overrun
#define SR_FRAME        (1 << 6)        // receiver frame error
#define SR_PARE         (1 << 7)        // receiver parity error
#define SR_TXEMPTY      (1 << 9)        // THR and shift register empty (write completed)
#define SR_TXBUFE       (1 << 11)       // PDC: no more data to transmit (TCR == TNCR == 0)
#define SR_RXBUFF       (1 << 12)       // PDC: no more data to receive/buffer full (RCR == RNCR == 0)
#define SR_COMMTX       (1 << 30)       // Forwarded to Core/Debug Comm Channel COMMTX
#define SR_COMMRX       (1 << 31)       // Forwarded to Core/Debug Comm Channel COMMRX


static int dbgu_uart_can_receive(void *opaque)
{
    return 1;   // TODO(at91.dbgu.recv)
}

static void dbgu_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    DbguState *s = opaque;

    if (size > 1) {
        error_report("at91.dbgu: cannot receive more than one character at a time");
        abort();
    }

    if (s->reg_sr & SR_RXRDY) {
        // SPEC: If DBGU_RHR has not been read by the software (or the
        // Peripheral Data Controller) since the last transfer, the RXRDY bit
        // is still set and a new character is received, the OVRE status bit in
        // DBGU_SR is set.
        s->reg_sr |= SR_OVRE;
        return;
    }

    // SPEC: When a complete character is received, it is transferred to the DBGU_RHR
    // and the RXRDY status bit in DBGU_SR (Status Register) is set.
    s->reg_rhr = buf[0];
    s->reg_sr |= SR_RXRDY;

    // TODO(at91.dbgu.pdc):
    // SPEC: The RXRDY bit triggers the PDC channel data transfer of the
    // receiver. This results in a read of the data in DBGU_RHR.

    // TODO(at91.dbgu.irq): update interrupts
}


static uint64_t dbgu_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    DbguState *s = opaque;

    if (size != 0x04) {
        error_report("at91.dbgu illegal read access at "
                      "0x%03lx with size: 0x%02x", offset, size);
        abort();
    }

    info_report("at91.dbgu read 0x%03lx", offset);

    switch (offset) {
    case DBGU_MR:
        return s->reg_mr;

    case DBGU_IMR:
        return s->reg_imr;

    case DBGU_SR:
        return s->reg_sr;

    case DBGU_RHR:
        s->reg_sr &= ~SR_RXRDY;
        // TODO(at91.dbgu.irq): update interrupts
        return s->reg_rhr;

    case DBGU_BRGR:
        return s->reg_brgr;

    case DBGU_CIDR:
        return s->reg_cidr;

    case DBGU_EXID:
        return s->reg_exid;

    case DBGU_FNR:
        return s->reg_fnr;

    case PDC_REG_FIRST ... PDC_REG_LAST:
        qemu_log_mask(LOG_UNIMP, "at91.dbgu: unimplemented read from PDC"
                      "(size %d, offset 0x%" HWADDR_PRIx ")\n",
                      size, offset);
        // TODO(at91.dbgu.pdc): implement PDC support (Sec. 23)

    default:
        error_report("at91.dbgu illegal read access at 0x%03lx", offset);
        abort();
    }
}

static void dbgu_mmio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    DbguState *s = opaque;
    uint8_t ch;

    if (size != 0x04) {
        error_report("at91.dbgu illegal write access at "
                      "0x%03lx with size: 0x%02x [value: 0x%08lx]",
                      offset, size, value);
        abort();
    }

    info_report("at91.dbgu write 0x%03lx [value: 0x%08lx]", offset, value);

    switch (offset) {
    case DBGU_CR:
        if (value & CR_RSTRX) {     // reset and disable receiver
            s->reg_sr &= ~SR_RXBUFF;
            s->rx_enabled = false;
        }
        if (value & CR_RSTTX) {     // reset and disable transmitter
            s->reg_sr |= SR_TXEMPTY;
            s->tx_enabled = false;
        }
        if (value & CR_RXEN) {      // enable receiver
            s->rx_enabled = true;
        }
        if (value & CR_RXDIS) {     // disable receiver (overrides RXEN)
            s->rx_enabled = false;
        }
        if (value & CR_TXEN) {      // enable transmitter
            s->reg_sr |= SR_TXRDY;
            s->tx_enabled = true;
        }
        if (value & CR_TXDIS) {     // disable transmitter (overrides TXDIS)
            s->tx_enabled = false;
        }
        if (value & CR_RSTSTA) {    // reset status bits
            s->reg_sr &= ~(SR_PARE | SR_FRAME | SR_OVRE);
        }
        break;

    case DBGU_MR:
        s->reg_mr = value;
        // TODO(at91.dbgu.mr): update mode
        break;

    case DBGU_IER:
        s->reg_imr |= ~value;
        break;

    case DBGU_IDR:
        s->reg_imr &= ~value;
        break;

    case DBGU_THR:
        ch = (uint8_t)value;

        // TODO(at91.dbgu.transmit)
        //
        // SPEC: The transmission starts when the programmer writes in the
        // Transmit Holding Register DBGU_THR, and after the written character
        // is transferred from DBGU_THR to the Shift Register. The bit TXRDY
        // remains high until a second character is written in DBGU_THR. As
        // soon as the first character is com- pleted, the last character
        // written in DBGU_THR is transferred into the shift register and TXRDY
        // rises again, showing that the holding reg- ister is empty.
        //
        // SPEC: When both the Shift Register and the DBGU_THR are empty, i.e.,
        // all the characters written in DBGU_THR have been processed, the bit
        // TXEMPTY rises after the last stop bit has been completed.

        // TODO(at91.dbug.pdc):
        // SPEC: The TXRDY bit triggers the PDC channel data transfer of the
        // transmitter. This results in a write of a data in DBGU_THR.

        qemu_chr_fe_write_all(&s->chr, &ch, 1);
        break;

    case DBGU_BRGR:
        s->reg_brgr = value;
        // TODO(at91.dbgu.brgr): update baud rate
        break;

    case DBGU_FNR:
        s->reg_fnr = value;
        qemu_log_mask(LOG_UNIMP, "at91.dbgu: unimplemented write to FNR"
                      "(size %d, value 0x%" PRIx64
                      ", offset 0x%" HWADDR_PRIx ")\n",
                      size, value, offset);
        break;

    case PDC_REG_FIRST ... PDC_REG_LAST:
        qemu_log_mask(LOG_UNIMP, "at91.dbgu: unimplemented write to PDC"
                      "(size %d, value 0x%" PRIx64
                      ", offset 0x%" HWADDR_PRIx ")\n",
                      size, value, offset);
        // TODO(at91.dbgu.pdc): implement PDC support (Sec. 23)
        break;

    default:
        error_report("at91.dbgu illegal write access at "
                      "0x%03lx [value: 0x%08lx]", offset, value);
        abort();
    }

    // TODO(at91.dbgu.irq): update interrupts
}

static const MemoryRegionOps dbgu_mmio_ops = {
    .read = dbgu_mmio_read,
    .write = dbgu_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static Property dbgu_device_properties[] = {
    DEFINE_PROP_CHR("chardev", DbguState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void dbgu_reset_registers(DbguState *s)
{
    // indicate shift register and THR empty
    s->reg_sr = SR_TXEMPTY;

    s->reg_mr   = 0x00;
    s->reg_imr  = 0x00;
    s->reg_rhr  = 0x00;
    s->reg_brgr = 0x00;
    s->reg_fnr  = 0x00;

    s->reg_cidr = IOBC_CIDR;
    s->reg_exid = IOBC_EXID;

    s->rx_enabled = false;
    s->tx_enabled = false;
}

static void dbgu_device_init(Object *obj)
{
    DbguState *s = AT91_DBGU(obj);

    memory_region_init_io(&s->mmio, OBJECT(s), &dbgu_mmio_ops, s, "at91.dbgu", 0x200);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void dbgu_device_realize(DeviceState *dev, Error **errp)
{
    DbguState *s = AT91_DBGU(dev);

    dbgu_reset_registers(s);
    qemu_chr_fe_set_handlers(&s->chr, dbgu_uart_can_receive, dbgu_uart_receive,
                             NULL, NULL, s, NULL, true);
}

static void dbgu_device_reset(DeviceState *dev)
{
    dbgu_reset_registers(AT91_DBGU(dev));
}

static void dbgu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = dbgu_device_realize;
    dc->reset = dbgu_device_reset;
    dc->props = dbgu_device_properties;
}

static const TypeInfo dbgu_device_info = {
    .name = TYPE_AT91_DBGU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DbguState),
    .instance_init = dbgu_device_init,
    .class_init = dbgu_class_init,
};

static void dbgu_register_types(void)
{
    type_register_static(&dbgu_device_info);
}

type_init(dbgu_register_types)

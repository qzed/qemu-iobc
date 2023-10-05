#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/loader.h"
#include "hw/boards.h"
#include "hw/arm/boot.h"
#include "hw/misc/unimp.h"
#include "cpu.h"

#include "iobc-reserved_memory.h"
#include "at91-pmc.h"
#include "at91-aic.h"
#include "at91-aic_stub.h"
#include "at91-dbgu.h"
#include "at91-rtt.h"
#include "at91-pit.h"
#include "at91-matrix.h"
#include "at91-rstc.h"
#include "at91-pio.h"
#include "at91-usart.h"
#include "at91-twi.h"


#define SOCKET_TWI      "/tmp/qemu_at91_twi"
#define SOCKET_USART0   "/tmp/qemu_at91_usart0"
#define SOCKET_USART1   "/tmp/qemu_at91_usart1"
#define SOCKET_USART2   "/tmp/qemu_at91_usart2"
#define SOCKET_USART3   "/tmp/qemu_at91_usart3"
#define SOCKET_USART4   "/tmp/qemu_at91_usart4"
#define SOCKET_USART5   "/tmp/qemu_at91_usart5"


static struct arm_boot_info iobc_board_binfo = {
    .loader_start     = 0x00000000,
    .ram_size         = 0x10000000,
    .nb_cpus          = 1,
};


typedef struct {
    ARMCPU *cpu;

    MemoryRegion mem_boot[__AT91_BOOTMEM_NUM_REGIONS];
    MemoryRegion mem_rom;
    MemoryRegion mem_sram0;
    MemoryRegion mem_sram1;
    MemoryRegion mem_pflash;
    MemoryRegion mem_sdram;

    DeviceState *dev_pmc;
    DeviceState *dev_aic;
    DeviceState *dev_aic_stub;
    DeviceState *dev_rstc;
    DeviceState *dev_rtt;
    DeviceState *dev_pit;
    DeviceState *dev_dbgu;
    DeviceState *dev_matrix;
    DeviceState *dev_pio_a;
    DeviceState *dev_pio_b;
    DeviceState *dev_pio_c;
    DeviceState *dev_usart0;
    DeviceState *dev_usart1;
    DeviceState *dev_usart2;
    DeviceState *dev_usart3;
    DeviceState *dev_usart4;
    DeviceState *dev_usart5;
    DeviceState *dev_twi;

    qemu_irq irq_aic[32];
    qemu_irq irq_sysc[32];

    at91_bootmem_region mem_boot_target;
} IobcBoardState;


static void iobc_bootmem_remap(void *opaque, at91_bootmem_region target)
{
    IobcBoardState *s = opaque;

    memory_region_transaction_begin();
    memory_region_set_enabled(&s->mem_boot[s->mem_boot_target], false);
    memory_region_set_enabled(&s->mem_boot[target], true);
    s->mem_boot_target = target;
    memory_region_transaction_commit();
}

static void iobc_mkclk_changed(void *opaque, unsigned clock)
{
    IobcBoardState *s = opaque;

    info_report("at91 master clock changed: %d", clock);
    at91_pit_set_master_clock(AT91_PIT(s->dev_pit), clock);
    at91_usart_set_master_clock(AT91_USART(s->dev_usart0), clock);
    at91_usart_set_master_clock(AT91_USART(s->dev_usart1), clock);
    at91_usart_set_master_clock(AT91_USART(s->dev_usart2), clock);
    at91_usart_set_master_clock(AT91_USART(s->dev_usart3), clock);
    at91_usart_set_master_clock(AT91_USART(s->dev_usart4), clock);
    at91_usart_set_master_clock(AT91_USART(s->dev_usart5), clock);
}

static void iobc_init(MachineState *machine)
{
    MemoryRegion *address_space_mem = get_system_memory();
    IobcBoardState *s = g_new(IobcBoardState, 1);
    char *firmware_path;
    int i;

    s->cpu = ARM_CPU(cpu_create(machine->cpu_type));

    /* Memory Map for AT91SAM9G20 (current implementation status)                              */
    /*                                                                                         */
    /* start        length       description        notes                                      */
    /* --------------------------------------------------------------------------------------- */
    /* 0x0000_0000  0x0010_0000  Boot Memory        Aliases SDRAM at boot (set by hardware)    */
    /* 0x0010_0000  0x0000_8000  Internal ROM                                                  */
    /* 0x0020_0000  0x0000_4000  Internal SRAM0                                                */
    /* 0x0030_0000  0x0000_4000  Internal SRAM1                                                */
    /* ...                                                                                     */
    /*                                                                                         */
    /* 0x1000_0000  0x1000_0000  NOR Program Flash  Gets loaded with program code              */
    /* 0x2000_0000  0x1000_0000  SDRAM              Copied from NOR Flash at boot via hardware */
    /* ...                                                                                     */
    /*                                                                                         */
    /* ...                                                                                     */
    /* 0xFFFB_0000  0x0000_4000  USART0                                                        */
    /* 0xFFFB_4000  0x0000_4000  USART1                                                        */
    /* 0xFFFB_8000  0x0000_4000  USART2                                                        */
    /* ...                                                                                     */
    /* 0xFFFD_0000  0x0000_4000  USART3                                                        */
    /* 0xFFFD_4000  0x0000_4000  USART4                                                        */
    /* 0xFFFD_8000  0x0000_4000  USART5                                                        */
    /* ...                                                                                     */
    /*                                                                                         */
    /* ...                                                                                     */
    /* 0xFFFF_EE00  0x0000_0200  Matrix             TODO: Only minimal implementation for now  */
    /* 0xFFFF_F000  0x0000_0200  AIC                Uses stub to OR system controller IRQs     */
    /* 0xFFFF_F200  0x0000_0200  Debug Unit (DBGU)                                             */
    /* 0xFFFF_F400  0x0000_0200  PIO A              TODO: Peripherals not connected yet        */
    /* 0xFFFF_F600  0x0000_0200  PIO B              TODO: Peripherals not connected yet        */
    /* 0xFFFF_F800  0x0000_0200  PIO C              TODO: Peripherals not connected yet        */
    /* ...                                                                                     */
    /* 0xFFFF_FC00  0x0000_0100  PMC                                                           */
    /* 0xFFFF_FD00  0x0000_0010  RSTC               TODO: Only minimal implementation for now  */
    /* ...                                                                                     */
    /* 0xFFFF_FD20  0x0000_0010  RTT                                                           */
    /* 0xFFFF_FD30  0x0000_0010  PIT                                                           */
    /* ...                                                                                     */

    // rom, ram, and flash
    memory_region_init_rom(&s->mem_rom,   NULL, "iobc.internal.rom",   0x8000, &error_fatal);
    memory_region_init_ram(&s->mem_sram0, NULL, "iobc.internal.sram0", 0x4000, &error_fatal);
    memory_region_init_ram(&s->mem_sram1, NULL, "iobc.internal.sram1", 0x4000, &error_fatal);

    memory_region_init_ram(&s->mem_pflash, NULL, "iobc.pflash", 0x10000000, &error_fatal);
    memory_region_init_ram(&s->mem_sdram,  NULL, "iobc.sdram",  0x10000000, &error_fatal);

    // bootmem aliases
    memory_region_init_alias(&s->mem_boot[AT91_BOOTMEM_ROM],   NULL, "iobc.internal.bootmem", &s->mem_rom,   0, 0x100000);
    memory_region_init_alias(&s->mem_boot[AT91_BOOTMEM_SRAM],  NULL, "iobc.internal.bootmem", &s->mem_sram0, 0, 0x100000);
    memory_region_init_alias(&s->mem_boot[AT91_BOOTMEM_SDRAM], NULL, "iobc.internal.bootmem", &s->mem_sdram, 0, 0x100000);

    // put it all together
    memory_region_add_subregion(address_space_mem, 0x00100000, &s->mem_rom);
    memory_region_add_subregion(address_space_mem, 0x00200000, &s->mem_sram0);
    memory_region_add_subregion(address_space_mem, 0x00300000, &s->mem_sram1);
    memory_region_add_subregion(address_space_mem, 0x10000000, &s->mem_pflash);
    memory_region_add_subregion(address_space_mem, 0x20000000, &s->mem_sdram);

    memory_region_transaction_begin();
    for (i = 0; i < __AT91_BOOTMEM_NUM_REGIONS; i++) {
        memory_region_set_enabled(&s->mem_boot[i], false);
        memory_region_add_subregion_overlap(address_space_mem, 0, &s->mem_boot[i], 1);
    }
    memory_region_transaction_commit();

    // map SDRAM to boot by default
    memory_region_set_enabled(&s->mem_boot[AT91_BOOTMEM_SDRAM], true);
    s->mem_boot_target = AT91_BOOTMEM_SDRAM;

    // reserved memory, accessing this will abort
    create_reserved_memory_region("iobc.undefined", 0x90000000, 0xF0000000 - 0x90000000);
    create_reserved_memory_region("iobc.periph.reserved0", 0xF0000000, 0xFFFA0000 - 0xF0000000);
    create_reserved_memory_region("iobc.periph.reserved1", 0xFFFE4000, 0xFFFFC000 - 0xFFFE4000);
    create_reserved_memory_region("iobc.periph.reserved2", 0xFFFEC000, 0xFFFFE800 - 0xFFFEC000);
    create_reserved_memory_region("iobc.periph.reserved3", 0xFFFFFA00, 0xFFFFFC00 - 0xFFFFFA00);
    create_reserved_memory_region("iobc.periph.reserved4", 0xFFFFFD60, 0x2A0);
    create_reserved_memory_region("iobc.internal.reserved0", 0x108000, 0x200000 - 0x108000);
    create_reserved_memory_region("iobc.internal.reserved1", 0x204000, 0x300000 - 0x204000);
    create_reserved_memory_region("iobc.internal.reserved2", 0x304000, 0x400000 - 0x304000);
    create_reserved_memory_region("iobc.internal.reserved3", 0x504000, 0x0FFFFFFF - 0x504000);

    // Advanced Interrupt Controller
    s->dev_aic = qdev_create(NULL, TYPE_AT91_AIC);
    qdev_init_nofail(s->dev_aic);
    sysbus_mmio_map(SYS_BUS_DEVICE(s->dev_aic), 0, 0xFFFFF000);
    sysbus_connect_irq(SYS_BUS_DEVICE(s->dev_aic), 0, qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(s->dev_aic), 1, qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_FIQ));
    for (i = 0; i < 32; i++) {
        s->irq_aic[i] = qdev_get_gpio_in_named(s->dev_aic, "irq-line", i);
    }

    // Advanced Interrupt Controller: Stub for or-ing SYSC interrupts
    s->dev_aic_stub = qdev_create(NULL, TYPE_AT91_AIC_STUB);
    qdev_init_nofail(s->dev_aic_stub);
    sysbus_connect_irq(SYS_BUS_DEVICE(s->dev_aic_stub), 0, s->irq_aic[1]);
    for (i = 0; i < 32; i++) {
        s->irq_sysc[i] = qdev_get_gpio_in_named(s->dev_aic_stub, "irq-line", i);
    }

    // Power Managemant Controller
    s->dev_pmc = sysbus_create_simple(TYPE_AT91_PMC, 0xFFFFFC00, s->irq_sysc[0]);
    at91_pmc_set_mclk_change_callback(AT91_PMC(s->dev_pmc), s, iobc_mkclk_changed);

    // Bus Matrix
    s->dev_matrix = sysbus_create_simple(TYPE_AT91_MATRIX, 0xFFFFEE00, NULL);
    at91_matrix_set_bootmem_remap_callback(AT91_MATRIX(s->dev_matrix), s, iobc_bootmem_remap);

    // Debug Unit
    s->dev_dbgu = qdev_create(NULL, TYPE_AT91_DBGU);
    qdev_prop_set_chr(s->dev_dbgu, "chardev", serial_hd(0));
    qdev_init_nofail(s->dev_dbgu);
    sysbus_mmio_map(SYS_BUS_DEVICE(s->dev_dbgu), 0, 0xFFFFF200);
    sysbus_connect_irq(SYS_BUS_DEVICE(s->dev_dbgu), 0, s->irq_sysc[1]);

    // Parallel Input Ouput Controller
    s->dev_pio_a = sysbus_create_simple(TYPE_AT91_PIO, 0xFFFFF400, s->irq_aic[2]);
    s->dev_pio_b = sysbus_create_simple(TYPE_AT91_PIO, 0xFFFFF600, s->irq_aic[3]);
    s->dev_pio_c = sysbus_create_simple(TYPE_AT91_PIO, 0xFFFFF800, s->irq_aic[4]);

    // TODO: connect PIO(A,B,C) peripheral pins

    // TWI
    s->dev_twi = qdev_create(NULL, TYPE_AT91_TWI);
    qdev_prop_set_string(s->dev_twi, "socket", SOCKET_TWI);
    qdev_init_nofail(s->dev_twi);
    sysbus_mmio_map(SYS_BUS_DEVICE(s->dev_twi), 0, 0xFFFAC000);
    sysbus_connect_irq(SYS_BUS_DEVICE(s->dev_twi), 0, s->irq_aic[11]);

    // USARTs
    s->dev_usart0 = qdev_create(NULL, TYPE_AT91_USART);
    qdev_prop_set_string(s->dev_usart0, "socket", SOCKET_USART0);
    qdev_init_nofail(s->dev_usart0);
    sysbus_mmio_map(SYS_BUS_DEVICE(s->dev_usart0), 0, 0xFFFB0000);
    sysbus_connect_irq(SYS_BUS_DEVICE(s->dev_usart0), 0, s->irq_aic[6]);

    s->dev_usart1 = qdev_create(NULL, TYPE_AT91_USART);
    qdev_prop_set_string(s->dev_usart1, "socket", SOCKET_USART1);
    qdev_init_nofail(s->dev_usart1);
    sysbus_mmio_map(SYS_BUS_DEVICE(s->dev_usart1), 0, 0xFFFB4000);
    sysbus_connect_irq(SYS_BUS_DEVICE(s->dev_usart1), 0, s->irq_aic[7]);

    s->dev_usart2 = qdev_create(NULL, TYPE_AT91_USART);
    qdev_prop_set_string(s->dev_usart2, "socket", SOCKET_USART2);
    qdev_init_nofail(s->dev_usart2);
    sysbus_mmio_map(SYS_BUS_DEVICE(s->dev_usart2), 0, 0xFFFB8000);
    sysbus_connect_irq(SYS_BUS_DEVICE(s->dev_usart2), 0, s->irq_aic[8]);

    s->dev_usart3 = qdev_create(NULL, TYPE_AT91_USART);
    qdev_prop_set_string(s->dev_usart3, "socket", SOCKET_USART3);
    qdev_init_nofail(s->dev_usart3);
    sysbus_mmio_map(SYS_BUS_DEVICE(s->dev_usart3), 0, 0xFFFD0000);
    sysbus_connect_irq(SYS_BUS_DEVICE(s->dev_usart3), 0, s->irq_aic[23]);

    s->dev_usart4 = qdev_create(NULL, TYPE_AT91_USART);
    qdev_prop_set_string(s->dev_usart4, "socket", SOCKET_USART4);
    qdev_init_nofail(s->dev_usart4);
    sysbus_mmio_map(SYS_BUS_DEVICE(s->dev_usart4), 0, 0xFFFD4000);
    sysbus_connect_irq(SYS_BUS_DEVICE(s->dev_usart4), 0, s->irq_aic[24]);

    s->dev_usart5 = qdev_create(NULL, TYPE_AT91_USART);
    qdev_prop_set_string(s->dev_usart5, "socket", SOCKET_USART5);
    qdev_init_nofail(s->dev_usart5);
    sysbus_mmio_map(SYS_BUS_DEVICE(s->dev_usart5), 0, 0xFFFD8000);
    sysbus_connect_irq(SYS_BUS_DEVICE(s->dev_usart5), 0, s->irq_aic[25]);

    // other peripherals
    s->dev_rstc = sysbus_create_simple(TYPE_AT91_RSTC, 0xFFFFFD00, s->irq_sysc[2]);
    s->dev_rtt  = sysbus_create_simple(TYPE_AT91_RTT,  0xFFFFFD20, s->irq_sysc[3]);
    s->dev_pit  = sysbus_create_simple(TYPE_AT91_PIT,  0xFFFFFD30, s->irq_sysc[4]);

    // currently unimplemented things...
    create_unimplemented_device("iobc.internal.uhp",   0x00500000, 0x4000);

    create_unimplemented_device("iobc.ebi.cs2",        0x30000000, 0x10000000);
    create_unimplemented_device("iobc.ebi.cs3",        0x40000000, 0x10000000);
    create_unimplemented_device("iobc.ebi.cs4",        0x50000000, 0x10000000);
    create_unimplemented_device("iobc.ebi.cs5",        0x60000000, 0x10000000);
    create_unimplemented_device("iobc.ebi.cs6",        0x70000000, 0x10000000);
    create_unimplemented_device("iobc.ebi.cs7",        0x80000000, 0x10000000);

    create_unimplemented_device("iobc.periph.tc012",   0xFFFA0000, 0x4000);
    create_unimplemented_device("iobc.periph.udp",     0xFFFA4000, 0x4000);
    create_unimplemented_device("iobc.periph.mci",     0xFFFA8000, 0x4000);
    create_unimplemented_device("iobc.periph.ssc",     0xFFFBC000, 0x4000);
    create_unimplemented_device("iobc.periph.isi",     0xFFFC0000, 0x4000);
    create_unimplemented_device("iobc.periph.emac",    0xFFFC4000, 0x4000);
    create_unimplemented_device("iobc.periph.spi0",    0xFFFC8000, 0x4000);
    create_unimplemented_device("iobc.periph.spi1",    0xFFFCC000, 0x4000);
    create_unimplemented_device("iobc.periph.tc345",   0xFFFDC000, 0x4000);
    create_unimplemented_device("iobc.periph.adc",     0xFFFE0000, 0x4000);

    create_unimplemented_device("iobc.periph.ecc",     0xFFFFE800, 0x200);
    create_unimplemented_device("iobc.periph.sdramc",  0xFFFFEA00, 0x200);
    create_unimplemented_device("iobc.periph.smc",     0xFFFFEC00, 0x200);

    create_unimplemented_device("iobc.periph.shdwc",   0xFFFFFD10, 0x10);
    create_unimplemented_device("iobc.periph.wdt",     0xFFFFFD40, 0x10);
    create_unimplemented_device("iobc.periph.gpbr",    0xFFFFFD50, 0x10);

    // load firmware
    if (bios_name) {
        firmware_path = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);

        if (firmware_path) {
            // load into nor flash (default program store)
            if (load_image_mr(firmware_path, &s->mem_pflash) < 0) {
                error_report("Unable to load %s into pflash", bios_name);
                exit(1);
            }

            // nor flash gets copied to sdram at boot, thus we load it directly
            if (load_image_mr(firmware_path, &s->mem_sdram) < 0) {
                error_report("Unable to load %s into sdram", bios_name);
                exit(1);
            }

            g_free(firmware_path);
        } else {
            error_report("Unable to find %s", bios_name);
            exit(1);
        }
    } else {
        warn_report("No firmware specified: Use -bios <file> to load firmware");
    }

    arm_load_kernel(s->cpu, &iobc_board_binfo);
}

static void iobc_machine_init(MachineClass *mc)
{
    mc->desc = "ISIS-OBC for CubeSat";
    mc->init = iobc_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm926");
}

DEFINE_MACHINE("isis-obc", iobc_machine_init)

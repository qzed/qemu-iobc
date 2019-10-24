#ifndef HW_ARM_ISIS_OBC_PMC_H
#define HW_ARM_ISIS_OBC_PMC_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"


#define TYPE_IOBC_PMC "iobc.peripherals.pmc"
#define IOBC_PMC(obj) OBJECT_CHECK(PmcState, (obj), TYPE_IOBC_PMC)


typedef struct {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    uint32_t reg[33];
} PmcState;


#endif /* HW_ARM_ISIS_OBC_PWC_H */

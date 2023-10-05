#ifndef HW_ARM_ISIS_OBC_PDC_H
#define HW_ARM_ISIS_OBC_PDC_H

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/sysbus.h"


#define PDC_START       0x100
#define PDC_END         0x128

#define PDC_RPR         0x100
#define PDC_RCR         0x104
#define PDC_TPR         0x108
#define PDC_TCR         0x10C
#define PDC_RNPR        0x110
#define PDC_RNCR        0x114
#define PDC_TNPR        0x118
#define PDC_TNCR        0x11C
#define PDC_PTCR        0x120
#define PDC_PTSR        0x124

#define PTCR_RXTEN      BIT(0)
#define PTCR_RXTDIS     BIT(1)
#define PTCR_TXTEN      BIT(8)
#define PTCR_TXTDIS     BIT(9)

#define PTSR_RXTEN      BIT(0)
#define PTSR_TXTEN      BIT(8)


typedef struct At91Pdc {
    uint32_t reg_ptsr;

    uint32_t reg_rpr;
    uint32_t reg_rnpr;
    uint32_t reg_tpr;
    uint32_t reg_tnpr;

    uint16_t reg_rcr;
    uint16_t reg_rncr;
    uint16_t reg_tcr;
    uint16_t reg_tncr;
} At91Pdc;

enum at91_pdc_action {
    AT91_PDC_ACTION_NONE = 0,
    AT91_PDC_ACTION_STATE,
    AT91_PDC_ACTION_START_RX,
    AT91_PDC_ACTION_STOP_RX,
    AT91_PDC_ACTION_START_TX,
    AT91_PDC_ACTION_STOP_TX,
};


inline static void at91_pdc_reset_registers(At91Pdc *pdc)
{
    pdc->reg_rpr = 0;
    pdc->reg_rcr = 0;
    pdc->reg_tpr = 0;
    pdc->reg_tcr = 0;
    pdc->reg_rnpr = 0;
    pdc->reg_rncr = 0;
    pdc->reg_tnpr = 0;
    pdc->reg_tncr = 0;
    pdc->reg_ptsr = 0;
}

inline static uint32_t at91_pdc_get_register(At91Pdc *pdc, hwaddr offset)
{
    switch (offset) {
    case PDC_RPR:
        return pdc->reg_rpr;

    case PDC_RCR:
        return pdc->reg_rcr;

    case PDC_TPR:
        return pdc->reg_tpr;

    case PDC_TCR:
        return pdc->reg_tcr;

    case PDC_RNPR:
        return pdc->reg_rnpr;

    case PDC_RNCR:
        return pdc->reg_rncr;

    case PDC_TNPR:
        return pdc->reg_tnpr;

    case PDC_TNCR:
        return pdc->reg_tncr;

    case PDC_PTSR:
        return pdc->reg_ptsr;

    default:
        error_report("at91.pdc illegal write access at 0x%03lx", offset);
        abort();
    }
}

inline static
enum at91_pdc_action at91_pdc_set_register(At91Pdc *pdc, hwaddr offset, uint32_t value)
{
    switch (offset) {
    case PDC_RPR:
        pdc->reg_rpr = value;
        return AT91_PDC_ACTION_NONE;

    case PDC_RCR:
        pdc->reg_rcr = value;

        if (pdc->reg_ptsr & PTSR_RXTEN) {
            return value ? AT91_PDC_ACTION_START_RX : AT91_PDC_ACTION_STOP_RX;
        } else {
            return AT91_PDC_ACTION_NONE;
        }

    case PDC_TPR:
        pdc->reg_tpr = value;
        return AT91_PDC_ACTION_NONE;

    case PDC_TCR:
        pdc->reg_tcr = value;

        if (pdc->reg_ptsr & PTSR_TXTEN) {
            return value ? AT91_PDC_ACTION_START_TX : AT91_PDC_ACTION_STOP_TX;
        } else {
            return AT91_PDC_ACTION_NONE;
        }

    case PDC_RNPR:
        pdc->reg_rnpr = value;
        return AT91_PDC_ACTION_NONE;

    case PDC_RNCR:
        pdc->reg_rncr = value;
        return AT91_PDC_ACTION_NONE;

    case PDC_TNPR:
        pdc->reg_tnpr = value;
        return AT91_PDC_ACTION_NONE;

    case PDC_TNCR:
        pdc->reg_tncr = value;
        return AT91_PDC_ACTION_NONE;

    case PDC_PTCR:
        if ((value & PTCR_RXTEN) && !(value & PTCR_RXTDIS)) {
            pdc->reg_ptsr |= PTSR_RXTEN;
        }
        if (value & PTCR_RXTDIS) {
            pdc->reg_ptsr &= ~PTSR_RXTEN;
        }
        if ((value & PTCR_TXTEN) && !(value & PTCR_TXTDIS)) {
            pdc->reg_ptsr |= PTSR_TXTEN;
        }
        if (value & PTCR_TXTDIS) {
            pdc->reg_ptsr &= ~PTSR_TXTEN;
        }
        return AT91_PDC_ACTION_STATE;

    default:
        error_report("at91.pdc: illegal read access at 0x%03lx"
                     " [value: 0x%08x]", offset, value);
        abort();
    }
}

inline static
enum at91_pdc_action at91_pdc_set_register_hd(At91Pdc *pdc, hwaddr offset, uint32_t value)
{
    switch (offset) {
    case PDC_RPR:
    case PDC_TPR:
        pdc->reg_rpr = value;
        pdc->reg_tpr = value;
        return AT91_PDC_ACTION_NONE;

    case PDC_RCR:
    case PDC_TCR:
        pdc->reg_rcr = value;
        pdc->reg_tcr = value;

        if (pdc->reg_ptsr & PTSR_TXTEN) {
            return value ? AT91_PDC_ACTION_START_TX : AT91_PDC_ACTION_STOP_TX;
        } else if (pdc->reg_ptsr & PTSR_RXTEN) {
            return value ? AT91_PDC_ACTION_START_RX : AT91_PDC_ACTION_STOP_RX;
        } else {
            return AT91_PDC_ACTION_NONE;
        }

    case PDC_RNPR:
    case PDC_TNPR:
        pdc->reg_rnpr = value;
        pdc->reg_tnpr = value;
        return AT91_PDC_ACTION_NONE;

    case PDC_RNCR:
        pdc->reg_rncr = value;
        pdc->reg_tncr = value;
        return AT91_PDC_ACTION_NONE;

    case PDC_PTCR:
        if ((value & PTCR_RXTEN) && (value & PTCR_TXTEN)) {
            // SPEC: It is forbidden to set both TXTEN and RXTEN for a half
            // duplex peripheral.
            error_report("at91.pdc: cannot set both RXTEN and TXTEN on half-duplex device");
            abort();
        }
        if ((value & PTCR_RXTEN) && !(value & PTCR_RXTDIS)) {
            // SPEC: When a half duplex peripheral is connected to the PDC,
            // enabling the receiver channel requests automatically disables
            // the transmitter channel requests.
            pdc->reg_ptsr = (pdc->reg_ptsr | PTSR_RXTEN) & ~PTSR_TXTEN;
        }
        if (value & PTCR_RXTDIS) {
            // SPEC: When a half duplex peripheral is connected to the PDC,
            // disabling the receiver channel requests also disables the
            // transmitter channel requests.
            pdc->reg_ptsr &= ~(PTSR_RXTEN | PTSR_TXTEN);
        }
        if ((value & PTCR_TXTEN) && !(value & PTCR_TXTDIS)) {
            // SPEC: When a half duplex peripheral is connected to the PDC, it
            // enables the transmitter channel requests only if RXTEN is not
            // set.
            if (!(pdc->reg_ptsr & PTSR_RXTEN)) {
                pdc->reg_ptsr |= PTSR_TXTEN;
            }
        }
        if (value & PTCR_TXTDIS) {
            // SPEC: When a half duplex peripheral is connected to the PDC,
            // disabling the transmitter channel requests disables the receiver
            // channel requests.
            pdc->reg_ptsr &= ~(PTSR_RXTEN | PTSR_TXTEN);
        }
        return AT91_PDC_ACTION_STATE;

    default:
        error_report("at91.pdc: illegal read access at 0x%03lx"
                     " [value: 0x%08x]", offset, value);
        abort();
    }
}

#endif /* HW_ARM_ISIS_OBC_PDC_H */

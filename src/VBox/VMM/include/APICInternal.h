/* $Id$ */
/** @file
 * APIC - Advanced Programmable Interrupt Controller.
 */

/*
 * Copyright (C) 2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___APICInternal_h
#define ___APICInternal_h

#include <VBox/sup.h>
#include <VBox/types.h>
#include <VBox/vmm/apic.h>

/** @defgroup grp_apic_int       Internal
 * @ingroup grp_apic
 * @internal
 * @{
 */

#define VMCPU_TO_XAPICPAGE(a_pVCpu)          ((PXAPICPAGE)(CTX_SUFF((a_pVCpu)->apic.s.pvApicPage)))
#define VMCPU_TO_CXAPICPAGE(a_pVCpu)         ((PCXAPICPAGE)(CTX_SUFF((a_pVCpu)->apic.s.pvApicPage)))

#define VMCPU_TO_X2APICPAGE(a_pVCpu)         ((PX2APICPAGE)(CTX_SUFF((a_pVCpu)->apic.s.pvApicPage)))
#define VMCPU_TO_CX2APICPAGE(a_pVCpu)        ((PCX2APICPAGE)(CTX_SUFF((a_pVCpu)->apic.s.pvApicPage)))

#define VMCPU_TO_APICCPU(a_pVCpu)            (&(a_pVCpu)->apic.s)
#define VM_TO_APIC(a_pVM)                    (&(a_pVM)->apic.s)
#define VM_TO_APICDEV(a_pVM)                 CTX_SUFF(VM_TO_APIC(a_pVM)->pApicDev)

#define APICCPU_TO_XAPICPAGE(a_ApicCpu)      ((PXAPICPAGE)(CTX_SUFF((a_ApicCpu)->pvApicPage)))
#define APICCPU_TO_CXAPICPAGE(a_ApicCpu)     ((PCXAPICPAGE)(CTX_SUFF((a_ApicCpu)->pvApicPage)))


/** Whether the APIC is in X2APIC mode or not. */
#define XAPIC_IN_X2APIC_MODE(a_pVCpu)        RT_BOOL((((a_pVCpu)->apic.s.uApicBaseMsr) & (MSR_APICBASE_XAPIC_ENABLE_BIT | MSR_APICBASE_X2APIC_ENABLE_BIT)) \
                                                     == (MSR_APICBASE_XAPIC_ENABLE_BIT | MSR_APICBASE_X2APIC_ENABLE_BIT))
/** Get an xAPIC page offset for an x2APIC MSR value. */
#define X2APIC_GET_XAPIC_OFF(a_uMsr)         ((((a_uMsr) - MSR_IA32_X2APIC_START) << 4) & UINT32_C(0xff0))
/** Get an x2APIC MSR for an xAPIC page offset. */
#define XAPIC_GET_X2APIC_MSR(a_offReg)       ((((a_offReg) & UINT32_C(0xff0)) >> 4) | MSR_IA32_X2APIC_START)

/** Illegal APIC vector value start. */
#define XAPIC_ILLEGAL_VECTOR_START           UINT8_C(0)
/** Illegal APIC vector value end (inclusive). */
#define XAPIC_ILLEGAL_VECTOR_END             UINT8_C(15)
/** Reserved APIC vector value start. */
#define XAPIC_RSVD_VECTOR_START              UINT8_C(16)
/** Reserved APIC vector value end (inclusive). */
#define XAPIC_RSVD_VECTOR_END                UINT8_C(31)

/** Vector offset in an APIC 256-bit sparse register. */
#define XAPIC_REG256_VECTOR_OFF(a_Vector)    (((a_Vector) & UINT32_C(0xe0)) >> 1)
/** Bit position at offset in an APIC 256-bit sparse register. */
#define XAPIC_REG256_VECTOR_BIT(a_Vector)    ((a_Vector) & UINT32_C(0x1f))

/** Maximum number of LVT entries for Pentium 4. */
#define XAPIC_MAX_LVT_ENTRIES_P4             UINT8_C(6)
/** Size of the APIC ID bits for Pentium 4. */
#define XAPIC_APIC_ID_BIT_COUNT_P4           UINT8_C(8)
/** The APIC hardware version number for Pentium 4. */
#define XAPIC_HARDWARE_VERSION_P4            UINT8_C(0x14)

/** Maximum number of LVT entries for Pentium 6. */
#define XAPIC_MAX_LVT_ENTRIES_P6             UINT8_C(4)
/** Size of the APIC ID bits for Pentium 6. */
#define XAPIC_APIC_ID_BIT_COUNT_P6           UINT8_C(4)
/** The APIC hardware version number for Pentium 6. */
#define XAPIC_HARDWARE_VERSION_P6            UINT8_C(0x10)

/** Maximum valid offset for a register (16-byte aligned, 4 byte wide access). */
#define XAPIC_OFF_MAX_VALID                  (sizeof(XAPICPAGE) - 4 * sizeof(uint32_t))

#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P6
/** ESR - Send checksum error. */
# define XAPIC_ESR_SEND_CHKSUM_ERROR         RT_BIT(0)
/** ESR - Send accept error. */
# define XAPIC_ESR_RECV_CHKSUM_ERROR         RT_BIT(1)
/** ESR - Send accept error. */
# define XAPIC_ESR_SEND_ACCEPT_ERROR         RT_BIT(2)
/** ESR - Receive accept error. */
# define XAPIC_ESR_RECV_ACCEPT_ERROR         RT_BIT(3)
#endif
/** ESR - Redirectable IPI. */
#define XAPIC_ESR_REDIRECTABLE_IPI           RT_BIT(4)
/** ESR - Send accept error. */
#define XAPIC_ESR_SEND_ILLEGAL_VECTOR        RT_BIT(5)
/** ESR - Send accept error. */
#define XAPIC_ESR_RECV_ILLEGAL_VECTOR        RT_BIT(6)
/** ESR - Send accept error. */
#define XAPIC_ESR_ILLEGAL_REG_ADDRESS        RT_BIT(7)
/** ESR - Valid write-only bits. */
#define XAPIC_ESR_WO                         UINT32_C(0x0)

/** TPR - Valid bits. */
#define XAPIC_TPR                            UINT32_C(0xff)
/** TPR - Task-priority class. */
#define XAPIC_TPR_TP                         UINT32_C(0xf0)
/** TPR - Task-priority subclass. */
#define XAPIC_TPR_TP_SUBCLASS                UINT32_C(0x0f)
/** TPR - Get the task-priority class. */
#define XAPIC_TPR_GET_TP(a_Tpr)              ((a_Tpr) & XAPIC_TPR_TP)
/** TPR - Get the task-priority subclass. */
#define XAPIC_TPR_GET_TP_SUBCLASS(a_Tpr)     ((a_Tpr) & XAPIC_TPR_TP_SUBCLASS)

/** PPR - Valid bits. */
#define XAPIC_PPR                            UINT32_C(0xff)
/** PPR - Processor-priority class. */
#define XAPIC_PPR_PP                         UINT32_C(0xf0)
/** PPR - Processor-priority subclass. */
#define XAPIC_PPR_PP_SUBCLASS                UINT32_C(0x0f)
/** PPR - Get the processor-priority class. */
#define XAPIC_PPR_GET_PP(a_Ppr)              ((a_Ppr) & XAPIC_PPR_PP)
/** PPR - Get the processor-priority subclass. */
#define XAPIC_PPR_GET_PP_SUBCLASS(a_Ppr)     ((a_Ppr) & XAPIC_PPR_PP_SUBCLASS)

/** Timer mode - One-shot. */
#define XAPIC_TIMER_MODE_ONESHOT             UINT32_C(0)
/** Timer mode - Periodic. */
#define XAPIC_TIMER_MODE_PERIODIC            UINT32_C(1)
/** Timer mode - TSC deadline. */
#define XAPIC_TIMER_MODE_TSC_DEADLINE        UINT32_C(2)

/** LVT - The vector. */
#define XAPIC_LVT_VECTOR                     UINT32_C(0xff)
/** LVT - Gets the vector from an LVT entry. */
#define XAPIC_LVT_GET_VECTOR(a_Lvt)          ((a_Lvt) & XAPIC_LVT_VECTOR)
/** LVT - The mask. */
#define XAPIC_LVT_MASK                       RT_BIT(16)
/** LVT - Is the LVT masked? */
#define XAPIC_LVT_IS_MASKED(a_Lvt)           RT_BOOL((a_Lvt) & XAPIC_LVT_MASK)
/** LVT - Timer mode. */
#define XAPIC_LVT_TIMER_MODE                 RT_BIT(17)
/** LVT - Timer TSC-deadline timer mode. */
#define XAPIC_LVT_TIMER_TSCDEADLINE          RT_BIT(18)
/** LVT - Gets the timer mode. */
#define XAPIC_LVT_GET_TIMER_MODE(a_Lvt)      (XAPICTIMERMODE)(((a_Lvt) >> 17) & UINT32_C(3))
/** LVT - Delivery mode. */
#define XAPIC_LVT_DELIVERY_MODE              (RT_BIT(8) | RT_BIT(9) | RT_BIT(10))
/** LVT - Gets the delivery mode. */
#define XAPIC_LVT_GET_DELIVERY_MODE(a_Lvt)   (XAPICDELIVERYMODE)(((a_Lvt) >> 8) & UINT32_C(7))
/** LVT - Delivery status. */
#define XAPIC_LVT_DELIVERY_STATUS            RT_BIT(12)
/** LVT - Trigger mode. */
#define XAPIC_LVT_TRIGGER_MODE               RT_BIT(15)
/** LVT - Gets the trigger mode. */
#define XAPIC_LVT_GET_TRIGGER_MODE(a_Lvt)    (XAPICTRIGGERMODE)(((a_Lvt) >> 15) & UINT32_C(1))
/** LVT - Remote IRR. */
#define XAPIC_LVT_REMOTE_IRR                 RT_BIT(14)
/** LVT - Interrupt Input Pin Polarity. */
#define XAPIC_LVT_INTR_INPUT_PIN_POLARITY    RT_BIT(13)
/** LVT - Valid bits common to all LVTs. */
#define XAPIC_LVT_COMMON                     (XAPIC_LVT_VECTOR | XAPIC_LVT_DELIVERY_STATUS | XAPIC_LVT_MASK)
/** LVT CMCI - Valid bits. */
#define XAPIC_LVT_CMCI_VALID                 (XAPIC_LVT_COMMON | XAPIC_LVT_DELIVERY_MODE)
/** LVT Timer - Valid bits. */
#define XAPIC_LVT_TIMER                      (XAPIC_LVT_COMMON | XAPIC_LVT_TIMER_MODE | XAPIC_LVT_TIMER_TSCDEADLINE)
/** LVT Thermal - Valid bits. */
#define XAPIC_LVT_THERMAL                    (XAPIC_LVT_COMMON | XAPIC_LVT_DELIVERY_MODE)
/** LVT Perf - Valid bits. */
#define XAPIC_LVT_PERF                       (XAPIC_LVT_COMMON | XAPIC_LVT_DELIVERY_MODE)
/** LVT LINT0 - Valid bits. */
#define XAPIC_LVT_LINT0                      (  XAPIC_LVT_COMMON | XAPIC_LVT_DELIVERY_MODE | XAPIC_LVT_DELIVERY_STATUS \
                                              | XAPIC_LVT_INTR_INPUT_PIN_POLARITY | XAPIC_LVT_REMOTE_IRR)
/** LVT LINT1 - Valid bits. */
#define XAPIC_LVT_LINT1                      (  XAPIC_LVT_COMMON | XAPIC_LVT_DELIVERY_MODE | XAPIC_LVT_DELIVERY_STATUS \
                                              | XAPIC_LVT_INTR_INPUT_PIN_POLARITY | XAPIC_LVT_REMOTE_IRR)
/** LVT Error - Valid bits. */
#define XAPIC_LVT_ERROR                      (XAPIC_LVT_COMMON)

/** SVR - The vector. */
#define XAPIC_SVR_VECTOR                     UINT32_C(0xff)
/** SVR - APIC Software enable. */
#define XAPIC_SVR_SOFTWARE_ENABLE            RT_BIT(8)
/** SVR - Supress EOI broadcast. */
#define XAPIC_SVR_SUPRESS_EOI_BROADCAST      RT_BIT(12)
#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
/** SVR - Valid bits. */
# define XAPIC_SVR                           (XAPIC_SVR_VECTOR | XAPIC_SVR_SOFTWARE_ENABLE)
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif

/** DFR - Valid bits. */
#define XAPIC_DFR                            UINT32_C(0xf0000000)
/** DFR - Reserved bits that must always remain set. */
#define XAPIC_DFR_RSVD_MB1                   UINT32_C(0x0fffffff)
/** DFR - The model. */
#define XAPIC_DFR_MODEL                      UINT32_C(0xf)
/** DFR - Gets the destination model. */
#define XAPIC_DFR_GET_MODEL(a_uReg)          (((a_uReg) >> 28) & XAPIC_DFR_MODEL)

/** LDR - Valid bits. */
#define XAPIC_LDR                            UINT32_C(0xff000000)
/** LDR - Cluster ID mask (x2APIC). */
#define X2APIC_LDR_CLUSTER_ID                UINT32_C(0xffff0000)
/** LDR - Mask of the LDR cluster ID (x2APIC). */
#define X2APIC_LDR_GET_CLUSTER_ID(a_uReg)    ((a_uReg) & X2APIC_LDR_CLUSTER_ID)
/** LDR - Mask of the LDR logical ID (x2APIC). */
#define X2APIC_LDR_LOGICAL_ID                UINT32_C(0x0000ffff)

/** LDR - Flat mode logical ID mask. */
#define XAPIC_LDR_FLAT_LOGICAL_ID            UINT32_C(0xff)
/** LDR - Clustered mode cluster ID mask. */
#define XAPIC_LDR_CLUSTERED_CLUSTER_ID       UINT32_C(0xf0)
/** LDR - Clustered mode logical ID mask. */
#define XAPIC_LDR_CLUSTERED_LOGICAL_ID       UINT32_C(0x0f)
/** LDR - Gets the clustered mode cluster ID. */
#define XAPIC_LDR_CLUSTERED_GET_CLUSTER_ID(a_uReg)   ((a_uReg) & XAPIC_LDR_CLUSTERED_CLUSTER_ID)


/** EOI - Valid write-only bits. */
#define XAPIC_EOI_WO                         UINT32_C(0x0)
/** Timer ICR - Valid bits. */
#define XAPIC_TIMER_ICR                      UINT32_C(0xffffffff)
/** Timer DCR - Valid bits. */
#define XAPIC_TIMER_DCR                      (RT_BIT(0) | RT_BIT(1) | RT_BIT(3))

/** Self IPI - Valid bits. */
#define XAPIC_SELF_IPI                       UINT32_C(0xff)
/** Self IPI - The vector. */
#define XAPIC_SELF_IPI_VECTOR                UINT32_C(0xff)
/** Self IPI - Gets the vector. */
#define XAPIC_SELF_IPI_GET_VECTOR(a_uReg)    ((a_uReg) & XAPIC_SELF_IPI_VECTOR)

/** ICR Low - The Vector. */
#define XAPIC_ICR_LO_VECTOR                  UINT32_C(0xff)
/** ICR Low - Gets the vector. */
#define XAPIC_ICR_LO_GET_VECTOR(a_uIcr)      ((a_uIcr) & XAPIC_ICR_LO_VECTOR)
/** ICR Low - The delivery mode. */
#define XAPIC_ICR_LO_DELIVERY_MODE           (RT_BIT(8) | RT_BIT(9) | RT_BIT(10))
/** ICR Low - The destination mode. */
#define XAPIC_ICR_LO_DEST_MODE               RT_BIT(11)
/** ICR Low - The delivery status. */
#define XAPIC_ICR_LO_DELIVERY_STATUS         RT_BIT(12)
/** ICR Low - The level. */
#define XAPIC_ICR_LO_LEVEL                   RT_BIT(14)
/** ICR Low - The trigger mode. */
#define XAPIC_ICR_TRIGGER_MODE               RT_BIT(15)
/** ICR Low - The destination shorthand. */
#define XAPIC_ICR_LO_DEST_SHORTHAND          (RT_BIT(18) | RT_BIT(19))
/** ICR Low - Valid write bits. */
#define XAPIC_ICR_LO_WR                      (  XAPIC_ICR_LO_VECTOR | XAPIC_ICR_LO_DELIVERY_MODE | XAPIC_ICR_LO_DEST_MODE \
                                              | XAPIC_ICR_LO_LEVEL | XAPIC_ICR_TRIGGER_MODE | XAPIC_ICR_LO_DEST_SHORTHAND)

/** ICR High - The destination field. */
#define XAPIC_ICR_HI_DEST                    UINT32_C(0xff000000)
/** ICR High - Get the destination field. */
#define XAPIC_ICR_HI_GET_DEST(a_u32IcrHi)    (((a_u32IcrHi) >> 24) & XAPIC_ICR_HI_DEST)
/** ICR High - Valid write bits in xAPIC mode. */
#define XAPIC_ICR_HI_WR                      XAPIC_ICR_HI_DEST

/** APIC ID broadcast mask - x2APIC mode. */
#define X2APIC_ID_BROADCAST_MASK             UINT32_C(0xffffffff)
#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
/** APIC ID broadcast mask - xAPIC mode. */
# define XAPIC_ID_BROADCAST_MASK             UINT32_C(0xff)
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif

/**
 * APIC MSR access error.
 * @note The values must match the array indices in apicMsrAccessError().
 */
typedef enum APICMSRACCESS
{
    /* MSR read while not in x2APIC. */
    APICMSRACCESS_INVALID_READ_MODE = 0,
    /* MSR write while not in x2APIC. */
    APICMSRACCESS_INVALID_WRITE_MODE,
    /* MSR read for a reserved/unknown/invalid MSR. */
    APICMSRACCESS_READ_RSVD_OR_UNKNOWN,
    /* MSR write for a reserved/unknown/invalid MSR. */
    APICMSRACCESS_WRITE_RSVD_OR_UNKNOWN,
    /* MSR read for a write-only MSR. */
    APICMSRACCESS_READ_WRITE_ONLY,
    /* MSR write for a read-only MSR. */
    APICMSRACCESS_WRITE_READ_ONLY,
    /* MSR read to reserved bits. */
    APICMSRACCESS_READ_RSVD_BITS,
    /* MSR write to reserved bits. */
    APICMSRACCESS_WRITE_RSVD_BITS,
    /* MSR write with invalid value. */
    APICMSRACCESS_WRITE_INVALID,
    /* Count of enum members (don't use). */
    APICMSRACCESS_COUNT
} APICMSRACCESS;

/** @name xAPIC Destination Format Register bits.
 * See Intel spec. 10.6.2.2 "Logical Destination Mode".
 * @{ */
typedef enum XAPICDESTFORMAT
{
    XAPICDESTFORMAT_FLAT    = 0xf,
    XAPICDESTFORMAT_CLUSTER = 0
} XAPICDESTFORMAT;
/** @} */

/** @name xAPIC Timer Mode bits.
 * See Intel spec. 10.5.1 "Local Vector Table".
 * @{ */
typedef enum XAPICTIMERMODE
{
    XAPICTIMERMODE_ONESHOT      = XAPIC_TIMER_MODE_ONESHOT,
    XAPICTIMERMODE_PERIODIC     = XAPIC_TIMER_MODE_PERIODIC,
    XAPICTIMERMODE_TSC_DEADLINE = XAPIC_TIMER_MODE_TSC_DEADLINE
} XAPICTIMERMODE;
/** @} */

/** @name xAPIC Interrupt Command Register bits.
 * See Intel spec. 10.6.1 "Interrupt Command Register (ICR)".
 * See Intel spec. 10.5.1 "Local Vector Table".
 * @{ */
/**
 * xAPIC trigger mode.
 */
typedef enum XAPICTRIGGERMODE
{
    XAPICTRIGGERMODE_EDGE = 0,
    XAPICTRIGGERMODE_LEVEL
} XAPICTRIGGERMODE;

/**
 * xAPIC destination shorthand.
 */
typedef enum XAPICDESTSHORTHAND
{
    XAPICDESTSHORTHAND_NONE = 0,
    XAPICDESTSHORTHAND_SELF,
    XAPIDDESTSHORTHAND_ALL_INCL_SELF,
    XAPICDESTSHORTHAND_ALL_EXCL_SELF
} XAPICDESTSHORTHAND;

/**
 * xAPIC INIT level de-assert delivery mode.
 */
typedef enum XAPICINITLEVEL
{
    XAPICINITLEVEL_DEASSERT = 0,
    XAPICINITLEVEL_ASSERT
} XAPICLEVEL;

/**
 * xAPIC destination mode.
 */
typedef enum XAPICDESTMODE
{
    XAPICDESTMODE_PHYSICAL = 0,
    XAPICDESTMODE_LOGICAL
} XAPICDESTMODE;

/**
 * xAPIC delivery mode type.
 */
typedef enum XAPICDELIVERYMODE
{
    XAPICDELIVERYMODE_FIXED               = 0,
    XAPICDELIVERYMODE_LOWEST_PRIO         = 1,
    XAPICDELIVERYMODE_SMI                 = 2,
    XAPICDELIVERYMODE_NMI                 = 4,
    XAPICDELIVERYMODE_INIT                = 5,
    XAPICDELIVERYMODE_INIT_LEVEL_DEASSERT = 5,  /* Yes, also 5. */
    XAPICDELIVERYMODE_STARTUP             = 6,
    XAPICDELIVERYMODE_EXTINT              = 7
} XAPICDELIVERYMODE;
/** @} */

/**
 * APIC operating modes.
 *
 * The values match hardware states.
 * See Intel spec. 10.12.1 "Detecting and Enabling x2APIC Mode".
 */
typedef enum APICMODE
{
    APICMODE_DISABLED = 0,
    APICMODE_INVALID,
    APICMODE_XAPIC,
    APICMODE_X2APIC
} APICMODE;

/**
 * APIC PDM instance data (per-VM).
 */
typedef struct APICDEV
{
    /** The device instance - R3 Ptr. */
    PPDMDEVINSR3                pDevInsR3;
    /** The APIC helpers - R3 Ptr. */
    PCPDMAPICHLPR3              pApicHlpR3;
    /** The PDM critical section - R3 Ptr. */
    R3PTRTYPE(PPDMCRITSECT)     pCritSectR3;

    /** The device instance - R0 Ptr. */
    PPDMDEVINSR0                pDevInsR0;
    /** The APIC helpers - R0 Ptr. */
    PCPDMAPICHLPR0              pApicHlpR0;
    /** The PDM critical section - R0 Ptr. */
    R0PTRTYPE(PPDMCRITSECT)     pCritSectR0;

    /** The device instance - RC Ptr. */
    PPDMDEVINSRC                pDevInsRC;
    /** The APIC helpers - RC Ptr. */
    PCPDMAPICHLPRC              pApicHlpRC;
    /** The PDM critical section - RC Ptr. */
    RCPTRTYPE(PPDMCRITSECT) pCritSectRC;
    /** Alignment padding. */
    RCPTRTYPE(void *)           pvAlignment0;
} APICDEV;
/** Pointer to an APIC device. */
typedef APICDEV *PAPICDEV;
/** Pointer to a const APIC device. */
typedef APICDEV const *PCAPICDEV;

/**
 * APIC VM Instance data.
 */
typedef struct APIC
{
    /** @name The APIC PDM device instance.
     * @{ */
    /** The APIC device - R0 ptr. */
    R0PTRTYPE(PAPICDEV)         pApicDevR0;
    /** The APIC device - R3 ptr. */
    R3PTRTYPE(PAPICDEV)         pApicDevR3;
    /** The APIC device - RC ptr. */
    RCPTRTYPE(PAPICDEV)         pApicDevRC;
    /** Alignment padding. */
    RTRCPTR                     RCPtrAlignment0;
    /** @} */

    /** @name The APIC pending-interrupt bitmap (PIB).
     * @{ */
    /** The host-context physical address of the PIB. */
    RTHCPHYS                    HCPhysApicPib;
    /** The ring-0 memory object of the PIB. */
    RTR0MEMOBJ                  hMemObjApicPibR0;
    /** The ring-3 mapping of the memory object of the PIB. */
    RTR0MEMOBJ                  hMapObjApicPibR0;
    /** The APIC PIB virtual address - R0 ptr. */
    R0PTRTYPE(volatile void *)  pvApicPibR0;
    /** The APIC PIB virtual address - R3 ptr. */
    R3PTRTYPE(volatile void *)  pvApicPibR3;
    /** The APIC PIB virtual address - RC ptr. */
    RCPTRTYPE(volatile void *)  pvApicPibRC;
    /** Alignment padding. */
    RTRCPTR                     RCPtrAlignment1;
    /** The size of the page in bytes. */
    uint32_t                    cbApicPib;
    /** Alignment padding. */
    uint32_t                    u32Aligment0;
    /** @} */

    /** @name Other miscellaneous data.
     * @{ */
    /** Whether full APIC register virtualization is enabled. */
    bool                        fVirtApicRegsEnabled;
    /** Whether posted-interrupt processing is enabled. */
    bool                        fPostedIntrsEnabled;
    /** Whether TSC-deadline timer mode is supported for the guest. */
    bool                        fSupportsTscDeadline;
    /** Whether this VM has an IO-APIC. */
    bool                        fIoApicPresent;
    /** Whether RZ is enabled or not (required for MSR handling as well). */
    bool                        fRZEnabled;
    /** Alignment padding. */
    bool                        afAlignment0[7];
    /** The original APIC mode from CFGM. */
    APICMODE                    enmOriginalMode;
    /** @} */
} APIC;
/** Pointer to APIC VM instance data. */
typedef APIC *PAPIC;
/** Pointer to const APIC VM instance data. */
typedef APIC const *PCAPIC;

/**
 * APIC VMCPU Instance data.
 */
typedef struct APICCPU
{
    /** @name The APIC page.
     * @{ */
    /** The host-context physical address of the page. */
    RTHCPHYS                    HCPhysApicPage;
    /** The ring-0 memory object of the page. */
    RTR0MEMOBJ                  hMemObjApicPageR0;
    /** The ring-3 mapping of the memory object of the page. */
    RTR0MEMOBJ                  hMapObjApicPageR0;
    /** The APIC page virtual address - R0 ptr. */
    R0PTRTYPE(volatile void *)  pvApicPageR0;
    /** The APIC page virtual address - R3 ptr. */
    R3PTRTYPE(volatile void *)  pvApicPageR3;
    /** The APIC page virtual address - RC ptr. */
    RCPTRTYPE(volatile void *)  pvApicPageRC;
    /** Alignment padding. */
    RTRCPTR                     RCPtrAlignment0;
    /** The size of the page in bytes. */
    uint32_t                    cbApicPage;
    /** @} */

    /** @name Auxiliary state.
     * @{ */
    /** The error status register's internal state. */
    uint32_t volatile           uEsrInternal;
    /** The APIC base MSR.*/
    uint64_t volatile           uApicBaseMsr;
    /** @} */

    /** @name The pending-interrupt bitmaps (PIB).
     * @{ */
    /** The host-context physical address of the page. */
    RTHCPHYS                    HCPhysApicPib;
    /** The APIC PIB virtual address - R0 ptr. */
    R0PTRTYPE(volatile void *)  pvApicPibR0;
    /** The APIC PIB virtual address - R3 ptr. */
    R3PTRTYPE(volatile void *)  pvApicPibR3;
    /** The APIC PIB virtual address - RC ptr. */
    RCPTRTYPE(volatile void *)  pvApicPibRC;
    /** Alignment padding. */
    RTRCPTR                     RCPtrAlignment1;
    /** The APIC PIB for level-sensitive interrupts. */
    volatile APICPIB            ApicPibLevel;
    /** @} */

    /** @name The APIC timer.
     * @{ */
    /** The timer - R0 ptr. */
    PTMTIMERR0                  pTimerR0;
    /** The timer - R3 ptr. */
    PTMTIMERR3                  pTimerR3;
    /** The timer - RC ptr. */
    PTMTIMERRC                  pTimerRC;
    /** Alignment padding. */
    RTRCPTR                     RCPtrAlignment2;
    /** The timer critical sect protecting @a u64TimerInitial  */
    PDMCRITSECT                 TimerCritSect;
    /** The time stamp when the timer was initialized. */
    uint64_t                    u64TimerInitial;
    /** Cache of timer initial count of the frequency hint to TM. */
    uint32_t                    uHintedTimerInitialCount;
    /** Cache of timer shift of the frequency hint to TM. */
    uint32_t                    uHintedTimerShift;
    /** The timer description. */
    char                        szTimerDesc[32];
    /** @} */

#ifdef VBOX_WITH_STATISTICS
    /** @name APIC statistics.
     * @{ */
    /** Number of MMIO reads in R0. */
    STAMCOUNTER                 StatMmioReadR0;
    /** Number of MMIO reads in R3. */
    STAMCOUNTER                 StatMmioReadR3;
    /** Number of MMIO reads in RC. */
    STAMCOUNTER                 StatMmioReadRC;

    /** Number of MMIO writes in R0. */
    STAMCOUNTER                 StatMmioWriteR0;
    /** Number of MMIO writes in R3. */
    STAMCOUNTER                 StatMmioWriteR3;
    /** Number of MMIO writes in RC. */
    STAMCOUNTER                 StatMmioWriteRC;

    /** Number of MSR reads in R0. */
    STAMCOUNTER                 StatMsrReadR0;
    /** Number of MSR reads in R3. */
    STAMCOUNTER                 StatMsrReadR3;
    /** Number of MSR reads in RC. */
    STAMCOUNTER                 StatMsrReadRC;

    /** Number of MSR writes in R0. */
    STAMCOUNTER                 StatMsrWriteR0;
    /** Number of MSR writes in R3. */
    STAMCOUNTER                 StatMsrWriteR3;
    /** Number of MSR writes in RC. */
    STAMCOUNTER                 StatMsrWriteRC;

    /** Profiling of APICUpdatePendingInterrupts().  */
    STAMPROFILE                 StatUpdatePendingIntrs;
    /** Profiling of APICPostInterrupt().  */
    STAMPROFILE                 StatPostInterrupt;
    /** @} */
#endif
} APICCPU;
/** Pointer to APIC VMCPU instance data. */
typedef APICCPU *PAPICCPU;
/** Pointer to a const APIC VMCPU instance data. */
typedef APICCPU const *PCAPICCPU;
AssertCompileMemberAlignment(APICCPU, uApicBaseMsr, 8);

/**
 * Gets the timer shift value.
 *
 * @returns The timer shift value.
 * @param   pXApicPage      The xAPIC page.
 */
DECLINLINE(uint8_t) apicGetTimerShift(PCXAPICPAGE pXApicPage)
{
    /* See Intel spec. 10.5.4 "APIC Timer". */
    uint32_t uShift = pXApicPage->timer_dcr.u.u2DivideValue0 | (pXApicPage->timer_dcr.u.u1DivideValue1 << 2);
    return (uShift + 1) & 7;
}

RT_C_DECLS_BEGIN

const char             *apicGetModeName(APICMODE enmMode);
const char             *apicGetDestFormatName(XAPICDESTFORMAT enmDestFormat);
const char             *apicGetDeliveryModeName(XAPICDELIVERYMODE enmDeliveryMode);
const char             *apicGetDestModeName(XAPICDESTMODE enmDestMode);
const char             *apicGetTriggerModeName(XAPICTRIGGERMODE enmTriggerMode);
const char             *apicGetDestShorthandName(XAPICDESTSHORTHAND enmDestShorthand);
const char             *apicGetTimerModeName(XAPICTIMERMODE enmTimerMode);

VMMDECL(uint64_t)       APICGetBaseMsr(PPDMDEVINS pDevIns, PVMCPU pVCpu);
VMMDECL(VBOXSTRICTRC)   APICSetBaseMsr(PPDMDEVINS pDevIns, PVMCPU pVCpu, uint64_t uBase);
VMMDECL(uint8_t)        APICGetTpr(PPDMDEVINS pDevIns, PVMCPU pVCpu);
VMMDECL(void)           APICSetTpr(PPDMDEVINS pDevIns, PVMCPU pVCpu, uint8_t u8Tpr);
VMMDECL(uint64_t)       APICGetTimerFreq(PPDMDEVINS pDevIns);
VMMDECL(int)            APICReadMmio(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS GCPhysAddr, void *pv, unsigned cb);
VMMDECL(int)            APICWriteMmio(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS GCPhysAddr, void const *pv, unsigned cb);
VMMDECL(VBOXSTRICTRC)   APICReadMsr(PPDMDEVINS pDevIns,  PVMCPU pVCpu, uint32_t u32Reg, uint64_t *pu64Val);
VMMDECL(VBOXSTRICTRC)   APICWriteMsr(PPDMDEVINS pDevIns, PVMCPU pVCpu, uint32_t u32Reg, uint64_t u64Val);
VMMDECL(bool)           APICHasPendingIrq(PPDMDEVINS pDevIns, PVMCPU pVCpu, uint8_t *pu8PendingIrq);
VMMDECL(int)            APICGetInterrupt(PPDMDEVINS pDevIns,  PVMCPU pVCpu, uint32_t *puTagSrc);
VMMDECL(void)           APICSetInterruptFF(PVMCPU pVCpu, PDMAPICIRQ enmType);
VMMDECL(void)           APICClearInterruptFF(PVMCPU pVCpu, PDMAPICIRQ enmType);
VMMDECL(VBOXSTRICTRC)   APICLocalInterrupt(PPDMDEVINS pDevIns, PVMCPU pVCpu, uint8_t u8Pin, uint8_t u8Level, int rcRZ);
VMMDECL(int)            APICBusDeliver(PPDMDEVINS pDevIns, uint8_t uDest, uint8_t uDestMode, uint8_t uDeliveryMode,
                                       uint8_t uVector, uint8_t uPolarity, uint8_t uTriggerMode, uint32_t uTagSrc);

VMM_INT_DECL(void)      APICPostInterrupt(PVMCPU pVCpu, uint8_t uVector, XAPICTRIGGERMODE enmTriggerMode);
VMM_INT_DECL(void)      APICStartTimer(PAPICCPU pApicCpu, uint32_t uInitialCount);
VMM_INT_DECL(void)      APICStopTimer(PAPICCPU pApicCpu);
VMM_INT_DECL(void)      APICUpdateCpuIdForMode(PVM pVM, APICMODE enmMode);

RT_C_DECLS_END

/** @} */

#endif

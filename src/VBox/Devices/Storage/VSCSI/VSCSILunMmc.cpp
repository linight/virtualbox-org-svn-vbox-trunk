/* $Id$ */
/** @file
 * Virtual SCSI driver: MMC LUN implementation (CD/DVD-ROM)
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_VSCSI
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/types.h>
#include <VBox/vscsi.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include "VSCSIInternal.h"

/**
 * Different event status types.
 */
typedef enum MMCEVENTSTATUSTYPE
{
    /** Medium event status not changed. */
    MMCEVENTSTATUSTYPE_UNCHANGED = 0,
    /** New medium inserted. */
    MMCEVENTSTATUSTYPE_MEDIA_NEW,
    /** Medium removed. */
    MMCEVENTSTATUSTYPE_MEDIA_REMOVED,
    /** Medium was removed + new medium was inserted. */
    MMCEVENTSTATUSTYPE_MEDIA_CHANGED,
    /** Medium eject requested (eject button pressed). */
    MMCEVENTSTATUSTYPE_MEDIA_EJECT_REQUESTED,
    /** 32bit hack. */
    MMCEVENTSTATUSTYPE_32BIT_HACK = 0x7fffffff
} MMCEVENTSTATUSTYPE;

/** @name Media track types.
 * @{ */
/** Unknown media type. */
#define MMC_MEDIA_TYPE_UNKNOWN          0
/** Door closed, no media. */
#define MMC_MEDIA_TYPE_NO_DISC       0x70
/** @} */


/**
 * MMC LUN instance
 */
typedef struct VSCSILUNMMC
{
    /** Core LUN structure */
    VSCSILUNINT                 Core;
    /** Size of the virtual disk. */
    uint64_t                    cSectors;
    /** Sector size. */
    uint32_t                    cbSector;
    /** Medium locked indicator. */
    bool                        fLocked;
    /** Media event status. */
    volatile MMCEVENTSTATUSTYPE MediaEventStatus;
    /** Media track type. */
    volatile uint32_t           u32MediaTrackType;
} VSCSILUNMMC, *PVSCSILUNMMC;


/* Fabricate normal TOC information. */
static int mmcReadTOCNormal(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq, uint16_t cbMaxTransfer, bool fMSF)
{
    PVSCSILUNMMC    pVScsiLunMmc = (PVSCSILUNMMC)pVScsiLun;
    uint8_t         aReply[32];
    uint8_t         *pbBuf = aReply;
    uint8_t         *q;
    uint8_t         iStartTrack;
    uint32_t        cbSize;

    iStartTrack = pVScsiReq->pbCDB[6];
    if (iStartTrack > 1 && iStartTrack != 0xaa)
    {
        return vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
    }
    q = pbBuf + 2;
    *q++ = 1;   /* first session */
    *q++ = 1;   /* last session */
    if (iStartTrack <= 1)
    {
        *q++ = 0;       /* reserved */
        *q++ = 0x14;    /* ADR, CONTROL */
        *q++ = 1;       /* track number */
        *q++ = 0;       /* reserved */
        if (fMSF)
        {
            *q++ = 0;   /* reserved */
            scsiLBA2MSF(q, 0);
            q += 3;
        }
        else
        {
            /* sector 0 */
            scsiH2BE_U32(q, 0);
            q += 4;
        }
    }
    /* lead out track */
    *q++ = 0;       /* reserved */
    *q++ = 0x14;    /* ADR, CONTROL */
    *q++ = 0xaa;    /* track number */
    *q++ = 0;       /* reserved */
    if (fMSF)
    {
        *q++ = 0;   /* reserved */
        scsiLBA2MSF(q, pVScsiLunMmc->cSectors);
        q += 3;
    }
    else
    {
        scsiH2BE_U32(q, pVScsiLunMmc->cSectors);
        q += 4;
    }
    cbSize = q - pbBuf;
    Assert(cbSize <= sizeof(aReply));
    scsiH2BE_U16(pbBuf, cbSize - 2);
    if (cbSize < cbMaxTransfer)
        cbMaxTransfer = cbSize;

    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, cbMaxTransfer);

    return vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
}

/* Fabricate session information. */
static int mmcReadTOCMulti(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq, uint16_t cbMaxTransfer, bool fMSF)
{
    RT_NOREF1(cbMaxTransfer);
    uint8_t         aReply[32];
    uint8_t         *pbBuf = aReply;

    /* multi session: only a single session defined */
    memset(pbBuf, 0, 12);
    pbBuf[1] = 0x0a;
    pbBuf[2] = 0x01;    /* first complete session number */
    pbBuf[3] = 0x01;    /* last complete session number */
    pbBuf[5] = 0x14;    /* ADR, CONTROL */
    pbBuf[6] = 1;       /* first track in last complete session */

    if (fMSF)
    {
        pbBuf[8] = 0;   /* reserved */
        scsiLBA2MSF(pbBuf + 8, 0);
    }
    else
    {
        /* sector 0 */
        scsiH2BE_U32(pbBuf + 8, 0);
    }

    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, 12);

    return vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
}

/**
 * Create raw TOC data information.
 *
 * @returns SCSI status code.
 * @param   pVScsiLun     The LUN instance.
 * @param   pVScsiReq     The VSCSI request.
 * @param   cbMaxTransfer The maximum transfer size.
 * @param   fMSF          Flag whether to use MSF format to encode sector numbers.
 */
static int mmcReadTOCRaw(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq, uint16_t cbMaxTransfer, bool fMSF)
{
    PVSCSILUNMMC pVScsiLunMmc = (PVSCSILUNMMC)pVScsiLun;
    uint8_t aReply[50]; /* Counted a maximum of 45 bytes but better be on the safe side. */
    uint32_t cbSize;
    uint8_t *pbBuf = &aReply[0] + 2;

    *pbBuf++ = 1; /* first session */
    *pbBuf++ = 1; /* last session */

    *pbBuf++ = 1; /* session number */
    *pbBuf++ = 0x14; /* data track */
    *pbBuf++ = 0; /* track number */
    *pbBuf++ = 0xa0; /* first track in program area */
    *pbBuf++ = 0; /* min */
    *pbBuf++ = 0; /* sec */
    *pbBuf++ = 0; /* frame */
    *pbBuf++ = 0;
    *pbBuf++ = 1; /* first track */
    *pbBuf++ = 0x00; /* disk type CD-DA or CD data */
    *pbBuf++ = 0;

    *pbBuf++ = 1; /* session number */
    *pbBuf++ = 0x14; /* data track */
    *pbBuf++ = 0; /* track number */
    *pbBuf++ = 0xa1; /* last track in program area */
    *pbBuf++ = 0; /* min */
    *pbBuf++ = 0; /* sec */
    *pbBuf++ = 0; /* frame */
    *pbBuf++ = 0;
    *pbBuf++ = 1; /* last track */
    *pbBuf++ = 0;
    *pbBuf++ = 0;

    *pbBuf++ = 1; /* session number */
    *pbBuf++ = 0x14; /* data track */
    *pbBuf++ = 0; /* track number */
    *pbBuf++ = 0xa2; /* lead-out */
    *pbBuf++ = 0; /* min */
    *pbBuf++ = 0; /* sec */
    *pbBuf++ = 0; /* frame */
    if (fMSF)
    {
        *pbBuf++ = 0; /* reserved */
        scsiLBA2MSF(pbBuf, pVScsiLunMmc->cSectors);
        pbBuf += 3;
    }
    else
    {
        scsiH2BE_U32(pbBuf, pVScsiLunMmc->cSectors);
        pbBuf += 4;
    }

    *pbBuf++ = 1; /* session number */
    *pbBuf++ = 0x14; /* ADR, control */
    *pbBuf++ = 0;    /* track number */
    *pbBuf++ = 1;    /* point */
    *pbBuf++ = 0; /* min */
    *pbBuf++ = 0; /* sec */
    *pbBuf++ = 0; /* frame */
    if (fMSF)
    {
        *pbBuf++ = 0; /* reserved */
        scsiLBA2MSF(pbBuf, 0);
        pbBuf += 3;
    }
    else
    {
        /* sector 0 */
        scsiH2BE_U32(pbBuf, 0);
        pbBuf += 4;
    }

    cbSize = pbBuf - aReply;
    scsiH2BE_U16(&aReply[0], cbSize - 2);

    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, RT_MIN(cbMaxTransfer, cbSize));
    return vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
}

static size_t vscsiLunMmcGetConfigurationFillFeatureListProfiles(uint8_t *pbBuf, size_t cbBuf)
{
    if (cbBuf < 3*4)
        return 0;

    scsiH2BE_U16(pbBuf, 0x0); /* feature 0: list of profiles supported */
    pbBuf[2] = (0 << 2) | (1 << 1) | (1 << 0); /* version 0, persistent, current */
    pbBuf[3] = 8; /* additional bytes for profiles */
    /* The MMC-3 spec says that DVD-ROM read capability should be reported
     * before CD-ROM read capability. */
    scsiH2BE_U16(pbBuf + 4, 0x10); /* profile: read-only DVD */
    pbBuf[6] = (0 << 0); /* NOT current profile */
    scsiH2BE_U16(pbBuf + 8, 0x08); /* profile: read only CD */
    pbBuf[10] = (1 << 0); /* current profile */

    return 3*4; /* Header + 2 profiles entries */
}

static size_t vscsiLunMmcGetConfigurationFillFeatureCore(uint8_t *pbBuf, size_t cbBuf)
{
    if (cbBuf < 12)
        return 0;

    scsiH2BE_U16(pbBuf, 0x1); /* feature 0001h: Core Feature */
    pbBuf[2] = (0x2 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 8; /* Additional length */
    scsiH2BE_U16(pbBuf + 4, 0x00000002); /* Physical interface ATAPI. */
    pbBuf[8] = RT_BIT(0); /* DBE */
    /* Rest is reserved. */

    return 12;
}

static size_t vscsiLunMmcGetConfigurationFillFeatureMorphing(uint8_t *pbBuf, size_t cbBuf)
{
    if (cbBuf < 8)
        return 0;

    scsiH2BE_U16(pbBuf, 0x2); /* feature 0002h: Morphing Feature */
    pbBuf[2] = (0x1 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 4; /* Additional length */
    pbBuf[4] = RT_BIT(1) | 0x0; /* OCEvent | !ASYNC */
    /* Rest is reserved. */

    return 8;
}

static size_t vscsiLunMmcGetConfigurationFillFeatureRemovableMedium(uint8_t *pbBuf, size_t cbBuf)
{
    if (cbBuf < 8)
        return 0;

    scsiH2BE_U16(pbBuf, 0x3); /* feature 0003h: Removable Medium Feature */
    pbBuf[2] = (0x2 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 4; /* Additional length */
    /* Tray type loading | Load | Eject | !Pvnt Jmpr | !DBML | Lock */
    pbBuf[4] = (0x2 << 5) | RT_BIT(4) | RT_BIT(3) | (0x0 << 2) | (0x0 << 1) | RT_BIT(0);
    /* Rest is reserved. */

    return 8;
}

static size_t vscsiLunMmcGetConfigurationFillFeatureRandomReadable(uint8_t *pbBuf, size_t cbBuf)
{
    if (cbBuf < 12)
        return 0;

    scsiH2BE_U16(pbBuf, 0x10); /* feature 0010h: Random Readable Feature */
    pbBuf[2] = (0x0 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 8; /* Additional length */
    scsiH2BE_U32(pbBuf + 4, 2048); /* Logical block size. */
    scsiH2BE_U16(pbBuf + 8, 0x10); /* Blocking (0x10 for DVD, CD is not defined). */
    pbBuf[10] = 0; /* PP not present */
    /* Rest is reserved. */

    return 12;
}

static size_t vscsiLunMmcGetConfigurationFillFeatureCDRead(uint8_t *pbBuf, size_t cbBuf)
{
    if (cbBuf < 8)
        return 0;

    scsiH2BE_U16(pbBuf, 0x1e); /* feature 001Eh: CD Read Feature */
    pbBuf[2] = (0x2 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 0; /* Additional length */
    pbBuf[4] = (0x0 << 7) | (0x0 << 1) | 0x0; /* !DAP | !C2-Flags | !CD-Text. */
    /* Rest is reserved. */

    return 8;
}

static size_t vscsiLunMmcGetConfigurationFillFeaturePowerManagement(uint8_t *pbBuf, size_t cbBuf)
{
    if (cbBuf < 4)
        return 0;

    scsiH2BE_U16(pbBuf, 0x100); /* feature 0100h: Power Management Feature */
    pbBuf[2] = (0x0 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 0; /* Additional length */

    return 4;
}

static size_t vscsiLunMmcGetConfigurationFillFeatureTimeout(uint8_t *pbBuf, size_t cbBuf)
{
    if (cbBuf < 8)
        return 0;

    scsiH2BE_U16(pbBuf, 0x105); /* feature 0105h: Timeout Feature */
    pbBuf[2] = (0x0 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 4; /* Additional length */
    pbBuf[4] = 0x0; /* !Group3 */

    return 8;
}

/**
 * Processes the GET CONFIGURATION SCSI request.
 *
 * @returns SCSI status code.
 * @param   pVScsiLunMmc  The MMC LUN instance.
 * @param   pVScsiReq     The VSCSI request.
 * @param   cbMaxTransfer The maximum transfer size.
 */
static int vscsiLunMmcGetConfiguration(PVSCSILUNMMC pVScsiLunMmc, PVSCSIREQINT pVScsiReq, size_t cbMaxTransfer)
{
    uint8_t aReply[80];
    uint8_t *pbBuf = &aReply[0];
    size_t cbBuf = sizeof(aReply);
    size_t cbCopied = 0;

    /* Accept valid request types only, and only starting feature 0. */
    if ((pVScsiReq->pbCDB[1] & 0x03) == 3 || scsiBE2H_U16(&pVScsiReq->pbCDB[2]) != 0)
        return vscsiLunReqSenseErrorSet(&pVScsiLunMmc->Core, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                        SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);

    /** @todo implement switching between CD-ROM and DVD-ROM profile (the only
     * way to differentiate them right now is based on the image size). */
    if (pVScsiLunMmc->cSectors)
        scsiH2BE_U16(pbBuf + 6, 0x08); /* current profile: read-only CD */
    else
        scsiH2BE_U16(pbBuf + 6, 0x00); /* current profile: none -> no media */
    cbBuf    -= 8;
    pbBuf    += 8;

    cbCopied = vscsiLunMmcGetConfigurationFillFeatureListProfiles(pbBuf, cbBuf);
    cbBuf -= cbCopied;
    pbBuf += cbCopied;

    cbCopied = vscsiLunMmcGetConfigurationFillFeatureCore(pbBuf, cbBuf);
    cbBuf -= cbCopied;
    pbBuf += cbCopied;

    cbCopied = vscsiLunMmcGetConfigurationFillFeatureMorphing(pbBuf, cbBuf);
    cbBuf -= cbCopied;
    pbBuf += cbCopied;

    cbCopied = vscsiLunMmcGetConfigurationFillFeatureRemovableMedium(pbBuf, cbBuf);
    cbBuf -= cbCopied;
    pbBuf += cbCopied;

    cbCopied = vscsiLunMmcGetConfigurationFillFeatureRandomReadable(pbBuf, cbBuf);
    cbBuf -= cbCopied;
    pbBuf += cbCopied;

    cbCopied = vscsiLunMmcGetConfigurationFillFeatureCDRead(pbBuf, cbBuf);
    cbBuf -= cbCopied;
    pbBuf += cbCopied;

    cbCopied = vscsiLunMmcGetConfigurationFillFeaturePowerManagement(pbBuf, cbBuf);
    cbBuf -= cbCopied;
    pbBuf += cbCopied;

    cbCopied = vscsiLunMmcGetConfigurationFillFeatureTimeout(pbBuf, cbBuf);
    cbBuf -= cbCopied;
    pbBuf += cbCopied;

    /* Set data length now. */
    scsiH2BE_U32(&aReply[0], (uint32_t)(sizeof(aReply) - cbBuf));

    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, RT_MIN(cbMaxTransfer, sizeof(aReply) - cbBuf));
    return vscsiLunReqSenseOkSet(&pVScsiLunMmc->Core, pVScsiReq);
}

/**
 * Processes the READ DVD STRUCTURE SCSI request.
 *
 * @returns SCSI status code.
 * @param   pVScsiLunMmc  The MMC LUN instance.
 * @param   pVScsiReq     The VSCSI request.
 * @param   cbMaxTransfer The maximum transfer size.
 */
static int vscsiLunMmcReadDvdStructure(PVSCSILUNMMC pVScsiLunMmc, PVSCSIREQINT pVScsiReq, size_t cbMaxTransfer)
{
    uint8_t aReply[25]; /* Counted a maximum of 20 bytes but better be on the safe side. */

    RT_ZERO(aReply);

    /* Act according to the indicated format. */
    switch (pVScsiReq->pbCDB[7])
    {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
        case 0x10:
        case 0x11:
        case 0x30:
        case 0x31:
        case 0xff:
            if (pVScsiReq->pbCDB[1] == 0)
            {
                int uASC = SCSI_ASC_NONE;

                switch (pVScsiReq->pbCDB[7])
                {
                    case 0x0: /* Physical format information */
                    {
                        uint8_t uLayer = pVScsiReq->pbCDB[6];
                        uint64_t cTotalSectors;

                        if (uLayer != 0)
                        {
                            uASC = -SCSI_ASC_INV_FIELD_IN_CMD_PACKET;
                            break;
                        }

                        cTotalSectors = pVScsiLunMmc->cSectors;
                        cTotalSectors >>= 2;
                        if (cTotalSectors == 0)
                        {
                            uASC = -SCSI_ASC_MEDIUM_NOT_PRESENT;
                            break;
                        }

                        aReply[4] = 1;   /* DVD-ROM, part version 1 */
                        aReply[5] = 0xf; /* 120mm disc, minimum rate unspecified */
                        aReply[6] = 1;   /* one layer, read-only (per MMC-2 spec) */
                        aReply[7] = 0;   /* default densities */

                        /* FIXME: 0x30000 per spec? */
                        scsiH2BE_U32(&aReply[8], 0); /* start sector */
                        scsiH2BE_U32(&aReply[12], cTotalSectors - 1); /* end sector */
                        scsiH2BE_U32(&aReply[16], cTotalSectors - 1); /* l0 end sector */

                        /* Size of buffer, not including 2 byte size field */
                        scsiH2BE_U32(&aReply[0], 2048 + 2);

                        /* 2k data + 4 byte header */
                        uASC = (2048 + 4);
                        break;
                    }
                    case 0x01: /* DVD copyright information */
                        aReply[4] = 0; /* no copyright data */
                        aReply[5] = 0; /* no region restrictions */

                        /* Size of buffer, not including 2 byte size field */
                        scsiH2BE_U16(&aReply[0], 4 + 2);

                        /* 4 byte header + 4 byte data */
                        uASC = (4 + 4);
                        break;

                    case 0x03: /* BCA information - invalid field for no BCA info */
                        uASC = -SCSI_ASC_INV_FIELD_IN_CMD_PACKET;
                        break;

                    case 0x04: /* DVD disc manufacturing information */
                        /* Size of buffer, not including 2 byte size field */
                        scsiH2BE_U16(&aReply[0], 2048 + 2);

                        /* 2k data + 4 byte header */
                        uASC = (2048 + 4);
                        break;
                    case 0xff:
                        /*
                         * This lists all the command capabilities above.  Add new ones
                         * in order and update the length and buffer return values.
                         */

                        aReply[4] = 0x00; /* Physical format */
                        aReply[5] = 0x40; /* Not writable, is readable */
                        scsiH2BE_U16(&aReply[6], 2048 + 4);

                        aReply[8] = 0x01; /* Copyright info */
                        aReply[9] = 0x40; /* Not writable, is readable */
                        scsiH2BE_U16(&aReply[10], 4 + 4);

                        aReply[12] = 0x03; /* BCA info */
                        aReply[13] = 0x40; /* Not writable, is readable */
                        scsiH2BE_U16(&aReply[14], 188 + 4);

                        aReply[16] = 0x04; /* Manufacturing info */
                        aReply[17] = 0x40; /* Not writable, is readable */
                        scsiH2BE_U16(&aReply[18], 2048 + 4);

                        /* Size of buffer, not including 2 byte size field */
                        scsiH2BE_U16(&aReply[0], 16 + 2);

                        /* data written + 4 byte header */
                        uASC = (16 + 4);
                        break;
                    default: /** @todo formats beyond DVD-ROM requires */
                        uASC = -SCSI_ASC_INV_FIELD_IN_CMD_PACKET;
                }

                if (uASC < 0)
                    return vscsiLunReqSenseErrorSet(&pVScsiLunMmc->Core, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                                    -uASC, 0x00);
                break;
            }
            /** @todo BD support, fall through for now */

        /* Generic disk structures */
        case 0x80: /** @todo AACS volume identifier */
        case 0x81: /** @todo AACS media serial number */
        case 0x82: /** @todo AACS media identifier */
        case 0x83: /** @todo AACS media key block */
        case 0x90: /** @todo List of recognized format layers */
        case 0xc0: /** @todo Write protection status */
        default:
            return vscsiLunReqSenseErrorSet(&pVScsiLunMmc->Core, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                            SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
    }

    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, RT_MIN(cbMaxTransfer, sizeof(aReply)));
    return vscsiLunReqSenseOkSet(&pVScsiLunMmc->Core, pVScsiReq);
}

/**
 * Processes the MODE SENSE 10 SCSI request.
 *
 * @returns SCSI status code.
 * @param   pVScsiLunMmc  The MMC LUN instance.
 * @param   pVScsiReq     The VSCSI request.
 * @param   cbMaxTransfer The maximum transfer size.
 */
static int vscsiLunMmcModeSense10(PVSCSILUNMMC pVScsiLunMmc, PVSCSIREQINT pVScsiReq, size_t cbMaxTransfer)
{
    int rcReq;
    uint8_t uPageControl = pVScsiReq->pbCDB[2] >> 6;
    uint8_t uPageCode = pVScsiReq->pbCDB[2] & 0x3f;

    switch (uPageControl)
    {
        case SCSI_PAGECONTROL_CURRENT:
            switch (uPageCode)
            {
                case SCSI_MODEPAGE_ERROR_RECOVERY:
                {
                    uint8_t aReply[16];

                    scsiH2BE_U16(&aReply[0], 16 + 6);
                    aReply[2] = (uint8_t)pVScsiLunMmc->u32MediaTrackType;
                    aReply[3] = 0;
                    aReply[4] = 0;
                    aReply[5] = 0;
                    aReply[6] = 0;
                    aReply[7] = 0;

                    aReply[8] = 0x01;
                    aReply[9] = 0x06;
                    aReply[10] = 0x00;
                    aReply[11] = 0x05;
                    aReply[12] = 0x00;
                    aReply[13] = 0x00;
                    aReply[14] = 0x00;
                    aReply[15] = 0x00;
                    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, RT_MIN(cbMaxTransfer, sizeof(aReply)));
                    rcReq = vscsiLunReqSenseOkSet(&pVScsiLunMmc->Core, pVScsiReq);
                    break;
                }
                case SCSI_MODEPAGE_CD_STATUS:
                {
                    uint8_t aReply[40];

                    scsiH2BE_U16(&aReply[0], 38);
                    aReply[2] = (uint8_t)pVScsiLunMmc->u32MediaTrackType;
                    aReply[3] = 0;
                    aReply[4] = 0;
                    aReply[5] = 0;
                    aReply[6] = 0;
                    aReply[7] = 0;

                    aReply[8] = 0x2a;
                    aReply[9] = 30; /* page length */
                    aReply[10] = 0x08; /* DVD-ROM read support */
                    aReply[11] = 0x00; /* no write support */
                    /* The following claims we support audio play. This is obviously false,
                     * but the Linux generic CDROM support makes many features depend on this
                     * capability. If it's not set, this causes many things to be disabled. */
                    aReply[12] = 0x71; /* multisession support, mode 2 form 1/2 support, audio play */
                    aReply[13] = 0x00; /* no subchannel reads supported */
                    aReply[14] = (1 << 0) | (1 << 3) | (1 << 5); /* lock supported, eject supported, tray type loading mechanism */
                    if (pVScsiLunMmc->fLocked)
                        aReply[14] |= 1 << 1; /* report lock state */
                    aReply[15] = 0; /* no subchannel reads supported, no separate audio volume control, no changer etc. */
                    scsiH2BE_U16(&aReply[16], 5632); /* (obsolete) claim 32x speed support */
                    scsiH2BE_U16(&aReply[18], 2); /* number of audio volume levels */
                    scsiH2BE_U16(&aReply[20], 128); /* buffer size supported in Kbyte - We don't have a buffer because we write directly into guest memory.
                                                       Just write some dummy value. */
                    scsiH2BE_U16(&aReply[22], 5632); /* (obsolete) current read speed 32x */
                    aReply[24] = 0; /* reserved */
                    aReply[25] = 0; /* reserved for digital audio (see idx 15) */
                    scsiH2BE_U16(&aReply[26], 0); /* (obsolete) maximum write speed */
                    scsiH2BE_U16(&aReply[28], 0); /* (obsolete) current write speed */
                    scsiH2BE_U16(&aReply[30], 0); /* copy management revision supported 0=no CSS */
                    aReply[32] = 0; /* reserved */
                    aReply[33] = 0; /* reserved */
                    aReply[34] = 0; /* reserved */
                    aReply[35] = 1; /* rotation control CAV */
                    scsiH2BE_U16(&aReply[36], 0); /* current write speed */
                    scsiH2BE_U16(&aReply[38], 0); /* number of write speed performance descriptors */
                    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, RT_MIN(cbMaxTransfer, sizeof(aReply)));
                    rcReq = vscsiLunReqSenseOkSet(&pVScsiLunMmc->Core, pVScsiReq);
                    break;
                }
                default:
                    rcReq = vscsiLunReqSenseErrorSet(&pVScsiLunMmc->Core, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                                     SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
                    break;
            }
            break;
        case SCSI_PAGECONTROL_CHANGEABLE:
        case SCSI_PAGECONTROL_DEFAULT:
            rcReq = vscsiLunReqSenseErrorSet(&pVScsiLunMmc->Core, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                             SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
            break;
        default:
        case SCSI_PAGECONTROL_SAVED:
            rcReq = vscsiLunReqSenseErrorSet(&pVScsiLunMmc->Core, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                             SCSI_ASC_SAVING_PARAMETERS_NOT_SUPPORTED, 0x00);
            break;
    }

    return rcReq;
}

/**
 * Processes the GET EVENT STATUS NOTIFICATION SCSI request.
 *
 * @returns SCSI status code.
 * @param   pVScsiLunMmc  The MMC LUN instance.
 * @param   pVScsiReq     The VSCSI request.
 * @param   cbMaxTransfer The maximum transfer size.
 */
static int vscsiLunMmcGetEventStatusNotification(PVSCSILUNMMC pVScsiLunMmc, PVSCSIREQINT pVScsiReq,
                                                 size_t cbMaxTransfer)
{
    uint32_t OldStatus;
    uint32_t NewStatus;
    uint8_t aReply[8];
    RT_ZERO(aReply);

    LogFlowFunc(("pVScsiLunMmc=%#p pVScsiReq=%#p cbMaxTransfer=%zu\n",
                 pVScsiLunMmc, pVScsiReq, cbMaxTransfer));

    do
    {
        OldStatus = ASMAtomicReadU32((volatile uint32_t *)&pVScsiLunMmc->MediaEventStatus);
        NewStatus = MMCEVENTSTATUSTYPE_UNCHANGED;

        switch (OldStatus)
        {
            case MMCEVENTSTATUSTYPE_MEDIA_NEW:
                /* mount */
                scsiH2BE_U16(&aReply[0], 6);
                aReply[2] = 0x04; /* media */
                aReply[3] = 0x5e; /* supported = busy|media|external|power|operational */
                aReply[4] = 0x02; /* new medium */
                aReply[5] = 0x02; /* medium present / door closed */
                aReply[6] = 0x00;
                aReply[7] = 0x00;
                pVScsiLunMmc->Core.fReady = true;
                break;

            case MMCEVENTSTATUSTYPE_MEDIA_CHANGED:
            case MMCEVENTSTATUSTYPE_MEDIA_REMOVED:
                /* umount */
                scsiH2BE_U16(&aReply[0], 6);
                aReply[2] = 0x04; /* media */
                aReply[3] = 0x5e; /* supported = busy|media|external|power|operational */
                aReply[4] = 0x03; /* media removal */
                aReply[5] = 0x00; /* medium absent / door closed */
                aReply[6] = 0x00;
                aReply[7] = 0x00;
                if (OldStatus == MMCEVENTSTATUSTYPE_MEDIA_CHANGED)
                    NewStatus = MMCEVENTSTATUSTYPE_MEDIA_NEW;
                break;

            case MMCEVENTSTATUSTYPE_MEDIA_EJECT_REQUESTED: /* currently unused */
                scsiH2BE_U16(&aReply[0], 6);
                aReply[2] = 0x04; /* media */
                aReply[3] = 0x5e; /* supported = busy|media|external|power|operational */
                aReply[4] = 0x01; /* eject requested (eject button pressed) */
                aReply[5] = 0x02; /* medium present / door closed */
                aReply[6] = 0x00;
                aReply[7] = 0x00;
                break;

            case MMCEVENTSTATUSTYPE_UNCHANGED:
            default:
                scsiH2BE_U16(&aReply[0], 6);
                aReply[2] = 0x01; /* operational change request / notification */
                aReply[3] = 0x5e; /* supported = busy|media|external|power|operational */
                aReply[4] = 0x00;
                aReply[5] = 0x00;
                aReply[6] = 0x00;
                aReply[7] = 0x00;
                break;
        }

        LogFlowFunc(("OldStatus=%u NewStatus=%u\n", OldStatus, NewStatus));

    } while (!ASMAtomicCmpXchgU32((volatile uint32_t *)&pVScsiLunMmc->MediaEventStatus, NewStatus, OldStatus));

    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, RT_MIN(cbMaxTransfer, sizeof(aReply)));
    return vscsiLunReqSenseOkSet(&pVScsiLunMmc->Core, pVScsiReq);
}

static DECLCALLBACK(int) vscsiLunMmcInit(PVSCSILUNINT pVScsiLun)
{
    PVSCSILUNMMC    pVScsiLunMmc = (PVSCSILUNMMC)pVScsiLun;
    uint64_t        cbDisk = 0;
    int             rc = VINF_SUCCESS;

    ASMAtomicWriteU32((volatile uint32_t *)&pVScsiLunMmc->MediaEventStatus, MMCEVENTSTATUSTYPE_UNCHANGED);
    pVScsiLunMmc->u32MediaTrackType = MMC_MEDIA_TYPE_UNKNOWN;
    pVScsiLunMmc->cbSector          = 2048;  /* Default to 2K sectors. */
    rc = vscsiLunMediumGetSize(pVScsiLun, &cbDisk);
    if (RT_SUCCESS(rc))
        pVScsiLunMmc->cSectors = cbDisk / pVScsiLunMmc->cbSector;

    return rc;
}

static DECLCALLBACK(int) vscsiLunMmcDestroy(PVSCSILUNINT pVScsiLun)
{
    RT_NOREF1(pVScsiLun);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) vscsiLunMmcReqProcess(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq)
{
    PVSCSILUNMMC    pVScsiLunMmc = (PVSCSILUNMMC)pVScsiLun;
    VSCSIIOREQTXDIR enmTxDir = VSCSIIOREQTXDIR_INVALID;
    uint64_t        uLbaStart = 0;
    uint32_t        cSectorTransfer = 0;
    int             rc = VINF_SUCCESS;
    int             rcReq = SCSI_STATUS_OK;
    unsigned        uCmd = pVScsiReq->pbCDB[0];

    LogFlowFunc(("pVScsiLun=%#p{.fReady=%RTbool, .fMediaPresent=%RTbool} pVScsiReq=%#p{.pbCdb[0]=%#x}\n",
                 pVScsiLun, pVScsiLun->fReady, pVScsiLun->fMediaPresent, pVScsiReq, uCmd));

    /*
     * GET CONFIGURATION, GET EVENT/STATUS NOTIFICATION, INQUIRY, and REQUEST SENSE commands
     * operate even when a unit attention condition exists for initiator; every other command
     * needs to report CHECK CONDITION in that case.
     */
    if (   !pVScsiLunMmc->Core.fReady
        && uCmd != SCSI_INQUIRY
        && uCmd != SCSI_GET_CONFIGURATION
        && uCmd != SCSI_GET_EVENT_STATUS_NOTIFICATION)
    {
        /*
         * A note on media changes: As long as a medium is not present, the unit remains in
         * the 'not ready' state. Technically the unit becomes 'ready' soon after a medium
         * is inserted; however, we internally keep the 'not ready' state until we've had
         * a chance to report the UNIT ATTENTION status indicating a media change.
         */
        if (pVScsiLunMmc->Core.fMediaPresent)
        {
            rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_UNIT_ATTENTION,
                                             SCSI_ASC_MEDIUM_MAY_HAVE_CHANGED, 0x00);
            pVScsiLunMmc->Core.fReady = true;
        }
        else
            rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_NOT_READY,
                                             SCSI_ASC_MEDIUM_NOT_PRESENT, 0x00);
    }
    else
    {
        switch (uCmd)
        {
            case SCSI_TEST_UNIT_READY:
                Assert(!pVScsiLunMmc->Core.fReady); /* Only should get here if LUN isn't ready. */
                rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_NOT_READY, SCSI_ASC_MEDIUM_NOT_PRESENT, 0x00);
                break;

            case SCSI_INQUIRY:
            {
                SCSIINQUIRYDATA ScsiInquiryReply;

                vscsiReqSetXferSize(pVScsiReq, scsiBE2H_U16(&pVScsiReq->pbCDB[3]));
                memset(&ScsiInquiryReply, 0, sizeof(ScsiInquiryReply));

                ScsiInquiryReply.cbAdditional           = 31;
                ScsiInquiryReply.fRMB                   = 1;    /* Removable. */
                ScsiInquiryReply.u5PeripheralDeviceType = SCSI_INQUIRY_DATA_PERIPHERAL_DEVICE_TYPE_CD_DVD;
                ScsiInquiryReply.u3PeripheralQualifier  = SCSI_INQUIRY_DATA_PERIPHERAL_QUALIFIER_CONNECTED;
                ScsiInquiryReply.u3AnsiVersion          = 0x05; /* MMC-?? compliant */
                ScsiInquiryReply.fCmdQue                = 1;    /* Command queuing supported. */
                ScsiInquiryReply.fWBus16                = 1;
                scsiPadStrS(ScsiInquiryReply.achVendorId, "VBOX", 8);
                scsiPadStrS(ScsiInquiryReply.achProductId, "CD-ROM", 16);
                scsiPadStrS(ScsiInquiryReply.achProductLevel, "1.0", 4);

                RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, (uint8_t *)&ScsiInquiryReply, sizeof(SCSIINQUIRYDATA));
                rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                break;
            }
            case SCSI_READ_CAPACITY:
            {
                uint8_t aReply[8];
                memset(aReply, 0, sizeof(aReply));
                vscsiReqSetXferSize(pVScsiReq, sizeof(aReply));

                /*
                 * If sector size exceeds the maximum value that is
                 * able to be stored in 4 bytes return 0xffffffff in this field
                 */
                if (pVScsiLunMmc->cSectors > UINT32_C(0xffffffff))
                    scsiH2BE_U32(aReply, UINT32_C(0xffffffff));
                else
                    scsiH2BE_U32(aReply, pVScsiLunMmc->cSectors - 1);
                scsiH2BE_U32(&aReply[4], pVScsiLunMmc->cbSector);
                RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, sizeof(aReply));
                rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                break;
            }
            case SCSI_MODE_SENSE_6:
            {
                uint8_t uModePage = pVScsiReq->pbCDB[2] & 0x3f;
                uint8_t aReply[24];
                uint8_t *pu8ReplyPos;
                bool    fValid = false;

                vscsiReqSetXferSize(pVScsiReq, pVScsiReq->pbCDB[4]);
                memset(aReply, 0, sizeof(aReply));
                aReply[0] = 4; /* Reply length 4. */
                aReply[1] = 0; /* Default media type. */
                aReply[2] = RT_BIT(4); /* Caching supported. */
                aReply[3] = 0; /* Block descriptor length. */

                pu8ReplyPos = aReply + 4;

                if ((uModePage == 0x08) || (uModePage == 0x3f))
                {
                    memset(pu8ReplyPos, 0, 20);
                    *pu8ReplyPos++ = 0x08; /* Page code. */
                    *pu8ReplyPos++ = 0x12; /* Size of the page. */
                    *pu8ReplyPos++ = 0x4;  /* Write cache enabled. */
                    fValid = true;
                } else if (uModePage == 0) {
                    fValid = true;
                }

                /* Querying unknown pages must fail. */
                if (fValid) {
                    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, sizeof(aReply));
                    rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                } else {
                    rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
                }
                break;
            }
            case SCSI_MODE_SENSE_10:
            {
                size_t cbMax = scsiBE2H_U16(&pVScsiReq->pbCDB[7]);
                vscsiReqSetXferSize(pVScsiReq, cbMax);
                rcReq = vscsiLunMmcModeSense10(pVScsiLunMmc, pVScsiReq, cbMax);
                break;
            }
            case SCSI_SEEK_10:
            {
                uint32_t uLba = scsiBE2H_U32(&pVScsiReq->pbCDB[2]);
                if (uLba > pVScsiLunMmc->cSectors)
                    rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                                     SCSI_ASC_LOGICAL_BLOCK_OOR, 0x00);
                else
                    rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                break;
            }
            case SCSI_MODE_SELECT_6:
            {
                /** @todo implement!! */
                vscsiReqSetXferSize(pVScsiReq, pVScsiReq->pbCDB[4]);
                rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                break;
            }
            case SCSI_READ_6:
            {
                enmTxDir       = VSCSIIOREQTXDIR_READ;
                uLbaStart      = ((uint64_t)    pVScsiReq->pbCDB[3]
                                            |  (pVScsiReq->pbCDB[2] <<  8)
                                            | ((pVScsiReq->pbCDB[1] & 0x1f) << 16));
                cSectorTransfer = pVScsiReq->pbCDB[4];
                break;
            }
            case SCSI_READ_10:
            {
                enmTxDir        = VSCSIIOREQTXDIR_READ;
                uLbaStart       = scsiBE2H_U32(&pVScsiReq->pbCDB[2]);
                cSectorTransfer = scsiBE2H_U16(&pVScsiReq->pbCDB[7]);
                break;
            }
            case SCSI_READ_12:
            {
                enmTxDir        = VSCSIIOREQTXDIR_READ;
                uLbaStart       = scsiBE2H_U32(&pVScsiReq->pbCDB[2]);
                cSectorTransfer = scsiBE2H_U32(&pVScsiReq->pbCDB[6]);
                break;
            }
            case SCSI_READ_16:
            {
                enmTxDir        = VSCSIIOREQTXDIR_READ;
                uLbaStart       = scsiBE2H_U64(&pVScsiReq->pbCDB[2]);
                cSectorTransfer = scsiBE2H_U32(&pVScsiReq->pbCDB[10]);
                break;
            }
            case SCSI_READ_BUFFER:
            {
                uint8_t uDataMode = pVScsiReq->pbCDB[1] & 0x1f;

                vscsiReqSetXferSize(pVScsiReq, scsiBE2H_U16(&pVScsiReq->pbCDB[6]));

                switch (uDataMode)
                {
                    case 0x00:
                    case 0x01:
                    case 0x02:
                    case 0x03:
                    case 0x0a:
                        break;
                    case 0x0b:
                    {
                        uint8_t aReply[4];
                        RT_ZERO(aReply);

                        RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, sizeof(aReply));
                        rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                        break;
                    }
                    case 0x1a:
                    case 0x1c:
                        break;
                    default:
                        AssertMsgFailed(("Invalid data mode\n"));
                }
                break;
            }
            case SCSI_VERIFY_10:
            case SCSI_START_STOP_UNIT:
            {
                int rc2 = VINF_SUCCESS;
                switch (pVScsiReq->pbCDB[4] & 3)
                {
                    case 0: /* 00 - Stop motor */
                    case 1: /* 01 - Start motor */
                        break;
                    case 2: /* 10 - Eject media */
                        rc2 = vscsiLunMediumEject(pVScsiLun);
                        break;
                    case 3: /* 11 - Load media */
                        /** @todo */
                        break;
                }
                if (RT_SUCCESS(rc2))
                    rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                else
                    rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_MEDIA_LOAD_OR_EJECT_FAILED, 0x02);
                break;
            }
            case SCSI_LOG_SENSE:
            {
                uint8_t uPageCode = pVScsiReq->pbCDB[2] & 0x3f;
                uint8_t uSubPageCode = pVScsiReq->pbCDB[3];

                vscsiReqSetXferSize(pVScsiReq, scsiBE2H_U16(&pVScsiReq->pbCDB[7]));

                switch (uPageCode)
                {
                    case 0x00:
                    {
                        if (uSubPageCode == 0)
                        {
                            uint8_t aReply[4];

                            aReply[0] = 0;
                            aReply[1] = 0;
                            aReply[2] = 0;
                            aReply[3] = 0;

                            RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, sizeof(aReply));
                            rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                            break;
                        }
                    }
                    default:
                        rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
                }
                break;
            }
            case SCSI_SERVICE_ACTION_IN_16:
            {
                switch (pVScsiReq->pbCDB[1] & 0x1f)
                {
                    case SCSI_SVC_ACTION_IN_READ_CAPACITY_16:
                    {
                        uint8_t aReply[32];

                        memset(aReply, 0, sizeof(aReply));
                        scsiH2BE_U64(aReply, pVScsiLunMmc->cSectors - 1);
                        scsiH2BE_U32(&aReply[8], pVScsiLunMmc->cbSector);
                        /* Leave the rest 0 */

                        vscsiReqSetXferSize(pVScsiReq, sizeof(aReply));
                        RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, sizeof(aReply));
                        rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                        break;
                    }
                    default:
                        rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                                         SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00); /* Don't know if this is correct */
                }
                break;
            }
            case SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL:
            {
                pVScsiLunMmc->fLocked = RT_BOOL(pVScsiReq->pbCDB[4] & 0x01);
                vscsiLunMediumSetLock(pVScsiLun, pVScsiLunMmc->fLocked);
                rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                break;
            }
            case SCSI_READ_TOC_PMA_ATIP:
            {
                uint8_t     format;
                uint16_t    cbMax;
                bool        fMSF;

                format = pVScsiReq->pbCDB[2] & 0x0f;
                cbMax  = scsiBE2H_U16(&pVScsiReq->pbCDB[7]);
                fMSF   = (pVScsiReq->pbCDB[1] >> 1) & 1;

                vscsiReqSetXferSize(pVScsiReq, cbMax);
                switch (format)
                {
                    case 0x00:
                        rcReq = mmcReadTOCNormal(pVScsiLun, pVScsiReq, cbMax, fMSF);
                        break;
                    case 0x01:
                        rcReq = mmcReadTOCMulti(pVScsiLun, pVScsiReq, cbMax, fMSF);
                        break;
                    case 0x02:
                        rcReq = mmcReadTOCRaw(pVScsiLun, pVScsiReq, cbMax, fMSF);
                        break;
                    default:
                        rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
                }
                break;
            }
            case SCSI_GET_EVENT_STATUS_NOTIFICATION:
            {
                /* Only supporting polled mode at the moment. */
                size_t cbMax = scsiBE2H_U16(&pVScsiReq->pbCDB[7]);

                vscsiReqSetXferSize(pVScsiReq, cbMax);
                if (pVScsiReq->pbCDB[1] & 0x1)
                    rcReq = vscsiLunMmcGetEventStatusNotification(pVScsiLunMmc, pVScsiReq, cbMax);
                else
                    rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
                break;
            }
            case SCSI_MECHANISM_STATUS:
            {
                size_t cbMax = scsiBE2H_U16(&pVScsiReq->pbCDB[8]);
                uint8_t aReply[8];

                vscsiReqSetXferSize(pVScsiReq, cbMax);
                scsiH2BE_U16(&aReply[0], 0);
                /* no current LBA */
                aReply[2] = 0;
                aReply[3] = 0;
                aReply[4] = 0;
                aReply[5] = 1;
                scsiH2BE_U16(&aReply[6], 0);
                RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, RT_MIN(sizeof(aReply), cbMax));
                rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                break;
            }
            case SCSI_READ_DISC_INFORMATION:
            {
                uint8_t aReply[34];
                size_t cbMax = scsiBE2H_U16(&pVScsiReq->pbCDB[7]);

                vscsiReqSetXferSize(pVScsiReq, cbMax);
                memset(aReply, '\0', sizeof(aReply));
                scsiH2BE_U16(&aReply[0], 32);
                aReply[2] = (0 << 4) | (3 << 2) | (2 << 0); /* not erasable, complete session, complete disc */
                aReply[3] = 1; /* number of first track */
                aReply[4] = 1; /* number of sessions (LSB) */
                aReply[5] = 1; /* first track number in last session (LSB) */
                aReply[6] = 1; /* last track number in last session (LSB) */
                aReply[7] = (0 << 7) | (0 << 6) | (1 << 5) | (0 << 2) | (0 << 0); /* disc id not valid, disc bar code not valid, unrestricted use, not dirty, not RW medium */
                aReply[8] = 0; /* disc type = CD-ROM */
                aReply[9] = 0; /* number of sessions (MSB) */
                aReply[10] = 0; /* number of sessions (MSB) */
                aReply[11] = 0; /* number of sessions (MSB) */
                scsiH2BE_U32(&aReply[16], 0x00ffffff); /* last session lead-in start time is not available */
                scsiH2BE_U32(&aReply[20], 0x00ffffff); /* last possible start time for lead-out is not available */
                RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, RT_MIN(sizeof(aReply), cbMax));
                rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                break;
            }
            case SCSI_READ_TRACK_INFORMATION:
            {
                size_t cbMax = scsiBE2H_U16(&pVScsiReq->pbCDB[7]);

                vscsiReqSetXferSize(pVScsiReq, cbMax);
                /* Accept address/number type of 1 only, and only track 1 exists. */
                if ((pVScsiReq->pbCDB[1] & 0x03) != 1 || scsiBE2H_U32(&pVScsiReq->pbCDB[2]) != 1)
                    rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                                     SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
                else
                {
                    uint8_t aReply[36];
                    RT_ZERO(aReply);

                    scsiH2BE_U16(&aReply[0], 34);
                    aReply[2] = 1; /* track number (LSB) */
                    aReply[3] = 1; /* session number (LSB) */
                    aReply[5] = (0 << 5) | (0 << 4) | (4 << 0); /* not damaged, primary copy, data track */
                    aReply[6] = (0 << 7) | (0 << 6) | (0 << 5) | (0 << 6) | (1 << 0); /* not reserved track, not blank, not packet writing, not fixed packet, data mode 1 */
                    aReply[7] = (0 << 1) | (0 << 0); /* last recorded address not valid, next recordable address not valid */
                    scsiH2BE_U32(&aReply[8], 0); /* track start address is 0 */
                    scsiH2BE_U32(&aReply[24], pVScsiLunMmc->cSectors); /* track size */
                    aReply[32] = 0; /* track number (MSB) */
                    aReply[33] = 0; /* session number (MSB) */

                    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, RT_MIN(sizeof(aReply), cbMax));
                    rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                }
                break;
            }
            case SCSI_GET_CONFIGURATION:
            {
                size_t cbMax = scsiBE2H_U16(&pVScsiReq->pbCDB[7]);
                vscsiReqSetXferSize(pVScsiReq, cbMax);
                rcReq = vscsiLunMmcGetConfiguration(pVScsiLunMmc, pVScsiReq, cbMax);
                break;
            }
            case SCSI_READ_DVD_STRUCTURE:
            {
                size_t cbMax = scsiBE2H_U16(&pVScsiReq->pbCDB[8]);
                vscsiReqSetXferSize(pVScsiReq, cbMax);
                rcReq = vscsiLunMmcReadDvdStructure(pVScsiLunMmc, pVScsiReq, cbMax);
                break;
            }
            default:
                //AssertMsgFailed(("Command %#x [%s] not implemented\n", pVScsiReq->pbCDB[0], SCSICmdText(pVScsiReq->pbCDB[0])));
                rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_ILLEGAL_OPCODE, 0x00);
        }
    }

    if (enmTxDir != VSCSIIOREQTXDIR_INVALID)
    {
        LogFlow(("%s: uLbaStart=%llu cSectorTransfer=%u\n",
                 __FUNCTION__, uLbaStart, cSectorTransfer));

        vscsiReqSetXferSize(pVScsiReq, cSectorTransfer * pVScsiLunMmc->cbSector);
        if (RT_UNLIKELY(uLbaStart + cSectorTransfer > pVScsiLunMmc->cSectors))
        {
            rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_LOGICAL_BLOCK_OOR, 0x00);
            vscsiDeviceReqComplete(pVScsiLun->pVScsiDevice, pVScsiReq, rcReq, false, VINF_SUCCESS);
        }
        else if (!cSectorTransfer)
        {
            /* A 0 transfer length is not an error. */
            rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
            vscsiDeviceReqComplete(pVScsiLun->pVScsiDevice, pVScsiReq, rcReq, false, VINF_SUCCESS);
        }
        else
        {
            /* Enqueue new I/O request */
            rc = vscsiIoReqTransferEnqueue(pVScsiLun, pVScsiReq, enmTxDir,
                                           uLbaStart * pVScsiLunMmc->cbSector,
                                           cSectorTransfer * pVScsiLunMmc->cbSector);
        }
    }
    else /* Request completed */
        vscsiDeviceReqComplete(pVScsiLun->pVScsiDevice, pVScsiReq, rcReq, false, VINF_SUCCESS);

    return rc;
}

/** @interface_method_impl{VSCSILUNDESC,pfnVScsiLunMediumInserted} */
static DECLCALLBACK(int) vscsiLunMmcMediumInserted(PVSCSILUNINT pVScsiLun)
{
    PVSCSILUNMMC pVScsiLunMmc = (PVSCSILUNMMC)pVScsiLun;
    uint64_t cbDisk = 0;
    int rc = vscsiLunMediumGetSize(pVScsiLun, &cbDisk);
    if (RT_SUCCESS(rc))
    {
        pVScsiLunMmc->cSectors = cbDisk / pVScsiLunMmc->cbSector;

        uint32_t OldStatus, NewStatus;
        do
        {
            OldStatus = ASMAtomicReadU32((volatile uint32_t *)&pVScsiLunMmc->MediaEventStatus);
            switch (OldStatus)
            {
                case MMCEVENTSTATUSTYPE_MEDIA_CHANGED:
                case MMCEVENTSTATUSTYPE_MEDIA_REMOVED:
                    /* no change, we will send "medium removed" + "medium inserted" */
                    NewStatus = MMCEVENTSTATUSTYPE_MEDIA_CHANGED;
                    break;
                default:
                    NewStatus = MMCEVENTSTATUSTYPE_MEDIA_NEW;
                    break;
            }
        } while (!ASMAtomicCmpXchgU32((volatile uint32_t *)&pVScsiLunMmc->MediaEventStatus,
                                      NewStatus, OldStatus));

        ASMAtomicXchgU32(&pVScsiLunMmc->u32MediaTrackType, MMC_MEDIA_TYPE_UNKNOWN);
    }

    return rc;
}

/** @interface_method_impl{VSCSILUNDESC,pfnVScsiLunMediumRemoved} */
static DECLCALLBACK(int) vscsiLunMmcMediumRemoved(PVSCSILUNINT pVScsiLun)
{
    PVSCSILUNMMC pVScsiLunMmc = (PVSCSILUNMMC)pVScsiLun;

    ASMAtomicWriteU32((volatile uint32_t *)&pVScsiLunMmc->MediaEventStatus, MMCEVENTSTATUSTYPE_MEDIA_REMOVED);
    ASMAtomicXchgU32(&pVScsiLunMmc->u32MediaTrackType, MMC_MEDIA_TYPE_NO_DISC);
    return VINF_SUCCESS;
}


VSCSILUNDESC g_VScsiLunTypeMmc =
{
    /** enmLunType */
    VSCSILUNTYPE_MMC,
    /** pcszDescName */
    "MMC",
    /** cbLun */
    sizeof(VSCSILUNMMC),
    /** cSupOpcInfo */
    0,
    /** paSupOpcInfo */
    NULL,
    /** pfnVScsiLunInit */
    vscsiLunMmcInit,
    /** pfnVScsiLunDestroy */
    vscsiLunMmcDestroy,
    /** pfnVScsiLunReqProcess */
    vscsiLunMmcReqProcess,
    /** pfnVScsiLunMediumInserted */
    vscsiLunMmcMediumInserted,
    /** pfnVScsiLunMediumRemoved */
    vscsiLunMmcMediumRemoved
};

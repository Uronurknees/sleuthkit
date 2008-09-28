/*
 * The Sleuth Kit
 *
 * Brian Carrier [carrier <at> sleuthkit [dot] org]
 * Copyright (c) 2003-2008 Brian Carrier.  All rights reserved
 *
 * tsk_vs_open - wrapper function for specific partition type
 *
 * This software is distributed under the Common Public License 1.0
 */

/**
 * \file mm_open.c
 * Contains general code to open volume systems.
 */

#include "tsk_vs_i.h"


/**
 * \ingroup vslib
 *
 * Open a disk image and process the media management system
 * data.  This calls VS specific code to determine the type and
 * collect data. 
 *
 * @param img_info The opened disk image.
 * @param offset Byte offset in the disk image to start analyzing from.
 * @param type Type of volume system (including auto detect)
 *
 * @return NULL on error. 
 */
TSK_VS_INFO *
tsk_vs_open(TSK_IMG_INFO * img_info, TSK_DADDR_T offset,
    TSK_VS_TYPE_ENUM type)
{
    /* Autodetect mode 
     * We need to try all of them in case there are multiple 
     * installations
     *
     * NOte that errors that are encountered during the testing process
     * will not be reported
     */
    if (type == TSK_VS_TYPE_DETECT) {
        TSK_VS_INFO *vs, *vs_set = NULL;
        char *set = NULL;

        if ((vs = tsk_vs_dos_open(img_info, offset, 1)) != NULL) {
            set = "DOS";
            vs_set = vs;
        }
        else {
            tsk_error_reset();
        }
        if ((vs = tsk_vs_bsd_open(img_info, offset)) != NULL) {
            // if (set == NULL) {
            // In this case, BSD takes priority because BSD partitions start off with
            // the DOS magic value in the first sector with the boot code.
            set = "BSD";
            vs_set = vs;
            /*
               }
               else {
               vs_set->close(vs_set);
               vs->close(vs);
               tsk_error_reset();
               tsk_errno = TSK_ERR_VS_UNKTYPE;
               snprintf(tsk_errstr, TSK_ERRSTR_L,
               "BSD or %s at %" PRIuDADDR, set, offset);
               tsk_errstr2[0] = '\0';
               return NULL;
               }
             */
        }
        else {
            tsk_error_reset();
        }
        if ((vs = tsk_vs_gpt_open(img_info, offset)) != NULL) {
            if (set == NULL) {
                set = "GPT";
                vs_set = vs;
            }
            else {
                vs_set->close(vs_set);
                vs->close(vs);
                tsk_error_reset();
                tsk_errno = TSK_ERR_VS_UNKTYPE;
                snprintf(tsk_errstr, TSK_ERRSTR_L,
                    "GPT or %s at %" PRIuDADDR, set, offset);
                return NULL;
            }
        }
        else {
            tsk_error_reset();
        }

        if ((vs = tsk_vs_sun_open(img_info, offset)) != NULL) {
            if (set == NULL) {
                set = "Sun";
                vs_set = vs;
            }
            else {
                vs_set->close(vs_set);
                vs->close(vs);
                tsk_error_reset();
                tsk_errno = TSK_ERR_VS_UNKTYPE;
                snprintf(tsk_errstr, TSK_ERRSTR_L,
                    "Sun or %s at %" PRIuDADDR, set, offset);
                return NULL;
            }
        }
        else {
            tsk_error_reset();
        }

        if ((vs = tsk_vs_mac_open(img_info, offset)) != NULL) {
            if (set == NULL) {
                set = "Mac";
                vs_set = vs;
            }
            else {
                vs_set->close(vs_set);
                vs->close(vs);
                tsk_error_reset();
                tsk_errno = TSK_ERR_VS_UNKTYPE;
                snprintf(tsk_errstr, TSK_ERRSTR_L,
                    "Mac or %s at %" PRIuDADDR, set, offset);
                return NULL;
            }
        }
        else {
            tsk_error_reset();
        }

        if (vs_set == NULL) {
            tsk_error_reset();
            tsk_errno = TSK_ERR_VS_UNKTYPE;
            return NULL;
        }

        return vs_set;
    }
    else {

        switch (type) {
        case TSK_VS_TYPE_DOS:
            return tsk_vs_dos_open(img_info, offset, 0);
        case TSK_VS_TYPE_MAC:
            return tsk_vs_mac_open(img_info, offset);
        case TSK_VS_TYPE_BSD:
            return tsk_vs_bsd_open(img_info, offset);
        case TSK_VS_TYPE_SUN:
            return tsk_vs_sun_open(img_info, offset);
        case TSK_VS_TYPE_GPT:
            return tsk_vs_gpt_open(img_info, offset);
        case TSK_VS_TYPE_UNSUPP:
        default:
            tsk_error_reset();
            tsk_errno = TSK_ERR_VS_UNSUPTYPE;
            snprintf(tsk_errstr, TSK_ERRSTR_L, "%d", type);
            return NULL;
        }
    }
}

/**
 * \ingroup vslib
 * Closes an open volume system 
 * @param a_vs Pointer to the open volume system structure.
 */
void
tsk_vs_close(TSK_VS_INFO * a_vs)
{
    if (a_vs == NULL) {
        return;
    }
    a_vs->close((TSK_VS_INFO *) a_vs);
}

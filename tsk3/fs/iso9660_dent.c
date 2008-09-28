/*
** The Sleuth Kit
**
** Brian Carrier [carrier <at> sleuthkit [dot] org]
** Copyright (c)2007 Brian Carrier.  All righs reserved.
**
**
** This software is subject to the IBM Public License ver. 1.0,
** which was displayed prior to download and is included in the readme.txt
** file accompanying the Sleuth Kit files.  It may also be requested from:
** Crucial Security Inc.
** 14900 Conference Center Drive
** Chantilly, VA 20151
**
** Copyright (c) 2007-2008 Brian Carrier.  All rights reserved
**
** Wyatt Banks [wbanks@crucialsecurity.com]
** Copyright (c) 2005 Crucial Security Inc.  All rights reserved.
**
** Brian Carrier [carrier <at> sleuthkit [dot] org]
** Copyright (c) 2003-2005 Brian Carrier.  All rights reserved
**
** Copyright (c) 1997,1998,1999, International Business Machines
** Corporation and others. All Rights Reserved.
*/

/* TCT
 * LICENSE
 *      This software is distributed under the IBM Public License.
 * AUTHOR(S)
 *      Wietse Venema
 *      IBM T.J. Watson Research
 *      P.O. Box 704
 *      Yorktown Heights, NY 10598, USA
 --*/

/*
** You may distribute the Sleuth Kit, or other software that incorporates
** part of all of the Sleuth Kit, in object code form under a license agreement,
** provided that:
** a) you comply with the terms and conditions of the IBM Public License
**    ver 1.0; and
** b) the license agreement
**     i) effectively disclaims on behalf of all Contributors all warranties
**        and conditions, express and implied, including warranties or
**        conditions of title and non-infringement, and implied warranties
**        or conditions of merchantability and fitness for a particular
**        purpose.
**    ii) effectively excludes on behalf of all Contributors liability for
**        damages, including direct, indirect, special, incidental and
**        consequential damages such as lost profits.
**   iii) states that any provisions which differ from IBM Public License
**        ver. 1.0 are offered by that Contributor alone and not by any
**        other party; and
**    iv) states that the source code for the program is available from you,
**        and informs licensees how to obtain it in a reasonable manner on or
**        through a medium customarily used for software exchange.
**
** When the Sleuth Kit or other software that incorporates part or all of
** the Sleuth Kit is made available in source code form:
**     a) it must be made available under IBM Public License ver. 1.0; and
**     b) a copy of the IBM Public License ver. 1.0 must be included with
**        each copy of the program.
*/

/**
 * \file iso9660_dent.c
 * Contains the internal TSK ISO9660 file system code to handle the parsing of 
 * file names and directory structures.
 */

#include "tsk_fs_i.h"
#include "tsk_iso9660.h"



uint8_t
iso9660_proc_dir(TSK_FS_INFO * a_fs, TSK_FS_DIR * a_fs_dir, char *buf,
    size_t length, TSK_INUM_T a_addr)
{
    ISO_INFO *iso = (ISO_INFO *) a_fs;
    TSK_FS_NAME *fs_name;

    iso9660_dentry *dd;         /* directory descriptor */
    iso9660_inode_node *in;

    if ((fs_name = tsk_fs_name_alloc(ISO9660_MAXNAMLEN + 1, 0)) == NULL)
        return TSK_ERR;

    dd = (iso9660_dentry *) buf;

    /* handle "." entry */
    fs_name->meta_addr = a_addr;
    strcpy(fs_name->name, ".");
    fs_name->type = TSK_FS_NAME_TYPE_DIR;
    fs_name->flags = TSK_FS_NAME_FLAG_ALLOC;

    tsk_fs_dir_add(a_fs_dir, fs_name);

    length -= dd->entry_len;
    dd = (iso9660_dentry *) ((char *) dd + dd->entry_len);

    /* handle ".." entry */
    in = iso->in_list;
    while (in
        && (tsk_getu32(a_fs->endian, in->inode.dr.ext_loc_m) !=
            tsk_getu32(a_fs->endian, dd->ext_loc_m)))
        in = in->next;
    if (in) {
        fs_name->meta_addr = in->inum;
        strcpy(fs_name->name, "..");

        fs_name->type = TSK_FS_NAME_TYPE_DIR;
        fs_name->flags = TSK_FS_NAME_FLAG_ALLOC;

        tsk_fs_dir_add(a_fs_dir, fs_name);
    }
    length -= dd->entry_len;
    dd = (iso9660_dentry *) ((char *) dd + dd->entry_len);

    // process the rest of the entries in the directory
    while (length > sizeof(iso9660_dentry)) {
        if (dd->entry_len) {
            int i;

            // find the entry in our list of files
            in = iso->in_list;
            while ((in)
                && (tsk_getu32(a_fs->endian,
                        in->inode.dr.ext_loc_m) != tsk_getu32(a_fs->endian,
                        dd->ext_loc_m))) {
                in = in->next;
            }

            if ((!in)
                || (tsk_getu32(a_fs->endian,
                        in->inode.dr.ext_loc_m) != tsk_getu32(a_fs->endian,
                        dd->ext_loc_m))) {
                // @@@ 
                return TSK_COR;
            }


            fs_name->meta_addr = in->inum;
            strncpy(fs_name->name, in->inode.fn, ISO9660_MAXNAMLEN);

            /* Clean up name */
            i = 0;
            while (fs_name->name[i] != '\0') {
                if (TSK_IS_CNTRL(fs_name->name[i]))
                    fs_name->name[i] = '^';
                i++;
            }

            if (dd->flags & ISO9660_FLAG_DIR)
                fs_name->type = TSK_FS_NAME_TYPE_DIR;
            else
                fs_name->type = TSK_FS_NAME_TYPE_REG;
            fs_name->flags = TSK_FS_NAME_FLAG_ALLOC;

            tsk_fs_dir_add(a_fs_dir, fs_name);

            length -= dd->entry_len;

            dd = (iso9660_dentry *) ((char *) dd + dd->entry_len);
            /* we need to look for files past the next NULL we discover, in case
             * directory has a hole in it (this is common) */
        }
        else {
            char *a, *b;
            length -= sizeof(iso9660_dentry);

            /* find next non-zero byte and we'll start over there */
            a = (char *) dd;
            b = a + sizeof(iso9660_dentry);

            while ((*a == 0) && (a != b))
                a++;

            if (a != b) {
                length += (int) (b - a);
                dd = (iso9660_dentry *) ((char *) dd +
                    (sizeof(iso9660_dentry) - (int) (b - a)));
            }
        }
    }

    free(buf);
    tsk_fs_name_free(fs_name);
    return TSK_OK;
}



/** \internal
 * Process a directory and load up FS_DIR with the entries. If a pointer to
 * an already allocated FS_DIR struture is given, it will be cleared.  If no existing
 * FS_DIR structure is passed (i.e. NULL), then a new one will be created. If the return 
 * value is error or corruption, then the FS_DIR structure could  
 * have entries (depending on when the error occured). 
 *
 * @param a_fs File system to analyze
 * @param a_fs_dir Pointer to FS_DIR pointer. Can contain an already allocated
 * structure or a new structure. 
 * @param a_addr Address of directory to process.
 * @returns error, corruption, ok etc. 
 */
TSK_RETVAL_ENUM
iso9660_dir_open_meta(TSK_FS_INFO * a_fs, TSK_FS_DIR ** a_fs_dir,
    TSK_INUM_T a_addr)
{
    TSK_RETVAL_ENUM retval;
    TSK_FS_DIR *fs_dir;
    TSK_OFF_T offs;             /* where we are reading in the file */
    ssize_t cnt;
    char *buf;
    size_t length;
    ISO_INFO *iso = (ISO_INFO *) a_fs;

    if (a_addr < a_fs->first_inum || a_addr > a_fs->last_inum) {
        tsk_error_reset();
        tsk_errno = TSK_ERR_FS_WALK_RNG;
        snprintf(tsk_errstr, TSK_ERRSTR_L,
            "iso9660_dir_open_meta: Invalid inode value: %" PRIuINUM,
            a_addr);
        return TSK_ERR;
    }
    else if (a_fs_dir == NULL) {
        tsk_error_reset();
        tsk_errno = TSK_ERR_FS_ARG;
        snprintf(tsk_errstr, TSK_ERRSTR_L,
            "iso9660_dir_open_meta: NULL fs_attr argument given");
        return TSK_ERR;
    }

    if (tsk_verbose)
        tsk_fprintf(stderr,
            "iso9660_dir_open_meta: Processing directory %" PRIuINUM "\n",
            a_addr);

    fs_dir = *a_fs_dir;
    if (fs_dir) {
        tsk_fs_dir_reset(fs_dir);
    }
    else {
        if ((*a_fs_dir = fs_dir = tsk_fs_dir_alloc(a_fs, 128)) == NULL) {
            return TSK_ERR;
        }
    }


    fs_dir->fs_file = tsk_fs_file_open_meta(a_fs, NULL, a_addr);
    if (fs_dir->fs_file == NULL) {
        tsk_error_reset();
        tsk_errno = TSK_ERR_FS_INODE_NUM;
        snprintf(tsk_errstr, TSK_ERRSTR_L,
            "iso9660_dir_open_meta: %" PRIuINUM " is not a valid inode",
            a_addr);
        return TSK_COR;
    }


    /* calculate directory extent location */
    offs =
        (TSK_OFF_T) (a_fs->block_size *
        tsk_getu32(a_fs->endian, iso->dinode->dr.ext_loc_m));

    /* read directory extent into memory */
    length = (size_t) fs_dir->fs_file->meta->size;
    if ((buf = tsk_malloc(length)) == NULL)
        return TSK_ERR;

    cnt = tsk_fs_file_read(fs_dir->fs_file, 0, buf, length, 0);
    if (cnt != length) {
        if (cnt >= 0) {
            tsk_error_reset();
            tsk_errno = TSK_ERR_FS_READ;
            tsk_errstr[0] = '\0';
        }
        snprintf(tsk_errstr2, TSK_ERRSTR_L, "iso9660_dir_open_meta");
        return TSK_ERR;
    }

    retval = iso9660_proc_dir(a_fs, fs_dir, buf, length, a_addr);

    return retval;
}

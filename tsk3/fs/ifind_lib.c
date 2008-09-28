/*
** ifind (inode find)
** The Sleuth Kit
**
** Given an image  and block number, identify which inode it is used by
** 
** Brian Carrier [carrier <at> sleuthkit [dot] org]
** Copyright (c) 2006-2008 Brian Carrier, Basis Technology.  All Rights reserved
** Copyright (c) 2003-2005 Brian Carrier.  All rights reserved
**
** TASK
** Copyright (c) 2002 Brian Carrier, @stake Inc.  All rights reserved
**
** TCTUTILs
** Copyright (c) 2001 Brian Carrier.  All rights reserved
**
**
** This software is distributed under the Common Public License 1.0
**
*/

/**
 * \file ifind_lib.c
 * Contains the library API functions used by the TSK ifind command
 * line tool.
 */

#include "tsk_fs_i.h"


/*******************************************************************************
 * Find an unallocated NTFS MFT entry based on its parent directory
 */

typedef struct {
    TSK_INUM_T parinode;
    TSK_FS_IFIND_FLAG_ENUM flags;
    uint8_t found;
} IFIND_PAR_DATA;


/* inode walk call back for tsk_fs_ifind_par to find unallocated files 
 * based on parent directory
 */
static TSK_WALK_RET_ENUM
ifind_par_act(TSK_FS_FILE * fs_file, void *ptr)
{
    IFIND_PAR_DATA *data = (IFIND_PAR_DATA *) ptr;
    TSK_FS_META_NAME_LIST *fs_name_list;

    /* go through each file name attribute for this file */
    fs_name_list = fs_file->meta->name2;
    while (fs_name_list) {

        /* we found a file that has the target parent directory.
         * Make a FS_NAME structure and print it.  */
        if (fs_name_list->par_inode == data->parinode) {
            int i, cnt;
            uint8_t printed;
            TSK_FS_NAME *fs_name;

            if ((fs_name = tsk_fs_name_alloc(256, 0)) == NULL)
                return TSK_WALK_ERROR;

            /* Fill in the basics of the fs_name entry 
             * so we can print in the fls formats */
            fs_name->meta_addr = fs_file->meta->addr;
            fs_name->flags = TSK_FS_NAME_FLAG_UNALLOC;
            strncpy(fs_name->name, fs_name_list->name, fs_name->name_size);

            // now look for the $Data and $IDXROOT attributes 
            fs_file->name = fs_name;
            printed = 0;

            // cycle through the attributes
            cnt = tsk_fs_file_attr_getsize(fs_file);
            for (i = 0; i < cnt; i++) {
                const TSK_FS_ATTR *fs_attr =
                    tsk_fs_file_attr_get_idx(fs_file, i);
                if (!fs_attr)
                    continue;

                if ((fs_attr->type == TSK_FS_ATTR_TYPE_NTFS_DATA)
                    || (fs_attr->type == TSK_FS_ATTR_TYPE_NTFS_IDXROOT)) {

                    if (data->flags & TSK_FS_IFIND_PAR_LONG) {
                        tsk_fs_name_print_long(stdout, fs_file, NULL,
                            fs_file->fs_info, fs_attr, 0);
                    }
                    else {
                        tsk_fs_name_print(stdout, fs_file, NULL,
                            fs_file->fs_info, fs_attr, 0);
                        tsk_printf("\n");
                    }
                    printed = 1;
                }
            }

            // if there were no attributes, print what we got
            if (printed == 0) {
                if (data->flags & TSK_FS_IFIND_PAR_LONG) {
                    tsk_fs_name_print_long(stdout, fs_file, NULL,
                        fs_file->fs_info, NULL, 0);
                }
                else {
                    tsk_fs_name_print(stdout, fs_file, NULL,
                        fs_file->fs_info, NULL, 0);
                    tsk_printf("\n");
                }
            }
            tsk_fs_name_free(fs_name);
            data->found = 1;
        }
        fs_name_list = fs_name_list->next;
    }

    return TSK_WALK_CONT;
}



/**
 * Searches for unallocated MFT entries that have a given 
 * MFT entry as their parent directory (as reported in FILE_NAME).
 * @param fs File system to search
 * @param lclflags Flags
 * @param par Parent directory MFT entry address
 * @returns 1 on error and 0 on success
 */
uint8_t
tsk_fs_ifind_par(TSK_FS_INFO * fs, TSK_FS_IFIND_FLAG_ENUM lclflags,
    TSK_INUM_T par)
{
    IFIND_PAR_DATA data;

    data.found = 0;
    data.flags = lclflags;
    data.parinode = par;

    /* Walk unallocated MFT entries */
    if (fs->inode_walk(fs, fs->first_inum, fs->last_inum,
            TSK_FS_META_FLAG_UNALLOC, ifind_par_act, &data)) {
        return 1;
    }

    return 0;
}




/**
 * \ingroup fslib
 * 
 * Find the meta data address for a given file name (UTF-8)
 *
 * @param a_fs FS to analyze
 * @param a_path UTF-8 path of file to search for
 * @param [out] a_result Meta data address of file
 * @param [out] a_fs_name Copy of name details (or NULL if details not wanted)
 * @returns -1 on (system) error, 0 if found, and 1 if not found
 */
int8_t
tsk_fs_path2inum(TSK_FS_INFO * a_fs, const char *a_path,
    TSK_INUM_T * a_result, TSK_FS_NAME * a_fs_name)
{
    char *cpath;
    size_t clen;
    char *cur_dir;              // The "current" directory or file we are looking for
    char *cur_attr;             // The "current" attribute of the dir we are looking for
    char *strtok_last;
    TSK_INUM_T next_meta;

    *a_result = 0;

    // copy to a buffer that we can modify
    clen = strlen(a_path) + 1;
    if ((cpath = (char *) tsk_malloc(clen)) == NULL) {
        return -1;
    }
    strncpy(cpath, a_path, clen);


    cur_dir = (char *) strtok_r(cpath, "/", &strtok_last);
    cur_attr = NULL;

    /* If there is no token, then only a '/' was given */
    if (cur_dir == NULL) {
        free(cpath);
        *a_result = a_fs->root_inum;

        // create the dummy entry if needed
        if (a_fs_name) {
            a_fs_name->meta_addr = a_fs->root_inum;
            a_fs_name->type = TSK_FS_NAME_TYPE_DIR;
            a_fs_name->flags = TSK_FS_NAME_FLAG_ALLOC;
            if (a_fs_name->name)
                a_fs_name->name[0] = '\0';
            if (a_fs_name->shrt_name)
                a_fs_name->shrt_name[0] = '\0';
        }
        return 0;
    }

    /* If this is NTFS, seperate out the attribute of the current directory */
    if (TSK_FS_TYPE_ISNTFS(a_fs->ftype)
        && ((cur_attr = strchr(cur_dir, ':')) != NULL)) {
        *(cur_attr) = '\0';
        cur_attr++;
    }

    if (tsk_verbose)
        tsk_fprintf(stderr, "Looking for %s\n", cur_dir);

    // initialize the first place to look, the root dir
    next_meta = a_fs->root_inum;

    // we loop until we know the outcome and then exit. 
    // everything should return from inside the loop.
    while (1) {
        size_t i;
        uint8_t found_name;
        TSK_FS_DIR *fs_dir = NULL;

        if ((fs_dir = tsk_fs_dir_open_meta(a_fs, next_meta)) == NULL) {
            free(cpath);
            return -1;
        }

        // will be set to 1 if an entry in this dir matches the target
        found_name = 0;

        // cycle through each entry
        for (i = 0; i < tsk_fs_dir_getsize(fs_dir); i++) {

            TSK_FS_FILE *fs_file;

            if ((fs_file = tsk_fs_dir_get(fs_dir, i)) == NULL) {
                free(cpath);
                return -1;
            }

            /* 
             * Check if this is the name that we are currently looking for,
             * as identified in 'cur_dir'
             */
            if (TSK_FS_TYPE_ISFFS(a_fs->ftype)
                || TSK_FS_TYPE_ISEXT(a_fs->ftype)) {
                if (strcmp(fs_file->name->name, cur_dir) == 0) {
                    found_name = 1;
                }
            }
            /* FAT is a special case because we do case insensitive and we check
             * the short name 
             */
            else if (TSK_FS_TYPE_ISFAT(a_fs->ftype)) {
                if (strcasecmp(fs_file->name->name, cur_dir) == 0) {
                    found_name = 1;
                }
                else if (strcasecmp(fs_file->name->shrt_name,
                        cur_dir) == 0) {
                    found_name = 1;
                }
            }

            /* NTFS gets a case insensitive comparison */
            else if (TSK_FS_TYPE_ISNTFS(a_fs->ftype)) {
                if (strcasecmp(fs_file->name->name, cur_dir) == 0) {
                    /*  ensure we have the right attribute name */
                    if (cur_attr == NULL) {
                        found_name = 1;
                    }
                    else {
                        if (fs_file->meta) {
                            int cnt, i;

                            // cycle through the attributes
                            cnt = tsk_fs_file_attr_getsize(fs_file);
                            for (i = 0; i < cnt; i++) {
                                const TSK_FS_ATTR *fs_attr =
                                    tsk_fs_file_attr_get_idx(fs_file, i);
                                if (!fs_attr)
                                    continue;

                                if (strcasecmp(fs_attr->name,
                                        cur_attr) == 0) {
                                    found_name = 1;
                                }
                            }
                        }
                        if (found_name != 1) {
                            free(cpath);

                            if (tsk_verbose)
                                tsk_fprintf(stderr,
                                    "Attribute name (%s) not found in %s: %"
                                    PRIuINUM "\n", cur_attr, cur_dir,
                                    fs_file->name->meta_addr);
                            return 1;
                        }
                    }
                }
            }

            /* if found_name is 1, this entry was our target.  Update
             * data and move on to the next step, if needed. */
            if (found_name) {
                const char *pname;

                pname = cur_dir;        // save a copy of the current name pointer

                // advance to the next name
                cur_dir = (char *) strtok_r(NULL, "/", &(strtok_last));
                cur_attr = NULL;

                if (tsk_verbose)
                    tsk_fprintf(stderr,
                        "Found it (%s), now looking for %s\n", pname,
                        cur_dir);


                /* That was the last name in the path -- we found the file! */
                if (cur_dir == NULL) {
                    *a_result = fs_file->name->meta_addr;

                    // make a copy if one was requested
                    if (a_fs_name) {
                        tsk_fs_name_copy(a_fs_name, fs_file->name);
                    }

                    free(cpath);
                    return 0;
                }

                // update the attribute field, if needed
                if (TSK_FS_TYPE_ISNTFS(a_fs->ftype)
                    && ((cur_attr = strchr(cur_dir, ':')) != NULL)) {
                    *(cur_attr) = '\0';
                    cur_attr++;
                }

                /* Before we recurse into this directory, check it */
                if (fs_file->meta == NULL) {
                    free(cpath);
                    if (tsk_verbose)
                        tsk_fprintf(stderr,
                            "Name does not point to an inode (%s)\n",
                            fs_file->name->name);
                    return 1;
                }

                /* Make sure this name is for a directory */
                else if (fs_file->meta->type != TSK_FS_META_TYPE_DIR) {
                    free(cpath);
                    if (tsk_verbose)
                        tsk_fprintf(stderr,
                            "Name is not for a directory (%s) (type: %x)\n",
                            fs_file->name->name, fs_file->meta->type);
                    return 1;
                }

                next_meta = fs_file->name->meta_addr;
            }

            tsk_fs_file_close(fs_file);
            fs_file = NULL;

            if (found_name)
                break;
        }

        tsk_fs_dir_close(fs_dir);
        fs_dir = NULL;

        // didn't find the name in this directory...
        if (found_name == 0) {
            free(cpath);
            return 1;
        }
    }

    free(cpath);
    return 1;
}


/**
 * Find the meta data address for a given file TCHAR name
 *
 * @param fs FS to analyze
 * @param tpath Path of file to search for
 * @param [out] result Meta data address of file
 * @returns -1 on error, 0 if found, and 1 if not found
 */
int8_t
tsk_fs_ifind_path(TSK_FS_INFO * fs, TSK_TCHAR * tpath, TSK_INUM_T * result)
{

#ifdef TSK_WIN32
    // Convert the UTF-16 path to UTF-8
    {
        size_t clen;
        UTF8 *ptr8;
        UTF16 *ptr16;
        int retval;
        char *cpath;

        clen = TSTRLEN(tpath) * 4;
        if ((cpath = (char *) tsk_malloc(clen)) == NULL) {
            return -1;
        }
        ptr8 = (UTF8 *) cpath;
        ptr16 = (UTF16 *) tpath;

        retval =
            tsk_UTF16toUTF8(fs->endian, (const UTF16 **) &ptr16, (UTF16 *)
            & ptr16[TSTRLEN(tpath) + 1], &ptr8,
            (UTF8 *) ((uintptr_t) ptr8 + clen), TSKlenientConversion);
        if (retval != TSKconversionOK) {
            tsk_error_reset();
            tsk_errno = TSK_ERR_FS_UNICODE;
            snprintf(tsk_errstr, TSK_ERRSTR_L,
                "tsk_fs_ifind_path: Error converting path to UTF-8: %d",
                retval);
            free(cpath);
            return -1;
        }
        return tsk_fs_path2inum(fs, cpath, result, NULL);
    }
#else
    return tsk_fs_path2inum(fs, (const char *) tpath, result, NULL);
#endif
}






/*******************************************************************************
 * Find an inode given a data unit
 */

typedef struct {
    TSK_DADDR_T block;          /* the block to find */
    TSK_FS_IFIND_FLAG_ENUM flags;
    uint8_t found;

    TSK_INUM_T curinode;        /* the inode being analyzed */
    uint32_t curtype;           /* the type currently being analyzed: NTFS */
    uint16_t curid;
} IFIND_DATA_DATA;


/*
 * file_walk action for non-ntfs
 */
static TSK_WALK_RET_ENUM
ifind_data_file_act(TSK_FS_FILE * fs_file, TSK_OFF_T a_off,
    TSK_DADDR_T addr, char *buf, size_t size, TSK_FS_BLOCK_FLAG_ENUM flags,
    void *ptr)
{
    TSK_FS_INFO *fs = fs_file->fs_info;
    IFIND_DATA_DATA *data = (IFIND_DATA_DATA *) ptr;

    /* Drop references to block zero (sparse)
     * This becomes an issue with fragments and looking for fragments
     * within the first block.  They will be triggered by sparse 
     * entries, even though the first block can not be allocated
     */
    if (!addr)
        return TSK_WALK_CONT;

    if ((data->block >= addr) &&
        (data->block <
            (addr + (size + fs->block_size - 1) / fs->block_size))) {
        tsk_printf("%" PRIuINUM "\n", data->curinode);
        data->found = 1;

        if (!(data->flags & TSK_FS_IFIND_ALL))
            return TSK_WALK_STOP;

    }
    return TSK_WALK_CONT;
}


/* 
 * file_walk action callback for ntfs  
 *
 */
static TSK_WALK_RET_ENUM
ifind_data_file_ntfs_act(TSK_FS_FILE * fs_file, TSK_OFF_T a_off,
    TSK_DADDR_T addr, char *buf, size_t size, TSK_FS_BLOCK_FLAG_ENUM flags,
    void *ptr)
{
    IFIND_DATA_DATA *data = (IFIND_DATA_DATA *) ptr;

    if (addr == data->block) {
        tsk_printf("%" PRIuINUM "-%" PRIu32 "-%" PRIu16 "\n",
            data->curinode, data->curtype, data->curid);
        data->found = 1;

        if (!(data->flags & TSK_FS_IFIND_ALL)) {
            return TSK_WALK_STOP;
        }
    }
    return TSK_WALK_CONT;
}



/*
** find_inode
**
** Callback action for inode_walk
*/
static TSK_WALK_RET_ENUM
ifind_data_act(TSK_FS_FILE * fs_file, void *ptr)
{
    IFIND_DATA_DATA *data = (IFIND_DATA_DATA *) ptr;
    int file_flags = (TSK_FS_FILE_WALK_FLAG_AONLY);

    data->curinode = fs_file->meta->addr;

    /* NT Specific Stuff: search all ADS */
    if (TSK_FS_TYPE_ISNTFS(fs_file->fs_info->ftype)) {
        int i, cnt;

        file_flags |= TSK_FS_FILE_WALK_FLAG_SLACK;
        cnt = tsk_fs_file_attr_getsize(fs_file);
        for (i = 0; i < cnt; i++) {
            const TSK_FS_ATTR *fs_attr =
                tsk_fs_file_attr_get_idx(fs_file, i);
            if (!fs_attr)
                continue;

            data->curtype = fs_attr->type;
            data->curid = fs_attr->id;
            if (fs_attr->flags & TSK_FS_ATTR_NONRES) {
                if (tsk_fs_file_walk_type(fs_file, fs_attr->type,
                        fs_attr->id, file_flags, ifind_data_file_ntfs_act,
                        ptr)) {
                    if (tsk_verbose)
                        tsk_fprintf(stderr,
                            "Error walking file %" PRIuINUM,
                            fs_file->meta->addr);

                    /* Ignore these errors */
                    tsk_error_reset();
                }
            }
        }
        return TSK_WALK_CONT;
    }
    else if (TSK_FS_TYPE_ISFAT(fs_file->fs_info->ftype)) {
        file_flags |= (TSK_FS_FILE_WALK_FLAG_SLACK);
        if (tsk_fs_file_walk(fs_file, file_flags,
                ifind_data_file_act, ptr)) {
            if (tsk_verbose)
                tsk_fprintf(stderr, "Error walking file %" PRIuINUM,
                    fs_file->meta->addr);

            /* Ignore these errors */
            tsk_error_reset();
        }
    }
    /* UNIX do not need the SLACK flag because they use fragments - if the
     * SLACK flag exists then any unused fragments in a block will be 
     * correlated with the incorrect inode
     */
    else {
        // @@@ Need to add handling back in here to find indirect blocks (once a soln is found)
        if (tsk_fs_file_walk(fs_file, file_flags,
                ifind_data_file_act, ptr)) {
            if (tsk_verbose)
                tsk_fprintf(stderr, "Error walking file %" PRIuINUM,
                    fs_file->meta->addr);

            /* Ignore these errors */
            tsk_error_reset();
        }
    }

    return TSK_WALK_CONT;
}




/* 
 * Find the inode that has allocated block blk
 * Return 1 on error, 0 if no error */
uint8_t
tsk_fs_ifind_data(TSK_FS_INFO * fs, TSK_FS_IFIND_FLAG_ENUM lclflags,
    TSK_DADDR_T blk)
{
    IFIND_DATA_DATA data;

    memset(&data, 0, sizeof(IFIND_DATA_DATA));
    data.flags = lclflags;
    data.block = blk;

    if (fs->inode_walk(fs, fs->first_inum, fs->last_inum,
            TSK_FS_META_FLAG_ALLOC | TSK_FS_META_FLAG_UNALLOC,
            ifind_data_act, &data)) {
        return 1;
    }

    /* 
     * If we did not find an inode yet, we call block_walk for the 
     * block to find out the associated flags so we can identify it as
     * a meta data block */
    if (!data.found) {
        TSK_FS_BLOCK *fs_block;

        if ((fs_block = tsk_fs_block_get(fs, NULL, blk)) != NULL) {

            if (fs_block->flags & TSK_FS_BLOCK_FLAG_META) {
                tsk_printf("Meta Data\n");
                data.found = 1;
            }
            tsk_fs_block_free(fs_block);
        }
    }
    if (!data.found) {
        tsk_printf("Inode not found\n");
    }
    return 0;
}

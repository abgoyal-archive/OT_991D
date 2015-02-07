

/* IOCTL information */
#define FSCTL_REQUEST_OPLOCK_LEVEL_1 0x00090000
#define FSCTL_REQUEST_OPLOCK_LEVEL_2 0x00090004
#define FSCTL_REQUEST_BATCH_OPLOCK   0x00090008
#define FSCTL_LOCK_VOLUME            0x00090018
#define FSCTL_UNLOCK_VOLUME          0x0009001C
#define FSCTL_IS_PATHNAME_VALID      0x0009002C /* BB add struct */
#define FSCTL_GET_COMPRESSION        0x0009003C /* BB add struct */
#define FSCTL_SET_COMPRESSION        0x0009C040 /* BB add struct */
#define FSCTL_QUERY_FAT_BPB          0x00090058 /* BB add struct */
/* Verify the next FSCTL number, we had it as 0x00090090 before */
#define FSCTL_FILESYSTEM_GET_STATS   0x00090060 /* BB add struct */
#define FSCTL_GET_NTFS_VOLUME_DATA   0x00090064 /* BB add struct */
#define FSCTL_GET_RETRIEVAL_POINTERS 0x00090073 /* BB add struct */
#define FSCTL_IS_VOLUME_DIRTY        0x00090078 /* BB add struct */
#define FSCTL_ALLOW_EXTENDED_DASD_IO 0x00090083 /* BB add struct */
#define FSCTL_REQUEST_FILTER_OPLOCK  0x0009008C
#define FSCTL_FIND_FILES_BY_SID      0x0009008F /* BB add struct */
#define FSCTL_SET_OBJECT_ID          0x00090098 /* BB add struct */
#define FSCTL_GET_OBJECT_ID          0x0009009C /* BB add struct */
#define FSCTL_DELETE_OBJECT_ID       0x000900A0 /* BB add struct */
#define FSCTL_SET_REPARSE_POINT      0x000900A4 /* BB add struct */
#define FSCTL_GET_REPARSE_POINT      0x000900A8 /* BB add struct */
#define FSCTL_DELETE_REPARSE_POINT   0x000900AC /* BB add struct */
#define FSCTL_SET_OBJECT_ID_EXTENDED 0x000900BC /* BB add struct */
#define FSCTL_CREATE_OR_GET_OBJECT_ID 0x000900C0 /* BB add struct */
#define FSCTL_SET_SPARSE             0x000900C4 /* BB add struct */
#define FSCTL_SET_ZERO_DATA          0x000900C8 /* BB add struct */
#define FSCTL_SET_ENCRYPTION         0x000900D7 /* BB add struct */
#define FSCTL_ENCRYPTION_FSCTL_IO    0x000900DB /* BB add struct */
#define FSCTL_WRITE_RAW_ENCRYPTED    0x000900DF /* BB add struct */
#define FSCTL_READ_RAW_ENCRYPTED     0x000900E3 /* BB add struct */
#define FSCTL_READ_FILE_USN_DATA     0x000900EB /* BB add struct */
#define FSCTL_WRITE_USN_CLOSE_RECORD 0x000900EF /* BB add struct */
#define FSCTL_SIS_COPYFILE           0x00090100 /* BB add struct */
#define FSCTL_RECALL_FILE            0x00090117 /* BB add struct */
#define FSCTL_QUERY_SPARING_INFO     0x00090138 /* BB add struct */
#define FSCTL_SET_ZERO_ON_DEALLOC    0x00090194 /* BB add struct */
#define FSCTL_SET_SHORT_NAME_BEHAVIOR 0x000901B4 /* BB add struct */
#define FSCTL_QUERY_ALLOCATED_RANGES 0x000940CF /* BB add struct */
#define FSCTL_SET_DEFECT_MANAGEMENT  0x00098134 /* BB add struct */
#define FSCTL_SIS_LINK_FILES         0x0009C104
#define FSCTL_PIPE_PEEK              0x0011400C /* BB add struct */
#define FSCTL_PIPE_TRANSCEIVE        0x0011C017 /* BB add struct */
/* strange that the number for this op is not sequential with previous op */
#define FSCTL_PIPE_WAIT              0x00110018 /* BB add struct */
#define FSCTL_LMR_GET_LINK_TRACK_INF 0x001400E8 /* BB add struct */
#define FSCTL_LMR_SET_LINK_TRACK_INF 0x001400EC /* BB add struct */

#define IO_REPARSE_TAG_MOUNT_POINT   0xA0000003
#define IO_REPARSE_TAG_HSM           0xC0000004
#define IO_REPARSE_TAG_SIS           0x80000007
//
// Created by 19338286 on 09.11.2021.
//

#ifndef EXT2READER__STRUCTURES_H_
#define EXT2READER__STRUCTURES_H_
#include <inttypes.h>

#define EXT2_SUPER_MAGIC 0xEF53
#define EXT2_VALID_FS 1
#define EXT2_ERROR_FS 2
#define EXT2_ERRORS_CONTINUE 1
#define EXT2_ERRORS_RO 2
#define EXT2_ERRORS_PANIC 3
#define EXT2_OS_LINUX 0
#define EXT2_OS_HURD 1
#define EXT2_OS_MASIC 2
#define EXT2_OS_FREEBSD 3
#define EXT2_OS_LITES 4
#define EXT2_GOOD_OLD_REV 0
#define EXT2_DYNAMIC_REV 1
#define EXT2_DEF_RESUID 0
#define EXT2_DEF_RESGID 0
#define EXT2_GOOD_OLD_FIRST_INO 11
#define EXT2_GOOD_OLD_INODE_SIZE 128
#define EXT2_FEATURE_COMPAT_DIR_PREALLOC 0x0001
#define EXT2_FEATURE_COMPAT_IMAGIC_INODES 0x0002
#define EXT3_FEATURE_COMPAT_HAS_JOURNAL 0x0004
#define EXT2_FEATURE_COMPAT_EXT_ATTR 0x0008
#define EXT2_FEATURE_COMPAT_RESIZE_INO 0x0010
#define EXT2_FEATURE_COMPAT_DIR_INDEX 0x0020
#define EXT2_FEATURE_INCOMPAT_COMPRESSION 0x0001
#define EXT2_FEATURE_INCOMPAT_FILETYPE 0x0002
#define EXT3_FEATURE_INCOMPAT_RECOVER 0x0004
#define EXT3_FEATURE_INCOMPAT_JOURNAL_DEV 0x0008
#define EXT2_FEATURE_INCOMPAT_META_BG 0x0010
#define EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER 0x0001
#define EXT2_FEATURE_RO_COMPAT_LARGE_FILE 0x0002
#define EXT2_FEATURE_RO_COMPAT_BTREE_DIR 0x0004
#define EXT2_LZV1_ALG 0x00000001
#define EXT2_LZRW3A_ALG 0x00000002
#define EXT2_GZIP_ALG 0x00000004
#define EXT2_BZIP2_ALG 0x00000008
#define EXT2_LZO_ALG 0x00000010

struct superblock {
  uint32_t s_inodes_count;

  uint32_t s_blocks_count;

  uint32_t s_r_blocks_count;

  uint32_t s_free_blocks_count;

  uint32_t s_free_inodes_count;

  uint32_t s_first_data_block;

  uint32_t s_log_block_size;

  int32_t s_log_frag_size;

  uint32_t s_blocks_per_group;

  uint32_t s_frags_per_group;

  uint32_t s_inodes_per_group;

  uint32_t s_mtime;

  uint32_t s_wtime;

  uint16_t s_mnt_count;

  uint16_t s_max_mnt_count;

  uint16_t s_magic;

  uint16_t s_state;

  uint16_t s_minor_rev_level;

  uint32_t s_lastcheck;

  uint32_t s_checkinterval;

  uint32_t s_creator_os;

  uint32_t s_rev_level;

  uint16_t s_def_resuid;

  uint16_t s_def_resgid;

  uint32_t s_first_ino;

  uint16_t s_inode_size;

  uint16_t s_block_group_nr;

  uint32_t s_feature_compat;

  uint32_t s_feature_incompat;

  uint32_t s_feature_ro_compat;

  uint8_t s_uuid[16];

  uint8_t s_volume_name[16];

  uint8_t s_last_mounted[64];

  uint32_t s_algo_bitmap;

  uint8_t s_prealloc_blocks;

  uint8_t s_prealloc_dir_blocks;

  uint16_t s_padding_1;

  uint8_t s_journal_uuid[16];

  uint32_t s_journal_inum;

  uint32_t s_journal_dev;

  uint32_t s_last_orphan;

  uint32_t s_hash_seed[4];

  uint8_t s_def_hash_version;

  uint8_t s_padding_2[3];

  uint32_t s_default_mount_options;

  uint32_t s_first_meta_bg;

  uint8_t s_unused[760];
};

struct blockgroup {
  uint32_t first_block_in_blockgroup;
  uint32_t last_block_in_blockgroup;
};

struct fs_metadata {
  uint32_t disk_size;
  uint32_t block_size;
  uint32_t num_blocks;
  uint32_t blockgroup_size;
  uint32_t inodes_per_group;
  uint32_t inode_blocks_per_group;
  uint32_t num_blockgroups;
  uint32_t num_blocks_per_desc_table;

  struct blockgroup *offsets;

  struct superblock *sb;

  struct blockgroup_descriptor *bgdt;
};

struct blockgroup_descriptor {
  uint32_t bg_block_bitmap;

  uint32_t bg_inode_bitmap;

  uint32_t bg_inode_table;

  uint16_t bg_free_blocks_count;

  uint16_t bg_free_inodes_count;

  uint16_t bg_used_dirs_count;

  uint16_t bg_pad;

  uint8_t bg_reserved[12];
};

struct inode {
  uint16_t i_mode;

  uint16_t i_uid;

  uint32_t i_size;

  uint32_t i_atime;

  uint32_t i_ctime;

  uint32_t i_mtime;

  uint32_t i_dtime;

  uint16_t i_gid;

  uint16_t i_links_count;

  uint16_t i_blocks;

  uint32_t i_flags;

  union {
    struct {
      uint32_t l_i_reserved1;
    } linux1;
    struct {
      uint32_t h_i_translator;
    } hurd1;
    struct {
      uint32_t m_i_reserved1;
    } masix1;
  } i_osdi1;

  uint32_t i_block[15];

  uint32_t i_generation;

  uint32_t i_file_acl;

  uint32_t i_dir_acl;

  uint32_t i_faddr;

  union {
    struct {
      uint8_t l_i_frag;       /* Fragment number */
      uint8_t l_i_fsize;      /* Fragment size */
      uint16_t i_pad1;
      uint16_t l_i_uid_high;   /* these 2 fields    */
      uint16_t l_i_gid_high;   /* were reserved2[0] */
      uint32_t l_i_reserved2;
    } linux2;
    struct {
      uint8_t h_i_frag;       /* Fragment number */
      uint8_t h_i_fsize;      /* Fragment size */
      uint16_t h_i_mode_high;
      uint16_t h_i_uid_high;
      uint16_t h_i_gid_high;
      uint32_t h_i_author;
    } hurd2;
    struct {
      uint8_t m_i_frag;       /* Fragment number */
      uint8_t m_i_fsize;      /* Fragment size */
      uint16_t m_pad1;
      uint32_t m_i_reserved2[2];
    } masix2;
  } i_osd2;
};

#define EXT2_S_IFSOCK 0xC000
#define EXT2_S_IFLNK  0xA000
#define EXT2_S_IFREG  0x8000
#define EXT2_S_IFBLK  0x6000
#define EXT2_S_IFDIR  0x4000
#define EXT2_S_IFCHR  0x2000
#define EXT2_S_IFIFO  0x1000
#define EXT2_S_ISUID  0x0800
#define EXT2_S_ISGID  0x0400
#define EXT2_S_ISVTX  0x0200
#define EXT2_S_IRUSR  0x0100
#define EXT2_S_IWUSR  0x0080
#define EXT2_S_IXUSR  0x0040
#define EXT2_S_IRGRP  0x0020
#define EXT2_S_IWGRP  0x0010
#define EXT2_S_IXGRP  0x0008
#define EXT2_S_IROTH  0x0004
#define EXT2_S_IWOTH  0x0002
#define EXT2_S_IXOTH  0x0001

struct fs_inode_blocks_iter {
  struct inode* s_ino;
  int cur_block;
};

struct open_file {
  char *buf;
  uint64_t size;
};

typedef struct open_file open_file;


#endif //EXT2READER__STRUCTURES_H_

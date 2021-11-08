#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>

#include "structures.h"
#define DEBUG

char *map_file(char *filename, open_file *file) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("open: ");
        abort();
    }
    struct stat st;
    if (fstat(fd, &st) == -1) {
        perror("fstat: ");
        abort();
    }
    char *buf = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (!buf) {
        perror("mmap: ");
        abort();
    }
    file->buf = buf;
    file->size = st.st_size;
    return buf;
}

void check_superblock(struct os_superblock_t *sb) {
    if (sb->s_magic != EXT2_SUPER_MAGIC) {
        printf("Superblock magic error: found %x", sb->s_magic);
    }
}

void print_superblock(struct os_superblock_t *sb) {
    printf("-----------------------Superblock info -------------------------");
    printf("s_blocks_count=%d\n", sb->s_blocks_count);
    printf("s_free_blocks_count=%d\n", sb->s_free_blocks_count);
    printf("s_block_size=1024*(2^%d)\n", sb->s_log_block_size);
    printf("s_inode_size=%d\n", sb->s_inode_size);
    printf("s_volume_name=%s\n", sb->s_volume_name);
    printf("-----------------------Superblock info end ----------------------");
}

struct os_superblock_t *read_superblock(open_file *file) {
    if (file->size < 2048) {
        printf("File size not enough: %ld", file->size);
        abort();
    }
    struct os_superblock_t *sb = (file->buf + 1024);
    check_superblock(sb);
#ifdef DEBUG
    print_superblock(sb);
#endif
    return sb;
}

struct os_fs_metadata_t *read_metadata(open_file *file, struct os_superblock_t *sb) {
    struct os_fs_metadata_t *metadata = calloc(1, sizeof(struct os_fs_metadata_t));
    if (metadata == NULL) {
        perror("malloc: ");
        abort();
    }
    metadata->disk_size = file->size;
    metadata->block_size = 1024 << sb->s_log_block_size;
    metadata->num_blocks = sb->s_blocks_count;
    metadata->inodes_per_group = sb->s_inodes_per_group;
    metadata->blockgroup_size = sb->s_blocks_per_group;
    uint32_t inodes_per_block = ceil(1.0 * metadata->block_size / sb->s_inode_size);
    metadata->inode_blocks_per_group = ceil(1.0 * sb->s_inodes_per_group / inodes_per_block);

    metadata->num_blockgroups = (metadata->num_blocks - sb->s_first_data_block) / metadata->blockgroup_size;
    uint32_t remainder = (metadata->num_blocks - sb->s_first_data_block) % metadata->blockgroup_size;
    uint32_t num_descriptortable_blocks = ((metadata->num_blockgroups + (remainder == 0 ? 0 : 1)) *
        sizeof(struct os_blockgroup_descriptor_t)) / metadata->block_size;
    uint32_t ndb_remainder = ((metadata->num_blockgroups + (remainder == 0 ? 0 : 1)) *
        sizeof(struct os_blockgroup_descriptor_t)) % metadata->block_size;
    if (ndb_remainder > 0) {
        num_descriptortable_blocks += 1;
    }
    uint32_t overhead = 3 + num_descriptortable_blocks + metadata->inode_blocks_per_group;
    if (remainder >= overhead + 50) {
        metadata->num_blockgroups++;
    }

    metadata->num_blocks_per_desc_table = num_descriptortable_blocks;
    metadata->offsets = malloc(metadata->num_blockgroups *
        sizeof(struct os_blockgroup_offsets_t));
    if (metadata->offsets == NULL) {
        perror("malloc: ");
        abort();
    }

    for (int i = 0; i < metadata->num_blockgroups; i++) {
        metadata->offsets[i].first_block_in_blockgroup = 1 + i * metadata->blockgroup_size;
        metadata->offsets[i].last_block_in_blockgroup = (i + 1) * metadata->blockgroup_size;
    }
    metadata->offsets[metadata->num_blockgroups - 1].last_block_in_blockgroup = metadata->num_blocks - 1;
    metadata->sb = sb;

    return metadata;
}

struct os_blockgroup_descriptor_t *read_bgdt(open_file *file,
                                             struct os_fs_metadata_t *metadata) {
    struct os_blockgroup_descriptor_t *bgd_table = file->buf + 1024 + 1024;

    metadata->bgdt = bgd_table;
    return bgd_table;
}

struct os_inode_t *fetch_inode(uint32_t inode_number, open_file *file,
                               struct os_fs_metadata_t *metadata) {
    uint32_t blockgroup_num = (inode_number - 1) / metadata->inodes_per_group;
    uint32_t offset_within_blockgroup = (inode_number - 1) % metadata->inodes_per_group;

    if (blockgroup_num >= metadata->num_blockgroups) {
        printf("Couldn't get inode with number %d", inode_number);
        return NULL;
    }
    uint32_t num_inodes_per_block = metadata->block_size / sizeof(struct os_inode_t);

    uint32_t inode_block_num = offset_within_blockgroup / num_inodes_per_block;
    inode_block_num += metadata->bgdt[blockgroup_num].bg_inode_table;
    uint32_t offset_in_block = offset_within_blockgroup % num_inodes_per_block;
    uint32_t block_n = inode_block_num * metadata->block_size;
    return file->buf + block_n + (offset_in_block * sizeof(struct os_inode_t));
}

void calculate_offsets(uint32_t blocknum,
                       uint32_t blocksize,
                       int32_t *direct_num,
                       int32_t *indirect_index,
                       int32_t *double_index,
                       int32_t *triple_index) {
    if (blocknum <= 11) {
        *direct_num = blocknum;
        *indirect_index = *double_index = *triple_index = -1;
        return;
    }
    uint32_t blocks_left = blocknum - 12;

    if (blocks_left < blocksize / 4) {
        *direct_num = *double_index = *triple_index = -1;
        *indirect_index = blocks_left;
        return;
    }
    blocks_left -= (blocksize / 4);

    if (blocks_left < (blocksize / 4) * (blocksize / 4)) {
        *direct_num = *triple_index = -1;

        *double_index = blocks_left / (blocksize / 4);
        *indirect_index = blocks_left -
            (*double_index) * (blocksize / 4);
        return;
    }
    blocks_left -= (blocksize / 4) * (blocksize / 4);

    if (blocks_left < (blocksize / 4) * (blocksize / 4) * (blocksize / 4)) {
        *direct_num = -1;
        *triple_index = blocks_left / (blocksize / 4) / (blocksize / 4);
        *double_index = blocks_left - (*triple_index) * (blocksize / 4) * (blocksize / 4);
        *indirect_index = blocks_left % ((*double_index) * (blocksize / 4);
        return;
    }
    printf("BlockNum incorrect\n");
    abort();
}

uint32_t file_blockread(struct os_inode_t file_inode, open_file *file,
                        struct os_fs_metadata_t *metadata,
                        uint32_t blocknum, unsigned char *buffer) {
    char range_in_hole = 0;
    if (blocknum * metadata->block_size >= file_inode.i_size) {
        return -1;
    }
    int32_t direct_index, single_index, double_index, triple_index;
    calculate_offsets(blocknum, metadata->block_size, &direct_index,
                      &single_index, &double_index, &triple_index);

    uint32_t ti_blocknum = file_inode.i_block[14];
    uint32_t do_blocknum = 0;
    int res;

    if ((triple_index != -1) && (ti_blocknum == 0)) {
        range_in_hole = 1;
    } else if (triple_index != -1) {
        char *tmp_buf = file->buf + ti_blocknum * metadata->block_size;
        if (triple_index >= (metadata->block_size / 4)) {
            printf("Triple index error: %d", triple_index);
            abort();
        }
        do_blocknum = *(((uint32_t *) tmp_buf) + triple_index);
    }

    uint32_t si_blocknum = 0;
    if ((!range_in_hole) && (double_index != -1)) {
        if (triple_index == -1) {
            do_blocknum = file_inode.i_block[13];
        }

        if (do_blocknum == 0) {
            range_in_hole = 1;
        } else {
            char *tmp_buf = file->buf + do_blocknum * metadata->block_size;
            if (double_index >= (metadata->block_size / 4)) {
                printf("Double index error: %d", double_index);
                abort();
            }
            si_blocknum = *(((uint32_t *) tmp_buf) + double_index);
        }
    }

    uint32_t direct_blocknum = 0;
    if ((!range_in_hole) && (single_index != -1)) {
        assert(direct_index == -1);
        if (double_index == -1) {
            si_blocknum = file_inode.i_block[12];
        }

        if (si_blocknum == 0) {
            range_in_hole = 1;
        } else {
            char *tmp_buf = file->buf + si_blocknum * metadata->block_size;
            assert(single_index < (metadata->block_size / 4));
            direct_blocknum = *(((uint32_t *) tmp_buf) + single_index);
        }
    }
    if ((!range_in_hole) && (direct_index != -1)) {
        assert(direct_index < 12);
        direct_blocknum = file_inode.i_block[direct_index];
    }
    if (range_in_hole) {
        memset(buffer, 0, metadata->block_size);
    } else {
        memcpy(buffer, file->buf + direct_blocknum * metadata->block_size, metadata->block_size);
    }
    uint32_t diff =
        file_inode.i_size - blocknum * metadata->block_size;
    if (diff >= metadata->block_size)
        return metadata->block_size;
    return diff;
}

char file_read(open_file *file, int file_inode_num,
               struct os_fs_metadata_t *metadata,
               unsigned char **buffer) {
    struct os_inode_t *inode = fetch_inode(file_inode_num, file, metadata);
    if (!(inode->i_mode & (EXT2_S_IFREG | EXT2_S_IFDIR))) {
        printf("Wrong inode type=%d\n", inode->i_mode);
        return 0;
    }
    if (inode->i_size == 0) {
        return 1;
    }
    uint32_t malloc_size = inode->i_size;
    uint32_t blk_count = inode->i_size / metadata->block_size;
    if (malloc_size % metadata->block_size != 0) {
        malloc_size +=
            metadata->block_size - (malloc_size % metadata->block_size);
        blk_count++;
    }
    *buffer = calloc(malloc_size, 1);
    if (*buffer == NULL) {
        printf("Calloc failed\n");
        return 0;
    }

    for (int block_ind = 0; block_ind < blk_count; block_ind++) {
        file_blockread(*inode, file, metadata, block_ind, (*buffer) + block_ind * metadata->block_size);
    }
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: ./reader <filename> <index>\n");
        return 1;
    }
    int block_idx = 0;
    char* end_ptr;
    block_idx = strtol(argv[2], &end_ptr, 10);
    if (errno != 0 || end_ptr == argv[2]) {
        perror("strtol: ");
        return 1;
    }
    open_file file = {0};
    map_file(argv[1], &file);
    struct os_superblock_t sb = {0};
    read_superblock(&file);
    struct os_fs_metadata_t* metadata = read_metadata(&file, &sb);
    struct os_blockgroup_descriptor_t* bgdt = read_bgdt(&file, metadata);
    uint8_t *buffer = 0;
    if (file_read(&file, block_idx, metadata, &buffer)) {
        printf("File red - %s", buffer);
    } else {
        printf("File read error\n");
        return 1;
    }
    return 0;
}

#include "structures.h"

#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>


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
    close(fd);
    return buf;
}

void close_file(open_file* file) {
    if (munmap(file->buf, file->size) != 0) {
        perror("unmap: ");
        return;
    }
}

void check_superblock(struct superblock *sb) {
    if (sb->s_magic != EXT2_SUPER_MAGIC) {
        printf("Superblock magic error: found %x", sb->s_magic);
    }
}

void print_superblock(struct superblock *sb) {
    printf("-----------------------Superblock info -------------------------");
    printf("s_blocks_count=%d\n", sb->s_blocks_count);
    printf("s_free_blocks_count=%d\n", sb->s_free_blocks_count);
    printf("s_block_size=1024*(2^%d)\n", sb->s_log_block_size);
    printf("s_inode_size=%d\n", sb->s_inode_size);
    printf("s_volume_name=%s\n", sb->s_volume_name);
    printf("-----------------------Superblock info end ----------------------");
}

struct superblock *read_superblock(open_file *file) {
    if (file->size < 2048) {
        printf("File size not enough: %ld", file->size);
        abort();
    }
    struct superblock *sb = (file->buf + 1024);
    check_superblock(sb);
#ifdef DEBUG
    print_superblock(sb);
#endif
    return sb;
}

struct fs_metadata *read_metadata(open_file *file, struct superblock *sb) {
    struct fs_metadata *metadata = calloc(1, sizeof(struct fs_metadata));
    if (metadata == NULL) {
        perror("malloc: ");
        abort();
    }
    metadata->disk_size = file->size;
    metadata->block_size = 1024 << sb->s_log_block_size;
    metadata->num_blocks = sb->s_blocks_count;
    metadata->inodes_per_group = sb->s_inodes_per_group;
    metadata->blockgroup_size = sb->s_blocks_per_group;
    double inodes_per_block = 1.0 * metadata->block_size / sb->s_inode_size;
    metadata->inode_blocks_per_group = ceil(1.0 * sb->s_inodes_per_group / inodes_per_block);

    metadata->num_blockgroups = (metadata->num_blocks - sb->s_first_data_block) / metadata->blockgroup_size;
    uint32_t remainder = (metadata->num_blocks - sb->s_first_data_block) % metadata->blockgroup_size;
    uint32_t num_descriptortable_blocks = ((metadata->num_blockgroups + (remainder == 0 ? 0 : 1)) *
        sizeof(struct blockgroup_descriptor)) / metadata->block_size;
    uint32_t ndb_remainder = ((metadata->num_blockgroups + (remainder == 0 ? 0 : 1)) *
        sizeof(struct blockgroup_descriptor)) % metadata->block_size;
    if (ndb_remainder > 0) {
        num_descriptortable_blocks += 1;
    }
    uint32_t overhead = 3 + num_descriptortable_blocks + metadata->inode_blocks_per_group;
    if (remainder >= overhead + 50) {
        metadata->num_blockgroups++;
    }

    metadata->num_blocks_per_desc_table = num_descriptortable_blocks;
    metadata->offsets = malloc(metadata->num_blockgroups *
        sizeof(struct blockgroup));
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

void delete_metadata(struct fs_metadata* metadata) {
    free(metadata->offsets);
    free(metadata);
}

struct blockgroup_descriptor *read_bgdt(open_file *file,
                                        struct fs_metadata *metadata) {
    struct blockgroup_descriptor *bgd_table = file->buf + 1024 + 1024;

    metadata->bgdt = bgd_table;
    return bgd_table;
}

struct inode *fetch_inode(uint32_t inode_number, open_file *file,
                          struct fs_metadata *metadata) {
    uint32_t blockgroup_num = (inode_number - 1) / metadata->inodes_per_group;
    uint32_t offset_within_blockgroup = (inode_number - 1) % metadata->inodes_per_group;

    if (blockgroup_num >= metadata->num_blockgroups) {
        printf("Couldn't get inode with number %d", inode_number);
        return NULL;
    }
    uint32_t num_inodes_per_block = metadata->block_size / sizeof(struct inode);

    uint32_t inode_block_num = offset_within_blockgroup / num_inodes_per_block;
    inode_block_num += metadata->bgdt[blockgroup_num].bg_inode_table;
    uint32_t offset_in_block = offset_within_blockgroup % num_inodes_per_block;
    uint32_t block_n = inode_block_num * metadata->block_size;
    return file->buf + block_n + (offset_in_block * sizeof(struct inode));
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
        *double_index = (blocks_left - (*triple_index) * (blocksize / 4) * (blocksize / 4)) / (blocksize / 4);
        *indirect_index = (blocks_left - (*triple_index) * (blocksize / 4) * (blocksize / 4)) - (*double_index) * (blocksize / 4);
        return;
    }
    printf("BlockNum incorrect\n");
    abort();
}

int32_t file_blockread(struct inode file_inode, open_file *file,
                        struct fs_metadata *metadata,
                        uint32_t blocknum, char *buffer) {
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

int32_t inode_blocks_iter_next(
    struct fs_metadata* metadata,
    open_file* file,
    struct fs_inode_blocks_iter* iter,
        char *buffer) {

    int ret_val = file_blockread(*iter->s_ino, file, metadata, iter->cur_block, buffer);
    iter->cur_block++;
    return ret_val;
}

char file_read(open_file *file, int file_inode_num,
               struct fs_metadata *metadata) {
    struct inode *inode = fetch_inode(file_inode_num, file, metadata);
    if (!(inode->i_mode & (EXT2_S_IFREG | EXT2_S_IFDIR))) {
        printf("Wrong inode type=%d\n", inode->i_mode);
        return 0;
    }
    if (inode->i_size == 0) {
        return 1;
    }
    char* buffer = calloc(metadata->block_size, 1);

    struct fs_inode_blocks_iter iter;
    iter.s_ino = inode;
    iter.cur_block = 0;
    int32_t read_bytes = 0;
    while ((read_bytes = inode_blocks_iter_next(metadata, file, &iter, buffer)) != -1) {
        write(1, buffer, read_bytes);
    }
    free(buffer);
    return 1;
}

char pop_next_dir(char *path,
                       char **next_component) {
    if ((path == NULL) || (*path == '\0'))
        return 0;
    if (strcmp(path, "/") == 0)
        return 0;
    if (*path != '/')
        return 0;

    uint32_t len = strlen(path);

    char *next_slash = strchr(path + 1, '/');

    if (next_slash == NULL) {
        *next_component = (char *) calloc(len, 1);
        if (*next_component == NULL)
            return 0;
        strcpy(*next_component, path+1);
        *path = '\0';
        return 1;
    }

    *next_component = (char *) calloc(next_slash - path, 1);
    *next_slash = '\0';
    strcpy(*next_component, path + 1);
    *next_slash = '/';

    for (int i = 0; i < len - ((int) (next_slash - path)) + 1; i++) {
        path[i] = path[i + (int) (next_slash - path)];
    }

    if (strcmp(path, "/") == 0)
        *path = '\0';
    return 1;
}

uint32_t scan_dir(unsigned char *directory_block,
                  uint32_t directory_length,
                  char *filename) {
    struct direntry_t current_entry;
    uint32_t current_offset = 0;

    if (strlen((char *) filename) == 0)
        return 0;

    memcpy(&current_entry, directory_block, sizeof(current_entry));

    while (current_offset < directory_length) {
        char cur_filename[EXT2_NAME_LEN] = {0};
        memcpy(cur_filename, current_entry.file_name, current_entry.name_len);
        cur_filename[current_entry.name_len] = '\0';
        if (current_entry.inode != 0) {
#ifdef DEBUG
            printf("File found: %s", cur_filename);
#endif
            if (strcmp(filename, cur_filename) == 0) {
                return current_entry.inode;
            }
        } else {
            current_offset += sizeof(current_entry);
            memcpy(&current_entry, directory_block, sizeof(current_entry));
            continue;
        }
        current_offset += current_entry.rec_len;
        memcpy(&current_entry, directory_block + current_offset, sizeof(current_entry));
    }
    return 0;
}

char path_read(char *path, open_file* file,
               struct fs_metadata *metadata) {
    if (strcmp(path, "") == 0)
        return 0;

    char *tmp_path = malloc(strlen(path) + 1);
    assert(tmp_path != NULL);
    strcpy(tmp_path, path);

    int32_t inode_num = EXT2_ROOT_INO;

    uint8_t* buffer = calloc(metadata->block_size, 1);
    if (!buffer) {
        perror("Buffer calloc: ");
        return 0;
    }

    while(1) {
        char res;
        struct inode inode;
        char *next_component = NULL;
        res = pop_next_dir(tmp_path, &next_component);
        if (res == 0) {
            if (!file_read(file, inode_num, metadata)) {
                printf("File read error!\n");
                free(tmp_path);
                free(buffer);
                return 0;
            }
            free(tmp_path);
            return 1;
        }
        struct fs_inode_blocks_iter iter = {0};
        iter.s_ino = &inode;
        int read_size = 0;
        while ((read_size = inode_blocks_iter_next(metadata, file, &iter, buffer)) != -1) {
            inode_num = scan_dir(buffer, read_size, next_component);
            if (inode_num != 0) {
                break;
            }
        }
        if (inode_num == 0) {
            printf("File/Dir not found: %s\n", next_component);
            free(next_component);
            free(buffer);
            free(tmp_path);
            return 0;
        }
        free(next_component);
        free(buffer);
    }
}


int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: ./reader [-i <inode_num>] [-n <path_to_file>] <image_filename> \n");
        return 1;
    }
    int inode_num = 0;
    char* path = NULL;
    int c = 0;
    while ((c = getopt(argc, argv, "i:n:"))) {
        switch (c) {
            case 'i': inode_num = strtol(optarg, NULL, 10); break;
            case 'n': path = strdup(optarg); break;
            case '?':
                if (optopt == 'i' || optopt == 'f') {
                    printf("Option -%c required argument\n", optopt);
                    return 1;
                } else {
                    printf("Unknown option: %c", optopt);
                    return 1;
                }
        }
    }
    if (!path && !inode_num) {
        printf("Usage: ./reader [-i <inode_num>] [-n <path_to_file>] <image_filename> \n");
        return 1;
    }
    open_file file = {0};
    map_file(argv[optind], &file);
    struct superblock sb = {0};
    read_superblock(&file);
    struct fs_metadata* metadata = read_metadata(&file, &sb);
    struct blockgroup_descriptor* bgdt = read_bgdt(&file, metadata);
    if (path) {
        if (path_read(path, &file, metadata) == 0) {
            printf("Path read failed\n");
            close_file(&file);
            delete_metadata(metadata);
            free(path);
            return 1;
        }
    } else
        if (file_read(&file, inode_num, metadata)) {
        } else {
            printf("File read error\n");
            close_file(&file);
            delete_metadata(metadata);
            free(path);
            return 1;
        }

    close_file(&file);
    delete_metadata(metadata);
    free(path);
    return 0;
}

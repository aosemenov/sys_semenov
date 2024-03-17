#include "userfs.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>


enum {
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 100,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
	/** Block memory. */
	char *memory;
	/** How many bytes are occupied. */
	int occupied;
	/** Next block in the file. */
	struct block *next;
	/** Previous block in the file. */
	struct block *prev;

	/* PUT HERE OTHER MEMBERS */
};

struct file {
	/** Double-linked list of file blocks. */
	struct block *block_list;
	/**
	 * Last block in the list above for fast access to the end
	 * of file.
	 */
	struct block *last_block;
	/** How many file descriptors are opened on the file. */
	int refs;
	/** File name. */
	char *name;
	/** Files are stored in a double-linked list. */
	struct file *next;
	struct file *prev;

	/* PUT HERE OTHER MEMBERS */
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc {
	struct file *file;
	size_t pos;

	/* PUT HERE OTHER MEMBERS */
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;

enum ufs_error_code
ufs_errno()
{
	return ufs_error_code;
}

int ufs_open(const char *filename, int flags) {
    struct file *current_file = file_list;

    while (current_file != NULL) {
        if (strcmp(current_file->name, filename) == 0) {
            break;
        }
        current_file = current_file->next;
    }

    if (current_file == NULL && !(flags & UFS_CREATE)) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    if (current_file == NULL) {
        current_file = (struct file *)malloc(sizeof(struct file));
        if (current_file == NULL) {
            ufs_error_code = UFS_ERR_NO_MEM;
            return -1;
        }
        current_file->name = strdup(filename);

        if (current_file->name == NULL) {
            free(current_file);
            ufs_error_code = UFS_ERR_NO_MEM;
            return -1;
        }

        current_file->block_list = NULL;
        current_file->last_block = NULL;
        current_file->refs = 0;
        current_file->next = file_list;
        current_file->prev = NULL;

        if (file_list != NULL) {
            file_list->prev = current_file;
        }

        file_list = current_file;
    }

    current_file->refs++;

    struct filedesc *fdesc = (struct filedesc *)malloc(sizeof(struct filedesc));
    if (fdesc == NULL) {
        ufs_error_code = UFS_ERR_NO_MEM;
        return -1;
    }
    fdesc->file = current_file;

    if (file_descriptor_count == file_descriptor_capacity) {
        int new_capacity = file_descriptor_capacity == 0 ? 1 : file_descriptor_capacity * 2;
        struct filedesc **new_fds = (struct filedesc **)realloc(file_descriptors, new_capacity * sizeof(struct filedesc *));
        if (new_fds == NULL) {
            free(fdesc);
            ufs_error_code = UFS_ERR_NO_MEM;
            return -1;
        }
        file_descriptors = new_fds;
        file_descriptor_capacity = new_capacity;
    }

    int fd;
    for (fd = 0; fd < file_descriptor_capacity; fd++) {
        if (file_descriptors[fd] == NULL) {
            file_descriptors[fd] = fdesc;
            break;
        }
    }

    if (fd == file_descriptor_capacity) {
        free(fdesc);
        ufs_error_code = UFS_ERR_NO_MEM;
        return -1;
    }

    file_descriptor_count++;
    return fd;
}

ssize_t ufs_write(int fd, const char *buf, size_t size) {
   if (fd < 0 || fd >= file_descriptor_capacity || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *fdesc = file_descriptors[fd];
    struct file *file = fdesc->file;

    size_t written = 0;
    while (size > 0) {
        if (file->last_block == NULL || file->last_block->occupied == BLOCK_SIZE) {
            if (file->last_block != NULL && file->last_block->occupied == BLOCK_SIZE) {
                if (MAX_FILE_SIZE - written < BLOCK_SIZE) {
                    break;
                }
            }

            struct block *new_block = (struct block *)malloc(sizeof(struct block));
            if (new_block == NULL) {
                ufs_error_code = UFS_ERR_NO_MEM;
                return -1;
            }
            new_block->memory = (char *)malloc(BLOCK_SIZE);
            if (new_block->memory == NULL) {
                free(new_block);
                ufs_error_code = UFS_ERR_NO_MEM;
                return -1;
            }
            new_block->occupied = 0;
            new_block->next = NULL;
            new_block->prev = file->last_block;

            if (file->last_block != NULL) {
                file->last_block->next = new_block;
            } else {
                file->block_list = new_block;
            }

            file->last_block = new_block;
        }

        size_t space_in_block = BLOCK_SIZE - file->last_block->occupied;
        size_t to_write = size < space_in_block ? size : space_in_block;
        memcpy(file->last_block->memory + file->last_block->occupied, buf, to_write);

        file->last_block->occupied += to_write;
        buf += to_write;
        size -= to_write;
        written += to_write;
    }

    return written;
}

ssize_t ufs_read(int fd, char *buf, size_t size) {
		if (fd < 0 || fd >= file_descriptor_capacity || file_descriptors[fd] == NULL) {
		    ufs_error_code = UFS_ERR_NO_FILE;
		    return -1;
		}

    struct filedesc *fdesc = file_descriptors[fd];
    struct file *file = fdesc->file;

    if (file == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    size_t read_bytes = 0;
    struct block *current_block = file->block_list;

    while (size > 0 && current_block != NULL) {
        size_t block_data_left = current_block->occupied - fdesc->pos % BLOCK_SIZE;
        size_t to_read = size < block_data_left ? size : block_data_left;
        memcpy(buf, current_block->memory + (fdesc->pos % BLOCK_SIZE), to_read);

        buf += to_read;
        size -= to_read;
        read_bytes += to_read;
        fdesc->pos += to_read;

        if (to_read == block_data_left) {
            current_block = current_block->next;
            fdesc->pos = 0;
        }

        if (current_block == NULL || to_read == 0) {
            break;
        }
    }

    return read_bytes;
}

int ufs_close(int fd) {
    if (fd < 0 || fd >= file_descriptor_capacity || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *fdesc = file_descriptors[fd];

    free(fdesc);
    file_descriptors[fd] = NULL;

    return 0;
}

int ufs_delete(const char *filename) {
		struct file **pp = &file_list;
    while (*pp && strcmp((*pp)->name, filename) != 0) {
        pp = &(*pp)->next;
    }

    if (*pp == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct file *to_delete = *pp;
    if (to_delete->refs == 0) {
        *pp = to_delete->next;

        free(to_delete->name);
        free(to_delete);
    }

    return 0;
}

void ufs_destroy(void) {
    while (file_list) {
        struct file *current = file_list;
        file_list = file_list->next;

        free(current->name);
        free(current);
    }

    for (int i = 0; i < file_descriptor_capacity; ++i) {
        if (file_descriptors[i]) {
            free(file_descriptors[i]);
        }
    }
    free(file_descriptors);
    file_descriptors = NULL;
    file_descriptor_capacity = 0;
    file_descriptor_count = 0;
}


int ufs_resize(int fd, size_t new_size) {
    if (fd < 0 || fd >= file_descriptor_capacity || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *fdesc = file_descriptors[fd];
    struct file *file = fdesc->file;
    size_t current_size = 0;
    struct block *block = file->block_list;
    struct block *last_block = NULL;

    while (block != NULL) {
        current_size += block->occupied;
        last_block = block;
        block = block->next;
    }

    if (new_size > current_size) {
        size_t additional_size = new_size - current_size;
        while (additional_size > 0) {
            struct block *new_block = (struct block *)malloc(sizeof(struct block));
            if (new_block == NULL) {
                ufs_error_code = UFS_ERR_NO_MEM;
                return -1;
            }

            new_block->memory = (char *)malloc(BLOCK_SIZE);
            if (new_block->memory == NULL) {
                free(new_block);
                ufs_error_code = UFS_ERR_NO_MEM;
                return -1;
            }

            size_t block_fill = additional_size < BLOCK_SIZE ? additional_size : BLOCK_SIZE;
            memset(new_block->memory, 0, block_fill);
            new_block->occupied = block_fill;
            new_block->next = NULL;
            new_block->prev = last_block;

            if (last_block != NULL) {
                last_block->next = new_block;
            } else {
                file->block_list = new_block;
            }

            file->last_block = new_block;
            last_block = new_block;

            additional_size -= block_fill;
        }
    } else if (new_size < current_size) {
        size_t remaining_size = new_size;
        block = file->block_list;
        while (block != NULL && remaining_size >= BLOCK_SIZE) {
            remaining_size -= block->occupied;
            block = block->next;
        }

        while (block != NULL) {
            struct block *next_block = block->next;
            free(block->memory);
            free(block);
            block = next_block;
        }

        if (last_block != NULL) {
            last_block->next = NULL;
        }
        file->last_block = last_block;
    }

    return 0;
}

#ifndef NANOD_H
#define NANOD_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#define NANOD_VERSION "1.0.0"
#define MAX_PATH_LEN 4096
#define MAX_FILES 1024
#define MAX_NAME_LEN 256

// File entry structure embedded in the generated nanod
typedef struct {
    char target_path[MAX_PATH_LEN];
    uint32_t compressed_size;
    uint32_t original_size;
    uint32_t offset;           // Offset in the binary where compressed data starts
} EmbeddedFile;

// Header at the start of generated nanod
typedef struct {
    uint32_t magic;            // 0x4E414E4F ("NANO")
    uint32_t version;          // 1
    uint32_t file_count;
    uint32_t data_offset;      // Where file data starts
    char project_name[MAX_NAME_LEN];
    char install_prefix[MAX_PATH_LEN];
} NanoDHeader;

// P.S. These are only for the runtime stub, not the builder, kinda useless now
// extern NanoDHeader _nanod_header;
// extern EmbeddedFile _nanod_files[];
// extern uint8_t _nanod_data[];

#endif

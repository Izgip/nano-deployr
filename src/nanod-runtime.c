#include "nanod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <libgen.h>
#include <errno.h>
#include <zlib.h>
#include <unistd.h>
#define MAGIC 0x4E414E4F
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

int get_self_path(char *buffer, size_t size) {
#ifdef __APPLE__
    uint32_t bufsize = (uint32_t)size;
    return _NSGetExecutablePath(buffer, &bufsize) == 0;
#else
    ssize_t len = readlink("/proc/self/exe", buffer, size - 1);
    if(len != -1) {
        buffer[len] = '\0';
        return 1;
    }
    return 0;
#endif
}

int mkdir_p(const char *path) {
    char tmp[MAX_PATH_LEN];
    char *p = NULL;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    
    if(tmp[len - 1] == '/') tmp[len - 1] = '\0';
    
    for(p = tmp + 1; *p; p++) {
        if(*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755);
}

int extract_file(const EmbeddedFile *file, const uint8_t *data) {
    char dir_path[MAX_PATH_LEN];
    strncpy(dir_path, file->target_path, MAX_PATH_LEN - 1);
    dir_path[MAX_PATH_LEN - 1] = '\0';
    char *last_slash = strrchr(dir_path, '/');
    if(last_slash) {
        *last_slash = '\0';
        mkdir_p(dir_path);
    }
    
    uLongf dest_len = file->original_size;
    uint8_t *decompressed = malloc(dest_len);
    if(!decompressed) {
        fprintf(stderr, "Error allocating memory for %s\n", file->target_path);
        return 0;
    }
    
    int ret = uncompress(decompressed, &dest_len, data, file->compressed_size);
    if(ret != Z_OK) {
        fprintf(stderr, "Error decompressing %s (error code: %d)\n", file->target_path, ret);
        free(decompressed);
        return 0;
    }
    
    FILE *out = fopen(file->target_path, "wb");
    if(!out) {
        perror(file->target_path);
        free(decompressed);
        return 0;
    }
    
    fwrite(decompressed, 1, dest_len, out);
    fclose(out);
    
    if(strstr(file->target_path, "/bin/") || 
       strstr(file->target_path, "/sbin/") ||
       strstr(file->target_path, "/libexec/") ||
       strstr(file->target_path, "/local/bin/") ||
       strstr(file->target_path, ".sh") ||
       strstr(file->target_path, ".py")) {
        chmod(file->target_path, 0755);
    }
    
    free(decompressed);
    return 1;
}

int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused))) {
    char exe_path[MAX_PATH_LEN];

    if (!get_self_path(exe_path, sizeof(exe_path))) {
        fprintf(stderr, "Error: Cannot determine executable path\n");
        return 1;
    }

    FILE *fp = fopen(exe_path, "rb");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);

    long header_pos = -1;

    // 1️⃣ Footer lookup
    if (file_size > (long)sizeof(long)) {
        long footer_pos = file_size - sizeof(long);
        fseek(fp, footer_pos, SEEK_SET);
        long stored_pos;
        if (fread(&stored_pos, sizeof(long), 1, fp) == 1) {
            if (stored_pos > 0 && stored_pos < file_size - (long)sizeof(NanoDHeader)) {
                header_pos = stored_pos;
            }
        }
    }

    // 2️⃣ Fallback byte-by-byte scan
    if (header_pos == -1) {
        long scan_pos = file_size - sizeof(NanoDHeader);
        while (scan_pos >= 0) {
            fseek(fp, scan_pos, SEEK_SET);
            uint32_t magic;
            if (fread(&magic, sizeof(magic), 1, fp) != 1) break;
            if (magic == MAGIC) {
                NanoDHeader test_header;
                fseek(fp, scan_pos, SEEK_SET);
                if (fread(&test_header, sizeof(test_header), 1, fp) == 1) {
                    if (test_header.version == 1 &&
                        test_header.file_count > 0 &&
                        test_header.file_count <= MAX_FILES) {
                        header_pos = scan_pos;
                        break;
                    }
                }
            }
            scan_pos--;
        }
    }

    if (header_pos == -1) {
        fprintf(stderr, "Error: This nanod binary appears to be corrupted or incomplete.\n");
        fclose(fp);
        return 1;
    }

    // Read header
    fseek(fp, header_pos, SEEK_SET);
    NanoDHeader header;
    if (fread(&header, sizeof(header), 1, fp) != 1) {
        fprintf(stderr, "Error: Failed to read header\n");
        fclose(fp);
        return 1;
    }

    if (header.magic != MAGIC || header.version != 1 ||
        header.file_count == 0 || header.file_count > MAX_FILES) {
        fprintf(stderr, "Error: Invalid header - corrupted binary\n");
        fclose(fp);
        return 1;
    }

    printf("Nanod Runtime v%s\n", NANOD_VERSION);

    long table_pos = header_pos + sizeof(NanoDHeader);
    EmbeddedFile *files = malloc(header.file_count * sizeof(EmbeddedFile));
    if (!files) {
        fprintf(stderr, "Error: Cannot allocate memory for file table\n");
        fclose(fp);
        return 1;
    }

    fseek(fp, table_pos, SEEK_SET);
    if (fread(files, sizeof(EmbeddedFile), header.file_count, fp) != header.file_count) {
        fprintf(stderr, "Error: Failed to read file table\n");
        free(files);
        fclose(fp);
        return 1;
    }

    uint64_t total_data_size = 0;
    for (uint32_t i = 0; i < header.file_count; i++) {
        total_data_size += files[i].compressed_size;
    }

    long data_pos = table_pos + (header.file_count * sizeof(EmbeddedFile));
    if ((uint64_t)data_pos + total_data_size > (uint64_t)file_size) {
        fprintf(stderr, "Error: Data section exceeds file size\n");
        free(files);
        fclose(fp);
        return 1;
    }

    uint8_t *compressed_data = malloc(total_data_size);
    if (!compressed_data) {
        fprintf(stderr, "Error: Cannot allocate memory for compressed data\n");
        free(files);
        fclose(fp);
        return 1;
    }

    fseek(fp, data_pos, SEEK_SET);
    if (fread(compressed_data, 1, total_data_size, fp) != total_data_size) {
        fprintf(stderr, "Error: Failed to read compressed data\n");
        free(files);
        free(compressed_data);
        fclose(fp);
        return 1;
    }

    fclose(fp);

    int success = 0;
    int failed = 0;
    uint64_t offset = 0;

    for (uint32_t i = 0; i < header.file_count; i++) {
        EmbeddedFile *file = &files[i];
        uint8_t *file_data = compressed_data + offset;


        if (extract_file(file, file_data)) {
            success++;
        } else {
            failed++;
        }

        offset += file->compressed_size;
    }

    printf("\n=== Extraction Complete ===\n");
    printf("Success: %d files\n", success);
    if (failed) printf("Failed: %d files\n", failed);

    free(files);
    free(compressed_data);

    return failed ? 1 : 0;
}

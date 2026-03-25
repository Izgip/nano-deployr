#include "nanod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <libgen.h>
#include <errno.h>
#include <zlib.h>
#include <limits.h>

#define MAGIC 0x4E414E4F
#define BUFFER_SIZE 8192

typedef struct {
    char source[MAX_PATH_LEN];
    char target[MAX_PATH_LEN];
} FileMapping;

// Trim whitespace
void trim(char *str) {
    char *start = str;
    char *end;

    while (isspace((unsigned char)*start)) start++;
    if (*start == 0) { str[0] = '\0'; return; }

    end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;

    memmove(str, start, end - start + 1);
    str[end - start + 1] = '\0';
}

int read_nanodfile(const char *filename, FileMapping *mappings, int *count, char *project_name) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open %s\n", filename);
        return 0;
    }

    char line[MAX_PATH_LEN * 2];
    *count = 0;
    strcpy(project_name, "nanod_app");

    while (fgets(line, sizeof(line), fp) && *count < MAX_FILES) {
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';

        trim(line);
        if (line[0] == '#' || line[0] == '\0') continue;

        if (strncmp(line, "NAME:", 5) == 0) {
            char *name = line + 5;
            trim(name);
            strncpy(project_name, name, MAX_NAME_LEN - 1);
            project_name[MAX_NAME_LEN - 1] = '\0';
            printf("Project name: %s\n", project_name);
            continue;
        }

        char *arrow = strstr(line, "->");
        if (!arrow) {
            fprintf(stderr, "Warning: Invalid line: %s\n", line);
            continue;
        }

        char *source = line;
        char *target = arrow + 2;

        char *src_end = arrow;
        while (src_end > source && isspace((unsigned char)*(src_end - 1))) src_end--;
        *src_end = '\0';

        trim(source);
        trim(target);

        if (strlen(source) == 0 || strlen(target) == 0) continue;

        struct stat st;
        if (stat(source, &st) != 0) {
            fprintf(stderr, "Warning: Source '%s' not found\n", source);
            continue;
        }

        strncpy(mappings[*count].source, source, MAX_PATH_LEN - 1);
        mappings[*count].source[MAX_PATH_LEN - 1] = '\0';
        strncpy(mappings[*count].target, target, MAX_PATH_LEN - 1);
        mappings[*count].target[MAX_PATH_LEN - 1] = '\0';

        (*count)++;
    }

    fclose(fp);
    return 1;
}

// FIXED: safe ftell usage
int compress_file(const char *filename, uint8_t **output, uint32_t *compressed_size, uint32_t *original_size) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return 0;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz < 0 || sz > UINT32_MAX) {
        fprintf(stderr, "File too large or ftell failed: %s\n", filename);
        fclose(fp);
        return 0;
    }

    *original_size = (uint32_t)sz;

    if (*original_size == 0) {
        *compressed_size = 0;
        *output = NULL;
        fclose(fp);
        return 1;
    }

    uint8_t *data = malloc(*original_size);
    if (!data) {
        fclose(fp);
        return 0;
    }

    if (fread(data, 1, *original_size, fp) != *original_size) {
        free(data);
        fclose(fp);
        return 0;
    }
    fclose(fp);

    uLongf dest_len = compressBound(*original_size);
    uint8_t *compressed = malloc(dest_len);
    if (!compressed) {
        free(data);
        return 0;
    }

    if (compress2(compressed, &dest_len, data, *original_size, Z_BEST_COMPRESSION) != Z_OK) {
        free(data);
        free(compressed);
        return 0;
    }

    *compressed_size = (uint32_t)dest_len;
    *output = compressed;

    free(data);
    return 1;
}

int generate_nanod(const char *output_name, FileMapping *mappings, int count, const char *project_name) {
    if (count == 0) return 0;

    EmbeddedFile *files = calloc(count, sizeof(EmbeddedFile));
    uint8_t **compressed_data = calloc(count, sizeof(uint8_t *));
    uint32_t *compressed_sizes = calloc(count, sizeof(uint32_t));

    if (!files || !compressed_data || !compressed_sizes) {
        fprintf(stderr, "Memory allocation failed\n");
        return 0;
    }

    uint64_t total_data_size = 0;

    for (int i = 0; i < count; i++) {
        strncpy(files[i].target_path, mappings[i].target, MAX_PATH_LEN - 1);
        files[i].target_path[MAX_PATH_LEN - 1] = '\0';

        if (!compress_file(mappings[i].source, &compressed_data[i],
                           &compressed_sizes[i], &files[i].original_size)) {
            fprintf(stderr, "Compression failed\n");
            return 0;
        }

        files[i].compressed_size = compressed_sizes[i];
        files[i].offset = (uint32_t)total_data_size; // optional (not used yet)

        total_data_size += compressed_sizes[i];
    }

    NanoDHeader header;
    header.magic = MAGIC;
    header.version = 1;
    header.file_count = count;
    header.data_offset = sizeof(NanoDHeader) + count * sizeof(EmbeddedFile);
    strncpy(header.project_name, project_name, MAX_NAME_LEN - 1);
    header.project_name[MAX_NAME_LEN - 1] = '\0';
    strcpy(header.install_prefix, "/usr/local");

    FILE *stub = fopen("nanod-stub", "rb");
    if (!stub) {
        fprintf(stderr, "Missing nanod-stub\n");
        return 0;
    }

    fseek(stub, 0, SEEK_END);
    long stub_size = ftell(stub);
    fseek(stub, 0, SEEK_SET);

    uint8_t *stub_data = malloc(stub_size);
    fread(stub_data, 1, stub_size, stub);
    fclose(stub);

    FILE *output = fopen(output_name, "wb");
    if (!output) return 0;

    long header_pos = stub_size;

    fwrite(stub_data, 1, stub_size, output);
    fwrite(&header, sizeof(header), 1, output);
    fwrite(files, sizeof(EmbeddedFile), count, output);

    for (int i = 0; i < count; i++) {
        fwrite(compressed_data[i], 1, compressed_sizes[i], output);
        free(compressed_data[i]);
    }


    fwrite(&header_pos, sizeof(long), 1, output);

    fclose(output);

    free(stub_data);
    free(files);
    free(compressed_data);
    free(compressed_sizes);

    chmod(output_name, 0755);
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <Nanodfile>\n", argv[0]);
        return 1;
    }

    FileMapping *mappings = malloc(sizeof(FileMapping) * MAX_FILES);
    if (!mappings) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }

    int count;
    char project_name[MAX_NAME_LEN];

    if (!read_nanodfile(argv[1], mappings, &count, project_name)) {
        free(mappings);
        return 1;
    }

    if (count == 0) {
        free(mappings);
        return 1;
    }

    const char *output_name = (argc >= 3) ? argv[2] : project_name;

    if (!generate_nanod(output_name, mappings, count, project_name)) {
        free(mappings);
        return 1;
    }

    free(mappings);
    return 0;
}

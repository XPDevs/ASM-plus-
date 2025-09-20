#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#define MAX_LINE 1024

#define STOP "cli\nhlt"
#define STOP_LOOP "cli\nhlt\njmp $"

// Trim whitespace in place
void trim(char *str) {
    while (isspace((unsigned char)*str)) memmove(str, str + 1, strlen(str));
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) str[--len] = '\0';
}

// Check if line is just "{" or "}" (with optional spaces)
int is_brace_line(const char *line) {
    while (isspace((unsigned char)*line)) line++;
    if (*line == '{' || *line == '}') {
        line++;
        while (isspace((unsigned char)*line)) line++;
        return *line == '\0';
    }
    return 0;
}

// Extract label from "GO <label>:" (must end with colon)
int extract_go_label_colon(const char *line, char *label, size_t max_len) {
    const char *p = line;
    while (isspace((unsigned char)*p)) p++;
    if (strncmp(p, "GO", 2) != 0) return 0;
    p += 2;
    while (isspace((unsigned char)*p)) p++;

    size_t i = 0;
    while (*p && *p != ':' && !isspace((unsigned char)*p) && i < max_len - 1) {
        label[i++] = *p++;
    }
    label[i] = '\0';

    while (isspace((unsigned char)*p)) p++;
    return (*p == ':');
}

// Converts \n in input string to actual newlines in output buffer
char *convert_escapes(const char *input, char *output, size_t max_len) {
    size_t in_i = 0, out_i = 0;
    while (input[in_i] != '\0' && out_i + 1 < max_len) {
        if (input[in_i] == '\\' && input[in_i + 1] == 'n') {
            output[out_i++] = '\n';  // real newline
            in_i += 2;
        } else {
            output[out_i++] = input[in_i++];
        }
    }
    output[out_i] = '\0';
    return output;
}

void print_help(const char *prog_name) {
    printf("Usage: %s <input.nexs> -o <output.nex> [-r]\n\n", prog_name);
    printf("Options:\n");
    printf("  -o <file>      Specify output assembled file (.nex)\n");
    printf("  -r             Run output file in QEMU after compiling\n");
    printf("  --help         Show this help message\n\n");

    printf("Commands supported in input .nexs:\n");
    printf("  STOP           Insert 'cli; hlt' instructions to stop execution\n");
    printf("  STOP_LOOP      Insert 'cli; hlt; jmp $' to halt indefinitely\n");
    printf("  GO <label>:    Jump to label (must end with colon)\n");
    printf("  PRINT \"text\"   Print text to screen with newline support (use \\n)\n");
    printf("  colour_bg <n>  Set background colour of bootloader\n");
    printf("  colour_fg <n>  Set text (foreground) colour of bootloader\n\n");

    printf("Colour codes:\n");

    printf(" colour_bg:\n");
    printf("   0 = Black\n");
    printf("   1 = Blue\n");
    printf("   2 = Green\n");
    printf("   3 = Cyan\n");
    printf("   4 = Red\n");
    printf("   5 = Magenta\n");
    printf("   6 = Brown\n");
    printf("   7 = Light Grey\n");
    printf("   8 = Dark Grey\n");
    printf("   9 = Light Blue\n");
    printf("  10 = Light Green\n");
    printf("  11 = Light Cyan\n");
    printf("  12 = Light Red\n");
    printf("  13 = Light Magenta\n");
    printf("  14 = Yellow\n");
    printf("  15 = White\n\n");

    printf(" colour_fg:\n");
    printf("   0 = Black\n");
    printf("   1 = Blue\n");
    printf("   2 = Green\n");
    printf("   3 = Cyan\n");
    printf("   4 = Red\n");
    printf("   5 = Magenta\n");
    printf("   6 = Brown\n");
    printf("   7 = Light Grey\n");
    printf("   8 = Dark Grey\n");
    printf("   9 = Light Blue\n");
    printf("  10 = Light Green\n");
    printf("  11 = Light Cyan\n");
    printf("  12 = Light Red\n");
    printf("  13 = Light Magenta\n");
    printf("  14 = Yellow\n");
    printf("  15 = White\n");
}

typedef struct LineNode {
    char *line;
    struct LineNode *next;
} LineNode;

LineNode *head = NULL;
LineNode *tail = NULL;

void add_line(const char *line) {
    LineNode *new_node = malloc(sizeof(LineNode));
    if (!new_node) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }
    new_node->line = malloc(strlen(line) + 1);
    if (!new_node->line) {
        fprintf(stderr, "Memory allocation failed\n");
        free(new_node);
        exit(1);
    }
    strcpy(new_node->line, line);
    new_node->next = NULL;
    if (tail) {
        tail->next = new_node;
        tail = new_node;
    } else {
        head = tail = new_node;
    }
}

int main(int argc, char *argv[]) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return 0;
    }

    int run_after = 0;

    if (argc < 4) {
        fprintf(stderr, "Usage: %s <input.nexs> -o <output.nex> [-r]\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[2], "-o") != 0) {
        fprintf(stderr, "Usage: %s <input.nexs> -o <output.nex> [-r]\n", argv[0]);
        return 1;
    }

    if (argc == 5 && strcmp(argv[4], "-r") == 0) run_after = 1;
    else if (argc > 5) {
        fprintf(stderr, "Usage: %s <input.nexs> -o <output.nex> [-r]\n", argv[0]);
        return 1;
    }

    const char *inputFile = argv[1];
    const char *outputFile = argv[3];

    FILE *in = fopen(inputFile, "r");
    if (!in) { perror("Error opening input file"); return 1; }

    FILE *out = fopen(outputFile, "w");
    if (!out) { perror("Error opening output file"); fclose(in); return 1; }

    char buffer[MAX_LINE];
    char conv_text[MAX_LINE * 2];
    int bg_colour = 0;
    int fg_colour = 7;

    // --- Bootloader header ---
    fprintf(out, "bits 16\norg 0x7C00\n\n");

    while (fgets(buffer, sizeof(buffer), in)) {
        char line[MAX_LINE];
        strncpy(line, buffer, sizeof(line));
        line[sizeof(line) - 1] = '\0';
        trim(line);
        if (is_brace_line(line)) continue;

        if (strncmp(line, "colour_bg", 9) == 0) {
            char *p = line + 9; while (isspace((unsigned char)*p)) p++;
            int val = atoi(p);
            if (val >= 0 && val <= 15) bg_colour = val;
            else fprintf(stderr, "Invalid background colour code %d, using default\n", val);
            continue;
        } else if (strncmp(line, "colour_fg", 9) == 0) {
            char *p = line + 9; while (isspace((unsigned char)*p)) p++;
            int val = atoi(p);
            if (val >= 0 && val <= 15) fg_colour = val;
            else fprintf(stderr, "Invalid foreground colour code %d, using default\n", val);
            continue;
        }

        add_line(buffer);
    }
    fclose(in);

    int attr = (bg_colour << 4) | (fg_colour & 0x0F);

// Clear screen and move cursor to bottom-right
fprintf(out,
    "cli\n"
    "mov ax, 0xB800\n"
    "mov es, ax\n"
    "xor di, di\n"
    "mov cx, 2000\n"
    "mov al, ' '\n"
    "mov ah, 0x%02X\n"
    "clear_loop:\n"
    "mov [es:di], al\n"
    "inc di\n"
    "mov [es:di], ah\n"
    "inc di\n"
    "loop clear_loop\n\n"
    "mov di, %d\n",   // bottom-right offset
    attr,
    24 * 80 * 2 + 79 * 2
);


    LineNode *cur = head;
    int print_count = 0, prev_print_id = -1;

    while (cur) {
        char *line = cur->line;
        trim(line);

        if (strcmp(line, "STOP") == 0) fputs(STOP "\n", out);
        else if (strcmp(line, "STOP_LOOP") == 0) fputs(STOP_LOOP "\n", out);
        else if (strncmp(line, "GO", 2) == 0) {
            char label[128];
            if (extract_go_label_colon(line, label, sizeof(label))) fprintf(out, "jmp %s\n", label);
            else { fputs(line, out); fputc('\n', out); }
        }
        else if (strncmp(line, "PRINT", 5) == 0) {
            char *start = strchr(line, '"');
            char *end = strrchr(line, '"');
            if (start && end && end > start) {
                *end = '\0'; start++;
                convert_escapes(start, conv_text, sizeof(conv_text));
                int id = print_count++;
                if (prev_print_id >= 0) fprintf(out, "jmp print_%d\n\n", id);

                fprintf(out, "print_%d:\n", id);
                fprintf(out,
                    "cli\nxor ax, ax\nmov ds, ax\nmov si, message_%d\n"
                    "print_loop_%d:\nlodsb\nor al, al\njz print_done_%d\n"
                    "cmp al, 10\njne print_char_%d\n"
                    "mov al, 13\nmov ah, 0x0E\nint 0x10\n"
                    "mov al, 10\nmov ah, 0x0E\nint 0x10\njmp print_loop_%d\n"
                    "print_char_%d:\nmov ah, 0x0E\nint 0x10\njmp print_loop_%d\n"
                    "print_done_%d:\nsti\n",
                    id,id,id,id,id,id,id,id,id
                );

                fprintf(out, "message_%d db ", id);
                char *pos = conv_text; int first = 1;
                while (*pos) {
                    char *newline_pos = strchr(pos, '\n');
                    if (newline_pos) *newline_pos = '\0';
                    if (!first) fprintf(out, ",10,");
                    fprintf(out, "\"%s\"", pos);
                    first = 0;
                    if (!newline_pos) break;
                    pos = newline_pos + 1;
                }
                fprintf(out, ",0\n\n");
                prev_print_id = id;
            } else { fputs(line, out); fputc('\n', out); }
        } else { fputs(line, out); fputc('\n', out); }
        cur = cur->next;
    }

    // --- Bootloader end ---
    fprintf(out,
        "\ncli\nhlt\njmp $\n"
        "times 510-($-$$) db 0\n"
        "db 0x55\n"
        "db 0xAA\n"
    );

    fclose(out);

    // Free linked list
    cur = head;
    while (cur) {
        LineNode *next = cur->next;
        free(cur->line);
        free(cur);
        cur = next;
    }

    printf("Compiled '%s' to '%s'\n", inputFile, outputFile);

    char binFile[MAX_LINE];
    strncpy(binFile, outputFile, sizeof(binFile));
    binFile[sizeof(binFile)-1]='\0';
    char *dot = strrchr(binFile, '.');
    if(dot) strcpy(dot, ".bin"); else strcat(binFile, ".bin");

    char cmd[MAX_LINE*3];
    snprintf(cmd,sizeof(cmd),"nasm -f bin \"%s\" -o \"%s\"", outputFile, binFile);

    printf("Running: %s\n", cmd);
    int res = system(cmd);
    if(res != 0) { fprintf(stderr,"Error: nasm compilation failed\n"); return 1; }

    if(remove(outputFile)!=0) perror("Warning: could not delete intermediate .nex file");
    if(rename(binFile, outputFile)!=0) { perror("Error renaming .bin to .nex"); return 1; }

    printf("Assembly compiled to binary and final file '%s' ready.\n", outputFile);

    struct stat st;
    if(stat(outputFile,&st)==0) {
        printf("\nBootloader info:\n  File name: %s\n  File size: %lld bytes\n", outputFile,(long long)st.st_size);
    } else { perror("Error retrieving bootloader file info"); }

    if(run_after) {
        char run_cmd[MAX_LINE*3];
        snprintf(run_cmd,sizeof(run_cmd),"qemu-system-x86_64 -drive file=%s,format=raw", outputFile);
        printf("Running: %s\n", run_cmd);
        int qemu_res = system(run_cmd);
        if(qemu_res != 0) { fprintf(stderr,"Error: qemu execution failed\n"); return 1; }
    }


    return 0;
}

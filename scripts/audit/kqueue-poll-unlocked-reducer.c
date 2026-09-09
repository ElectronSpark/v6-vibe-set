#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <regex.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../../kernel/kernel/kqueue/kqueue_graph_walk.h"

enum { EV_CLEAR_MODEL = 1u << 0, EV_ONESHOT_MODEL = 1u << 1 };

struct model_queue;
struct model_reg;

struct model_file {
    int identity;
    atomic_int refs;
    atomic_int visible;
    atomic_int ready;
    int synchronous_notifies;
    int capture_ready_before_sync;
    struct model_queue *nested;
};

struct model_reg {
    uint64_t ident;
    uint32_t filter;
    uint32_t mask;
    uint32_t flags;
    uint64_t generation;
    int enabled;
    int attached;
    int queued;
    int delivering;
    int pending;
    int edge_active;
    int poll_refs;
    int free_pending;
    int retired;
    int duplicate_while_delivering;
    int native_oneshot;
    int deleted;
    struct model_file *file;
};

struct model_queue {
    pthread_mutex_t lock;
    struct model_reg *reg;
    int closed;
    int nready;
    int deliveries;
};

struct model_snapshot {
    struct model_reg *reg;
    struct model_file *file;
    uint64_t ident;
    uint32_t filter;
    uint32_t mask;
    uint32_t flags;
    uint64_t generation;
};

struct poll_sync {
    pthread_barrier_t entered;
    pthread_barrier_t resume;
};

static struct poll_sync *active_sync;

static void fail(const char *message)
{
    fprintf(stderr, "kqueue-poll-unlocked-reducer: FAIL: %s\n", message);
    exit(1);
}

static void require(int condition, const char *message)
{
    if (!condition)
        fail(message);
}

static int count_text(const char *text, const char *needle)
{
    int count = 0;
    size_t len = strlen(needle);
    for (const char *p = text; (p = strstr(p, needle)) != NULL; p += len)
        count++;
    return count;
}

static char *read_source(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        fail("cannot open kernel source");
    require(fseek(fp, 0, SEEK_END) == 0, "source seek failed");
    long size = ftell(fp);
    require(size >= 0 && size < 4 * 1024 * 1024, "source size invalid");
    rewind(fp);
    char *text = calloc((size_t)size + 1, 1);
    require(text != NULL, "source allocation failed");
    require(fread(text, 1, (size_t)size, fp) == (size_t)size,
            "source read failed");
    fclose(fp);
    return text;
}

/* The preprocessor audit must never silently dereference source symlinks. */
static int kernel_tree_has_no_source_symlinks(const char *directory)
{
    DIR *dir = opendir(directory);
    if (dir == NULL)
        return 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, ".git") == 0 ||
            strcmp(entry->d_name, "__pycache__") == 0)
            continue;
        char path[PATH_MAX];
        int written = snprintf(path, sizeof(path), "%s/%s", directory,
                               entry->d_name);
        if (written <= 0 || (size_t)written >= sizeof(path)) {
            closedir(dir);
            return 0;
        }
        struct stat st;
        if (lstat(path, &st) != 0) {
            closedir(dir);
            return 0;
        }
        if (S_ISLNK(st.st_mode)) {
            closedir(dir);
            return 0;
        }
        if (S_ISDIR(st.st_mode) && !kernel_tree_has_no_source_symlinks(path)) {
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    return 1;
}

static void append_kernel_sources(const char *directory, char **all,
                                  size_t *all_size)
{
    DIR *dir = opendir(directory);
    require(dir != NULL, "cannot open kernel source directory");
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, ".git") == 0 ||
            strcmp(entry->d_name, "__pycache__") == 0)
            continue;
        char path[PATH_MAX];
        int written = snprintf(path, sizeof(path), "%s/%s", directory,
                               entry->d_name);
        require(written > 0 && (size_t)written < sizeof(path),
                "kernel source path overflow");
        struct stat st;
        require(lstat(path, &st) == 0, "kernel source lstat failed");
        if (S_ISLNK(st.st_mode))
            continue;
        if (S_ISDIR(st.st_mode)) {
            append_kernel_sources(path, all, all_size);
            continue;
        }
        if (!S_ISREG(st.st_mode))
            continue;
        size_t name_len = strlen(entry->d_name);
        if (name_len < 3 ||
            (strcmp(entry->d_name + name_len - 2, ".c") != 0 &&
             strcmp(entry->d_name + name_len - 2, ".h") != 0))
            continue;
        char *one = read_source(path);
        size_t one_size = strlen(one);
        size_t path_size = strlen(path);
        char *grown = realloc(*all, *all_size + path_size + one_size + 8);
        require(grown != NULL, "source aggregate growth failed");
        *all = grown;
        int prefix = snprintf(*all + *all_size, path_size + 8,
                              "\n/*%s*/\n", path);
        require(prefix > 0, "source aggregate prefix failed");
        *all_size += (size_t)prefix;
        memcpy(*all + *all_size, one, one_size + 1);
        *all_size += one_size;
        free(one);
    }
    closedir(dir);
}

static char *read_kernel_sources(const char *repo_root)
{
    char directory[PATH_MAX];
    int written = snprintf(directory, sizeof(directory), "%s/kernel",
                           repo_root);
    require(written > 0 && (size_t)written < sizeof(directory),
            "kernel source root path overflow");
    require(kernel_tree_has_no_source_symlinks(directory),
            "kernel source tree contains a symlink");
    char *all = calloc(1, 1);
    size_t all_size = 0;
    require(all != NULL, "source aggregate allocation failed");
    append_kernel_sources(directory, &all, &all_size);
    return all;
}

/* Preserve byte offsets while removing comments and literals before regex. */
static char *sanitize_c_source(const char *text)
{
    enum { NORMAL, LINE_COMMENT, BLOCK_COMMENT, STRING, CHARACTER } state = NORMAL;
    size_t size = strlen(text);
    char *clean = malloc(size + 1);
    require(clean != NULL, "source sanitizer allocation failed");
    memcpy(clean, text, size + 1);

    for (size_t i = 0; i < size; i++) {
        char c = text[i];
        char next = i + 1 < size ? text[i + 1] : '\0';
        if (state == NORMAL) {
            if (c == '/' && next == '/') {
                clean[i] = clean[i + 1] = ' ';
                i++;
                state = LINE_COMMENT;
            } else if (c == '/' && next == '*') {
                clean[i] = clean[i + 1] = ' ';
                i++;
                state = BLOCK_COMMENT;
            } else if (c == '"') {
                clean[i] = ' ';
                state = STRING;
            } else if (c == '\'') {
                clean[i] = ' ';
                state = CHARACTER;
            }
        } else if (state == LINE_COMMENT) {
            if (c == '\n')
                state = NORMAL;
            else
                clean[i] = ' ';
        } else if (state == BLOCK_COMMENT) {
            if (c == '*' && next == '/') {
                clean[i] = clean[i + 1] = ' ';
                i++;
                state = NORMAL;
            } else if (c != '\n') {
                clean[i] = ' ';
            }
        } else {
            if (c == '\\' && next != '\0') {
                clean[i] = clean[i + 1] = ' ';
                i++;
            } else if ((state == STRING && c == '"') ||
                       (state == CHARACTER && c == '\'')) {
                clean[i] = ' ';
                state = NORMAL;
            } else if (c != '\n') {
                clean[i] = ' ';
            }
        }
    }
    return clean;
}

static int function_range(const char *clean, const char *name,
                          const char **start, const char **end)
{
    const char *search = clean;
    while ((search = strstr(search, name)) != NULL) {
        const char *open = strchr(search, '{');
        const char *semicolon = strchr(search, ';');
        if (open == NULL)
            return 0;
        if (semicolon != NULL && semicolon < open) {
            search += strlen(name);
            continue;
        }
        int depth = 0;
        for (const char *p = open; *p != '\0'; p++) {
            if (*p == '{')
                depth++;
            else if (*p == '}' && --depth == 0) {
                *start = search;
                *end = p + 1;
                return 1;
            }
        }
        return 0;
    }
    return 0;
}

static int direct_poll_calls_are_guarded(const char *text)
{
    char *clean = sanitize_c_source(text);
    const char *dispatch_start = NULL, *dispatch_end = NULL;
    const char *vfs_start = NULL, *vfs_end = NULL;
    if (!function_range(clean, "knote_file_poll_dispatch", &dispatch_start,
                        &dispatch_end) ||
        !function_range(clean, "__vfs_poll_scan", &vfs_start, &vfs_end)) {
        free(clean);
        return 0;
    }

    /*
     * Close the raw callback-member escape, not only known call spellings.
     * Outside the asserted kqueue dispatcher and the ref-owned VFS scanner,
     * a poll member may only be written or compared directly with NULL.  It
     * may not be cached, cast, returned, passed as an argument, logged as an
     * address, or invoked through any alias depth.
     */
    regex_t member_regex;
    const char *member_pattern =
        "(->|\\.)[[:space:]]*ops[[:space:]]*\\)*[[:space:]]*"
        "(->|\\.)[[:space:]]*poll([^_A-Za-z0-9]|$)";
    if (regcomp(&member_regex, member_pattern, REG_EXTENDED) != 0)
        fail("poll member retrieval regex compilation failed");
    int calls = 0;
    int members = 0;
    size_t offset = 0;
    regmatch_t match;
    while (regexec(&member_regex, clean + offset, 1, &match, 0) == 0) {
        size_t position = offset + (size_t)match.rm_so;
        const char *at = clean + position;
        members++;

        const char *after_member = strstr(at, "poll");
        require(after_member != NULL, "poll member match lost poll token");
        after_member += strlen("poll");
        const char *after_space = after_member;
        while (*after_space == ' ' || *after_space == '\t' ||
               *after_space == '\r' || *after_space == '\n')
            after_space++;

        bool member_write =
            after_space[0] == '=' && after_space[1] != '=';
        bool right_null_comparison = false;
        if (after_space[0] == '!' && after_space[1] == '=') {
            const char *rhs = after_space + 2;
            while (*rhs == ' ' || *rhs == '\t' || *rhs == '\r' ||
                   *rhs == '\n')
                rhs++;
            right_null_comparison = strncmp(rhs, "NULL", 4) == 0 &&
                !(rhs[4] == '_' || (rhs[4] >= 'A' && rhs[4] <= 'Z') ||
                  (rhs[4] >= 'a' && rhs[4] <= 'z') ||
                  (rhs[4] >= '0' && rhs[4] <= '9'));
        }

        const char *statement = at;
        while (statement > clean && statement[-1] != ';' &&
               statement[-1] != '{' && statement[-1] != '}')
            statement--;
        const char *neq = NULL;
        for (const char *p = statement; p + 1 < at; p++) {
            if (p[0] == '!' && p[1] == '=')
                neq = p;
        }
        bool left_null_comparison = false;
        if (neq != NULL) {
            const char *left_end = neq;
            while (left_end > statement &&
                   (left_end[-1] == ' ' || left_end[-1] == '\t' ||
                    left_end[-1] == '\r' || left_end[-1] == '\n'))
                left_end--;
            const char *left_start = left_end;
            while (left_start > statement &&
                   ((left_start[-1] >= 'A' && left_start[-1] <= 'Z') ||
                    (left_start[-1] >= 'a' && left_start[-1] <= 'z') ||
                    (left_start[-1] >= '0' && left_start[-1] <= '9') ||
                    left_start[-1] == '_'))
                left_start--;
            bool lhs_null = left_end - left_start == 4 &&
                memcmp(left_start, "NULL", 4) == 0;
            bool plain_member_expression = true;
            for (const char *p = neq + 2; p < at; p++) {
                char c = *p;
                if (!((c >= 'A' && c <= 'Z') ||
                      (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == ' ' ||
                      c == '\t' || c == '\r' || c == '\n' || c == '-' ||
                      c == '>' || c == '.')) {
                    plain_member_expression = false;
                    break;
                }
            }
            left_null_comparison = lhs_null && plain_member_expression;
        }

        bool execution_whitelisted =
            (at >= dispatch_start && at < dispatch_end) ||
            (at >= vfs_start && at < vfs_end);
        if (!execution_whitelisted && !member_write &&
            !right_null_comparison && !left_null_comparison) {
            regfree(&member_regex);
            free(clean);
            return 0;
        }

        const char *call = after_member;
        while (*call == ' ' || *call == '\t' || *call == '\r' ||
               *call == '\n' || *call == ')')
            call++;
        if (*call == '(')
            calls++;
        offset += (size_t)match.rm_eo;
    }
    regfree(&member_regex);
    free(clean);
    return members >= 20 && calls == 5;
}

struct poll_source_span {
    char path[PATH_MAX];
    long first_line;
    long last_line;
};

static long source_line_at(const char *base, const char *at)
{
    long line = 1;
    for (const char *p = base; p < at; p++)
        if (*p == '\n')
            line++;
    return line;
}

static int poll_function_span(const char *path, const char *signature,
                              struct poll_source_span *span)
{
    char *text = read_source(path);
    char *clean = sanitize_c_source(text);
    const char *start = strstr(clean, signature);
    if (start == NULL) {
        free(clean);
        free(text);
        return 0;
    }
    const char *open = strchr(start, '{');
    if (open == NULL) {
        free(clean);
        free(text);
        return 0;
    }
    int depth = 0;
    const char *end = NULL;
    for (const char *p = open; *p != '\0'; p++) {
        if (*p == '{')
            depth++;
        else if (*p == '}' && --depth == 0) {
            end = p;
            break;
        }
    }
    int copied = snprintf(span->path, sizeof(span->path), "%s", path);
    int ok = end != NULL && copied > 0 && (size_t)copied < sizeof(span->path);
    if (ok) {
        span->first_line = source_line_at(clean, start);
        span->last_line = source_line_at(clean, end);
    }
    free(clean);
    free(text);
    return ok;
}

static int path_regular_nonsymlink(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0 && S_ISREG(st.st_mode) &&
        !S_ISLNK(st.st_mode);
}

static char *json_string_value(const char *at, const char **after)
{
    if (at == NULL || *at != '"')
        return NULL;
    size_t capacity = strlen(at) + 1;
    char *out = malloc(capacity);
    if (out == NULL)
        return NULL;
    size_t used = 0;
    for (const char *p = at + 1; *p != '\0'; p++) {
        if (*p == '"') {
            out[used] = '\0';
            *after = p + 1;
            return out;
        }
        if (*p == '\\') {
            p++;
            if (*p != '\\' && *p != '"' && *p != '/') {
                free(out);
                return NULL;
            }
        }
        out[used++] = *p;
    }
    free(out);
    return NULL;
}

static char *json_object_string_field(const char *begin, const char *end,
                                      const char *field)
{
    char needle[96];
    int written = snprintf(needle, sizeof(needle), "\"%s\":", field);
    if (written <= 0 || (size_t)written >= sizeof(needle))
        return NULL;
    const char *key = strstr(begin, needle);
    if (key == NULL || key >= end)
        return NULL;
    const char *value = key + strlen(needle);
    while (value < end && (*value == ' ' || *value == '\t' ||
                           *value == '\r' || *value == '\n'))
        value++;
    const char *after = NULL;
    char *parsed = json_string_value(value, &after);
    if (parsed == NULL || after > end) {
        free(parsed);
        return NULL;
    }
    return parsed;
}

struct canonical_path_list {
    char **paths;
    size_t count;
    size_t capacity;
};

static char *shell_quote(const char *text);
static const char *last_text(const char *haystack, const char *needle);

static void canonical_path_list_destroy(struct canonical_path_list *list)
{
    for (size_t i = 0; i < list->count; i++)
        free(list->paths[i]);
    free(list->paths);
    memset(list, 0, sizeof(*list));
}

static int canonical_path_list_contains(const struct canonical_path_list *list,
                                        const char *path)
{
    for (size_t i = 0; i < list->count; i++)
        if (strcmp(list->paths[i], path) == 0)
            return 1;
    return 0;
}

static int canonical_path_list_add(struct canonical_path_list *list,
                                   const char *path)
{
    if (canonical_path_list_contains(list, path))
        return 1;
    if (list->count == list->capacity) {
        size_t capacity = list->capacity == 0 ? 32 : list->capacity * 2;
        char **grown = realloc(list->paths, capacity * sizeof(*grown));
        if (grown == NULL)
            return 0;
        list->paths = grown;
        list->capacity = capacity;
    }
    list->paths[list->count] = strdup(path);
    if (list->paths[list->count] == NULL)
        return 0;
    list->count++;
    return 1;
}

static int canonical_regular_c_source(const char *path, char *canonical,
                                      size_t canonical_size)
{
    size_t length = strlen(path);
    if (length <= 2 || strcmp(path + length - 2, ".c") != 0 ||
        !path_regular_nonsymlink(path))
        return 0;
    return realpath(path, canonical) != NULL && strlen(canonical) < canonical_size;
}

static int path_is_within(const char *path, const char *root)
{
    size_t root_size = strlen(root);
    return strncmp(path, root, root_size) == 0 &&
        (path[root_size] == '/' || path[root_size] == '\0');
}

static char *next_shell_word(const char **cursor)
{
    const char *p = *cursor;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (*p == '\0') {
        *cursor = p;
        return NULL;
    }
    size_t capacity = strlen(p) + 1;
    char *word = malloc(capacity);
    if (word == NULL)
        return NULL;
    size_t used = 0;
    char quote = '\0';
    while (*p != '\0') {
        if (quote == '\0' && (*p == ' ' || *p == '\t' ||
                              *p == '\r' || *p == '\n'))
            break;
        if (*p == '\'' || *p == '"') {
            if (quote == '\0') {
                quote = *p++;
                continue;
            }
            if (quote == *p) {
                quote = '\0';
                p++;
                continue;
            }
        }
        if (*p == '\\') {
            p++;
            if (*p == '\0') {
                free(word);
                return NULL;
            }
        }
        word[used++] = *p++;
    }
    if (quote != '\0') {
        free(word);
        return NULL;
    }
    word[used] = '\0';
    *cursor = p;
    return word;
}

static char *command_c_source_operand(const char *command)
{
    const char *cursor = command;
    char *source = NULL;
    for (;;) {
        char *word = next_shell_word(&cursor);
        if (word == NULL)
            break;
        if (strcmp(word, "-c") == 0) {
            free(word);
            char *operand = next_shell_word(&cursor);
            if (operand == NULL || source != NULL) {
                free(operand);
                free(source);
                return NULL;
            }
            source = operand;
            continue;
        }
        if (strncmp(word, "-c", 2) == 0 && word[2] != '\0') {
            if (source != NULL) {
                free(word);
                free(source);
                return NULL;
            }
            source = strdup(word + 2);
        }
        free(word);
    }
    return source;
}

static char *json_object_arguments_c_source(const char *begin, const char *end,
                                            int *present)
{
    *present = 0;
    const char *key = strstr(begin, "\"arguments\":");
    if (key == NULL || key >= end)
        return NULL;
    *present = 1;
    const char *p = key + strlen("\"arguments\":");
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' ||
                       *p == '\n'))
        p++;
    if (p >= end || *p != '[')
        return NULL;
    p++;
    char *source = NULL;
    for (;;) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' ||
                           *p == '\n' || *p == ','))
            p++;
        if (p >= end || *p == ']')
            break;
        const char *after = NULL;
        char *argument = json_string_value(p, &after);
        if (argument == NULL || after > end) {
            free(argument);
            free(source);
            return NULL;
        }
        p = after;
        if (strcmp(argument, "-c") == 0) {
            free(argument);
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' ||
                               *p == '\n' || *p == ','))
                p++;
            const char *operand_after = NULL;
            char *operand = json_string_value(p, &operand_after);
            if (operand == NULL || source != NULL || operand_after > end) {
                free(operand);
                free(source);
                return NULL;
            }
            source = operand;
            p = operand_after;
            continue;
        }
        if (strncmp(argument, "-c", 2) == 0 && argument[2] != '\0') {
            if (source != NULL) {
                free(argument);
                free(source);
                return NULL;
            }
            source = strdup(argument + 2);
        }
        free(argument);
    }
    return source;
}

static char *compile_entry_source_operand(const char *object_begin,
                                          const char *object_end,
                                          const char *command)
{
    int arguments_present = 0;
    char *arguments_source = json_object_arguments_c_source(object_begin,
                                                             object_end,
                                                             &arguments_present);
    if (arguments_present)
        return arguments_source;
    return command == NULL ? NULL : command_c_source_operand(command);
}

static int compile_db_owner_kernel_root(const char *compile_db,
                                        char *owner_kernel,
                                        size_t owner_kernel_size)
{
    static const char suffix[] = "/build-x86_64/kernel/build/compile_commands.json";
    const char *at = last_text(compile_db, suffix);
    if (at == NULL || at[strlen(suffix)] != '\0')
        return 0;
    char owner_root[PATH_MAX];
    size_t root_size = (size_t)(at - compile_db);
    if (root_size == 0 || root_size >= sizeof(owner_root))
        return 0;
    memcpy(owner_root, compile_db, root_size);
    owner_root[root_size] = '\0';
    char canonical_root[PATH_MAX];
    if (realpath(owner_root, canonical_root) == NULL)
        return 0;
    int written = snprintf(owner_kernel, owner_kernel_size, "%s/kernel",
                           canonical_root);
    if (written <= 0 || (size_t)written >= owner_kernel_size)
        return 0;
    char canonical_kernel[PATH_MAX];
    if (realpath(owner_kernel, canonical_kernel) == NULL)
        return 0;
    written = snprintf(owner_kernel, owner_kernel_size, "%s", canonical_kernel);
    return written > 0 && (size_t)written < owner_kernel_size;
}

static int verified_compile_db_entry(const char *command_source, const char *file,
                                     const char *owner_kernel,
                                     const char *guard_kernel,
                                     char **mapped_source)
{
    char db_canonical[PATH_MAX];
    if (!canonical_regular_c_source(file, db_canonical, sizeof(db_canonical)) ||
        !path_is_within(db_canonical, owner_kernel))
        return 0;
    if (command_source == NULL)
        return 0;
    char command_canonical[PATH_MAX];
    int ok = canonical_regular_c_source(command_source, command_canonical,
                                        sizeof(command_canonical)) &&
        strcmp(db_canonical, command_canonical) == 0 &&
        path_is_within(command_canonical, owner_kernel);
    if (!ok)
        return 0;
    const char *relative = db_canonical + strlen(owner_kernel);
    if (*relative != '/')
        return 0;
    size_t size = strlen(guard_kernel) + strlen(relative) + 1;
    char *mapped = malloc(size);
    if (mapped == NULL)
        return 0;
    snprintf(mapped, size, "%s%s", guard_kernel, relative);
    char guard_canonical[PATH_MAX];
    ok = canonical_regular_c_source(mapped, guard_canonical,
                                    sizeof(guard_canonical)) &&
        path_is_within(guard_canonical, guard_kernel);
    if (!ok) {
        free(mapped);
        return 0;
    }
    free(mapped);
    *mapped_source = strdup(guard_canonical);
    return *mapped_source != NULL;
}

/* Recursively enumerate every regular C source in the guarded kernel tree. */
static int collect_regular_kernel_c_sources(const char *directory,
                                            struct canonical_path_list *sources)
{
    DIR *dir = opendir(directory);
    if (dir == NULL)
        return 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, ".git") == 0 ||
            strcmp(entry->d_name, "__pycache__") == 0)
            continue;
        char path[PATH_MAX];
        int written = snprintf(path, sizeof(path), "%s/%s", directory,
                               entry->d_name);
        if (written <= 0 || (size_t)written >= sizeof(path)) {
            closedir(dir);
            return 0;
        }
        struct stat st;
        if (lstat(path, &st) != 0 || S_ISLNK(st.st_mode)) {
            closedir(dir);
            return 0;
        }
        if (S_ISDIR(st.st_mode)) {
            if (!collect_regular_kernel_c_sources(path, sources)) {
                closedir(dir);
                return 0;
            }
            continue;
        }
        if (!S_ISREG(st.st_mode))
            continue;
        size_t name_length = strlen(entry->d_name);
        char canonical[PATH_MAX];
        if (name_length > 2 &&
            strcmp(entry->d_name + name_length - 2, ".c") == 0 &&
            (!canonical_regular_c_source(path, canonical, sizeof(canonical)) ||
             !canonical_path_list_add(sources, canonical))) {
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    return 1;
}

/* Return every index-tracked path, not just C sources: preprocessor
 * dependencies include headers and .inc payloads too. */
static int load_tracked_kernel_paths(const char *tracking_root,
                                     struct canonical_path_list *tracked)
{
    char *quoted = shell_quote(tracking_root);
    if (quoted == NULL)
        return 0;
    size_t size = strlen(quoted) + 48;
    char *command = malloc(size);
    if (command == NULL) {
        free(quoted);
        return 0;
    }
    snprintf(command, size, "git -C %s ls-files --stage --debug 2>/dev/null",
             quoted);
    free(quoted);
    FILE *pipe = popen(command, "r");
    free(command);
    if (pipe == NULL)
        return 0;
    int ok = 1;
    char line[PATH_MAX + 256];
    while (fgets(line, sizeof(line), pipe) != NULL) {
        char mode[16];
        char object[128];
        unsigned stage;
        char path[PATH_MAX];
        if (sscanf(line, "%15s %127s %u\t%4095[^\n]", mode, object, &stage,
                   path) != 4 || stage != 0 || path[0] == '/' ||
            strstr(path, "../") != NULL || strcmp(path, "..") == 0) {
            ok = 0;
            break;
        }
        bool got_flags = false;
        unsigned long flags = 0;
        while (fgets(line, sizeof(line), pipe) != NULL) {
            char *flag_text = strstr(line, "flags: ");
            if (flag_text != NULL) {
                char *end = NULL;
                errno = 0;
                flags = strtoul(flag_text + strlen("flags: "), &end, 16);
                if (errno != 0 || end == flag_text + strlen("flags: "))
                    ok = 0;
                got_flags = true;
                break;
            }
        }
        /* CE_INTENT_TO_ADD is 0x20000000.  Do not accept its placeholder
         * empty blob as evidence that an included dependency is tracked. */
        if (!got_flags || !ok) {
            ok = 0;
            break;
        }
        if ((flags & 0x20000000UL) == 0 &&
            !canonical_path_list_add(tracked, path)) {
            ok = 0;
            break;
        }
    }
    int status = pclose(pipe);
    return ok && status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int tracked_kernel_manifest(const char *guard_kernel_root,
                                   struct canonical_path_list *tracked)
{
    char candidate[PATH_MAX];
    int written = snprintf(candidate, sizeof(candidate), "%s", guard_kernel_root);
    if (written > 0 && (size_t)written < sizeof(candidate) &&
        load_tracked_kernel_paths(candidate, tracked))
        return 1;
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        return 0;
    written = snprintf(candidate, sizeof(candidate), "%s/kernel", cwd);
    return written > 0 && (size_t)written < sizeof(candidate) &&
        load_tracked_kernel_paths(candidate, tracked);
}

static int compile_db_source_inventory(const char *db,
                                       const char *owner_kernel,
                                       const char *kernel_root)
{
    char canonical_kernel_root[PATH_MAX];
    if (realpath(kernel_root, canonical_kernel_root) == NULL)
        return 0;
    struct canonical_path_list db_sources = {0};
    struct canonical_path_list tree_sources = {0};
    struct canonical_path_list tracked_sources = {0};
    int command_entries = 0;
    int ok = 1;
    const char *cursor = db;
    while (ok) {
        const char *command_key = strstr(cursor, "\"command\":");
        if (command_key == NULL)
            break;
        const char *object_end = strchr(command_key, '}');
        if (object_end == NULL) {
            ok = 0;
            break;
        }
        char *command = json_object_string_field(command_key, object_end,
                                                 "command");
        char *file = json_object_string_field(command_key, object_end, "file");
        if (command == NULL || file == NULL) {
            free(command);
            free(file);
            ok = 0;
            break;
        }
        size_t file_length = strlen(file);
        char *operand = compile_entry_source_operand(command_key, object_end,
                                                     command);
        bool command_c = operand != NULL && strlen(operand) > 2 &&
            strcmp(operand + strlen(operand) - 2, ".c") == 0;
        bool file_c = file_length > 2 &&
            strcmp(file + file_length - 2, ".c") == 0;
        if (command_c || file_c) {
            char *mapped = NULL;
            char canonical[PATH_MAX];
            if (!verified_compile_db_entry(operand, file, owner_kernel,
                                           canonical_kernel_root, &mapped) ||
                !canonical_regular_c_source(mapped, canonical, sizeof(canonical)) ||
                !path_is_within(canonical, canonical_kernel_root) ||
                !canonical_path_list_add(&db_sources, canonical)) {
                ok = 0;
            }
            free(mapped);
            command_entries++;
        }
        free(operand);
        free(command);
        free(file);
        cursor = object_end + 1;
    }
    if (ok && command_entries < 100)
        ok = 0;
    if (ok && !collect_regular_kernel_c_sources(canonical_kernel_root,
                                                &tree_sources))
        ok = 0;
    if (ok && !tracked_kernel_manifest(kernel_root, &tracked_sources))
        ok = 0;
    for (size_t i = 0; ok && i < tree_sources.count; i++) {
        const char *relative = tree_sources.paths[i] + strlen(canonical_kernel_root);
        if (*relative == '/')
            relative++;
        if (!canonical_path_list_contains(&tracked_sources, relative))
            ok = 0;
    }
    for (size_t i = 0; ok && i < db_sources.count; i++) {
        if (!canonical_path_list_contains(&tree_sources, db_sources.paths[i])) {
            ok = 0;
            break;
        }
        const char *relative = db_sources.paths[i] + strlen(canonical_kernel_root);
        if (*relative == '/')
            relative++;
        if (!canonical_path_list_contains(&tracked_sources, relative))
            ok = 0;
    }

    canonical_path_list_destroy(&tracked_sources);
    canonical_path_list_destroy(&tree_sources);
    canonical_path_list_destroy(&db_sources);
    return ok;
}

static int path_compare(const void *left, const void *right)
{
    const char *const *a = left;
    const char *const *b = right;
    return strcmp(*a, *b);
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int stat_identity_equal(const struct stat *left, const struct stat *right)
{
    return left->st_dev == right->st_dev && left->st_ino == right->st_ino &&
        left->st_size == right->st_size &&
        left->st_mtim.tv_sec == right->st_mtim.tv_sec &&
        left->st_mtim.tv_nsec == right->st_mtim.tv_nsec &&
        left->st_ctim.tv_sec == right->st_ctim.tv_sec &&
        left->st_ctim.tv_nsec == right->st_ctim.tv_nsec;
}

static void hash_stat_identity(uint64_t *hash, const struct stat *st)
{
    *hash = hash_bytes(*hash, &st->st_dev, sizeof(st->st_dev));
    *hash = hash_bytes(*hash, &st->st_ino, sizeof(st->st_ino));
    *hash = hash_bytes(*hash, &st->st_size, sizeof(st->st_size));
    *hash = hash_bytes(*hash, &st->st_mtim.tv_sec, sizeof(st->st_mtim.tv_sec));
    *hash = hash_bytes(*hash, &st->st_mtim.tv_nsec, sizeof(st->st_mtim.tv_nsec));
    *hash = hash_bytes(*hash, &st->st_ctim.tv_sec, sizeof(st->st_ctim.tv_sec));
    *hash = hash_bytes(*hash, &st->st_ctim.tv_nsec, sizeof(st->st_ctim.tv_nsec));
}

/* Capture a regular file atomically enough for a fail-closed audit: a
 * replacement, timestamp-only touch, or content rewrite during the read is a
 * failure rather than evidence. */
static int file_identity_and_content_hash(const char *path, struct stat *identity,
                                          uint64_t *content_hash)
{
    struct stat before;
    if (lstat(path, &before) != 0 || !S_ISREG(before.st_mode) ||
        S_ISLNK(before.st_mode))
        return 0;
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    struct stat opened;
    if (fstat(fileno(file), &opened) != 0 || !S_ISREG(opened.st_mode) ||
        opened.st_dev != before.st_dev || opened.st_ino != before.st_ino) {
        fclose(file);
        return 0;
    }
    uint64_t digest = UINT64_C(1469598103934665603);
    unsigned char buffer[65536];
    size_t total = 0;
    size_t read;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) != 0) {
        total += read;
        if (total > 64 * 1024 * 1024) {
            fclose(file);
            return 0;
        }
        digest = hash_bytes(digest, buffer, read);
    }
    int closed = !ferror(file) && fclose(file) == 0;
    struct stat after;
    if (!closed || lstat(path, &after) != 0 || !S_ISREG(after.st_mode) ||
        S_ISLNK(after.st_mode) || !stat_identity_equal(&before, &after))
        return 0;
    *identity = before;
    *content_hash = digest;
    return 1;
}

static int hash_regular_file(uint64_t *hash, const char *path)
{
    struct stat st;
    uint64_t content_hash;
    if (!file_identity_and_content_hash(path, &st, &content_hash))
        return 0;
    *hash = hash_bytes(*hash, path, strlen(path) + 1);
    hash_stat_identity(hash, &st);
    *hash = hash_bytes(*hash, &content_hash, sizeof(content_hash));
    return 1;
}

struct dependency_snapshot_entry {
    char *path;
    struct stat identity;
    uint64_t content_hash;
};

struct dependency_snapshot {
    struct dependency_snapshot_entry *entries;
    size_t count;
    size_t capacity;
};

/* C line-control directives can forge the filename and line numbers that GCC
 * prints in -E output.  They are not needed by this kernel; reject both the
 * standard #line spelling and GCC's numeric linemarker spelling in every
 * authenticated in-tree dependency.  Comments/literals are blanked first,
 * and escaped newlines are treated as preprocessing whitespace. */
static const char *skip_pp_space_and_splices(const char *p)
{
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\f' ||
               *p == '\v')
            p++;
        if ((p[0] == '\\' && p[1] == '\n') ||
            (p[0] == '\\' && p[1] == '\r' && p[2] == '\n')) {
            p += 2;
            if (p[-1] == '\r')
                p++;
            continue;
        }
        /* ??/ is a backslash before line-splicing when trigraphs are on. */
        if (p[0] == '?' && p[1] == '?' && p[2] == '/' &&
            (p[3] == '\n' || (p[3] == '\r' && p[4] == '\n'))) {
            p += p[3] == '\n' ? 4 : 5;
            continue;
        }
        return p;
    }
}

static int pp_word_is_line(const char *p)
{
    static const char word[] = "line";
    for (size_t i = 0; i < sizeof(word) - 1; i++) {
        p = skip_pp_space_and_splices(p);
        if (*p++ != word[i])
            return 0;
    }
    p = skip_pp_space_and_splices(p);
    return !((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
             (*p >= '0' && *p <= '9') || *p == '_');
}

static char trigraph_replacement(char third)
{
    switch (third) {
    case '=': return '#';
    case '(': return '[';
    case '/': return '\\';
    case ')': return ']';
    case '\'': return '^';
    case '<': return '{';
    case '!': return '|';
    case '>': return '}';
    case '-': return '~';
    default: return '\0';
    }
}

/* Build the source view relevant to directives in the same order as early C
 * translation phases: trigraph replacement, escaped-newline splicing, then
 * comment replacement.  This catches split slash-comment and %: spellings
 * that become effective line-control directives only after those phases. */
static char *preprocessing_directive_view(const char *text)
{
    size_t size = strlen(text);
    char *trigraphs = malloc(size + 1);
    if (trigraphs == NULL)
        return NULL;
    size_t written = 0;
    for (size_t i = 0; i < size; i++) {
        if (i + 2 < size && text[i] == '?' && text[i + 1] == '?' &&
            trigraph_replacement(text[i + 2]) != '\0') {
            trigraphs[written++] = trigraph_replacement(text[i + 2]);
            i += 2;
        } else {
            trigraphs[written++] = text[i];
        }
    }
    trigraphs[written] = '\0';
    char *spliced = malloc(written + 1);
    if (spliced == NULL) {
        free(trigraphs);
        return NULL;
    }
    size_t out = 0;
    for (size_t i = 0; i < written; i++) {
        if (trigraphs[i] == '\\' && trigraphs[i + 1] == '\n') {
            i++;
            continue;
        }
        if (trigraphs[i] == '\\' && trigraphs[i + 1] == '\r' &&
            trigraphs[i + 2] == '\n') {
            i += 2;
            continue;
        }
        spliced[out++] = trigraphs[i];
    }
    spliced[out] = '\0';
    free(trigraphs);
    char *clean = sanitize_c_source(spliced);
    free(spliced);
    return clean;
}

static int source_has_line_control_directive(const char *path)
{
    char *text = read_source(path);
    char *clean = preprocessing_directive_view(text);
    if (clean == NULL) {
        free(text);
        return 1;
    }
    int found = 0;
    const char *line = clean;
    while (*line != '\0' && !found) {
        const char *p = skip_pp_space_and_splices(line);
        size_t introducer = *p == '#' ? 1 :
            (p[0] == '%' && p[1] == ':' ? 2 :
             (p[0] == '?' && p[1] == '?' && p[2] == '=' ? 3 : 0));
        if (introducer != 0) {
            p = skip_pp_space_and_splices(p + introducer);
            found = (*p >= '0' && *p <= '9') || pp_word_is_line(p);
        }
        const char *next = strchr(line, '\n');
        if (next == NULL)
            break;
        line = next + 1;
    }
    free(clean);
    free(text);
    return found;
}

static void dependency_snapshot_destroy(struct dependency_snapshot *snapshot)
{
    for (size_t i = 0; i < snapshot->count; i++)
        free(snapshot->entries[i].path);
    free(snapshot->entries);
    memset(snapshot, 0, sizeof(*snapshot));
}

static int dependency_snapshot_find(const struct dependency_snapshot *snapshot,
                                    const char *path)
{
    for (size_t i = 0; i < snapshot->count; i++)
        if (strcmp(snapshot->entries[i].path, path) == 0)
            return (int)i;
    return -1;
}

static int dependency_snapshot_add(struct dependency_snapshot *snapshot,
                                   const char *path)
{
    if (dependency_snapshot_find(snapshot, path) >= 0)
        return 1;
    if (source_has_line_control_directive(path))
        return 0;
    if (snapshot->count == snapshot->capacity) {
        size_t capacity = snapshot->capacity == 0 ? 64 : snapshot->capacity * 2;
        struct dependency_snapshot_entry *grown = realloc(snapshot->entries,
                                                            capacity * sizeof(*grown));
        if (grown == NULL)
            return 0;
        snapshot->entries = grown;
        snapshot->capacity = capacity;
    }
    struct dependency_snapshot_entry *entry = &snapshot->entries[snapshot->count];
    memset(entry, 0, sizeof(*entry));
    entry->path = strdup(path);
    if (entry->path == NULL ||
        !file_identity_and_content_hash(path, &entry->identity,
                                        &entry->content_hash)) {
        free(entry->path);
        memset(entry, 0, sizeof(*entry));
        return 0;
    }
    snapshot->count++;
    return 1;
}

static int dependency_snapshot_verify(const struct dependency_snapshot *snapshot)
{
    if (snapshot->count == 0)
        return 0;
    for (size_t i = 0; i < snapshot->count; i++) {
        struct stat identity;
        uint64_t content_hash;
        const struct dependency_snapshot_entry *entry = &snapshot->entries[i];
        if (!file_identity_and_content_hash(entry->path, &identity, &content_hash) ||
            !stat_identity_equal(&entry->identity, &identity) ||
            entry->content_hash != content_hash)
            return 0;
    }
    return 1;
}

static int hash_git_state_at(const char *kernel_root, uint64_t *hash)
{
    char *quoted = shell_quote(kernel_root);
    if (quoted == NULL)
        return 0;
    size_t size = strlen(quoted) * 2 + 128;
    char *command = malloc(size);
    if (command == NULL) {
        free(quoted);
        return 0;
    }
    snprintf(command, size,
             "git -C %s status --porcelain=v1 --untracked-files=all 2>/dev/null; "
             "git -C %s ls-files -s 2>/dev/null", quoted, quoted);
    free(quoted);
    FILE *pipe = popen(command, "r");
    free(command);
    if (pipe == NULL)
        return 0;
    unsigned char buffer[4096];
    size_t read;
    int saw = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), pipe)) != 0) {
        *hash = hash_bytes(*hash, buffer, read);
        saw = 1;
    }
    int status = pclose(pipe);
    return saw && status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

struct guard_state_snapshot {
    uint64_t digest;
    size_t source_count;
};

static int guard_state_snapshot(const char *repo_root, const char *compile_db,
                                const char *kernel_root,
                                struct guard_state_snapshot *snapshot)
{
    if (!kernel_tree_has_no_source_symlinks(kernel_root))
        return 0;
    struct canonical_path_list sources = {0};
    if (!collect_regular_kernel_c_sources(kernel_root, &sources)) {
        canonical_path_list_destroy(&sources);
        return 0;
    }
    qsort(sources.paths, sources.count, sizeof(*sources.paths), path_compare);
    uint64_t digest = UINT64_C(1469598103934665603);
    if (!hash_regular_file(&digest, compile_db)) {
        canonical_path_list_destroy(&sources);
        return 0;
    }
    digest = hash_bytes(digest, &sources.count, sizeof(sources.count));
    for (size_t i = 0; i < sources.count; i++) {
        if (!hash_regular_file(&digest, sources.paths[i])) {
            canonical_path_list_destroy(&sources);
            return 0;
        }
    }
    char cwd[PATH_MAX];
    int git_ok = hash_git_state_at(kernel_root, &digest);
    if (!git_ok && getcwd(cwd, sizeof(cwd)) != NULL) {
        char fallback[PATH_MAX];
        int written = snprintf(fallback, sizeof(fallback), "%s/kernel", cwd);
        if (written > 0 && (size_t)written < sizeof(fallback))
            git_ok = hash_git_state_at(fallback, &digest);
    }
    size_t source_count = sources.count;
    canonical_path_list_destroy(&sources);
    if (!git_ok)
        return 0;
    snapshot->digest = digest;
    snapshot->source_count = source_count;
    (void)repo_root;
    return 1;
}

static int post_snapshot_test_hook(const char *kernel_root,
                                   const char *compile_db,
                                   const struct dependency_snapshot *dependencies)
{
    const char *path = getenv("KQUEUE_GUARD_TEST_POST_SNAPSHOT_TOUCH");
    if (path == NULL || *path == '\0')
        return 1;
    char canonical[PATH_MAX];
    if (realpath(path, canonical) == NULL || !path_regular_nonsymlink(canonical))
        return 0;
    if (!path_is_within(canonical, kernel_root) &&
        strcmp(canonical, compile_db) != 0 &&
        dependency_snapshot_find(dependencies, canonical) < 0)
        return 0;
    struct timespec times[2];
    if (clock_gettime(CLOCK_REALTIME, &times[0]) != 0)
        return 0;
    times[1] = times[0];
    return utimensat(AT_FDCWD, canonical, times, 0) == 0;
}

static const char *last_text(const char *haystack, const char *needle)
{
    const char *last = NULL;
    const char *at = haystack;
    while ((at = strstr(at, needle)) != NULL) {
        last = at;
        at += strlen(needle);
    }
    return last;
}

static char *replace_all_text(const char *text, const char *from,
                              const char *to)
{
    size_t from_len = strlen(from);
    size_t to_len = strlen(to);
    if (from_len == 0)
        return NULL;
    size_t count = 0;
    for (const char *p = text; (p = strstr(p, from)) != NULL; p += from_len)
        count++;
    size_t text_len = strlen(text);
    if (count > (SIZE_MAX - text_len - 1) / (to_len > from_len ?
                                               to_len - from_len : 1))
        return NULL;
    size_t size = text_len + count * (to_len - from_len) + 1;
    char *out = malloc(size);
    if (out == NULL)
        return NULL;
    char *write = out;
    const char *read = text;
    const char *match;
    while ((match = strstr(read, from)) != NULL) {
        size_t prefix = (size_t)(match - read);
        memcpy(write, read, prefix);
        write += prefix;
        memcpy(write, to, to_len);
        write += to_len;
        read = match + from_len;
    }
    strcpy(write, read);
    return out;
}

static char *shell_quote(const char *text)
{
    size_t size = 3;
    for (const char *p = text; *p != '\0'; p++)
        size += *p == '\'' ? 4 : 1;
    char *out = malloc(size);
    if (out == NULL)
        return NULL;
    char *write = out;
    *write++ = '\'';
    for (const char *p = text; *p != '\0'; p++) {
        if (*p == '\'') {
            memcpy(write, "'\\''", 4);
            write += 4;
        } else {
            *write++ = *p;
        }
    }
    *write++ = '\'';
    *write = '\0';
    return out;
}

static int find_compile_database(const char *repo_root, char *path,
                                 size_t path_size)
{
    const char *override = getenv("KQUEUE_GUARD_COMPILE_DB");
    if (override != NULL && *override != '\0') {
        if (!path_regular_nonsymlink(override))
            return 0;
        int written = snprintf(path, path_size, "%s", override);
        return written > 0 && (size_t)written < path_size;
    }
    int written = snprintf(path, path_size,
                           "%s/build-x86_64/kernel/build/compile_commands.json",
                           repo_root);
    if (written > 0 && (size_t)written < path_size &&
        path_regular_nonsymlink(path))
        return 1;
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        return 0;
    written = snprintf(path, path_size,
                       "%s/build-x86_64/kernel/build/compile_commands.json",
                       cwd);
    return written > 0 && (size_t)written < path_size &&
        path_regular_nonsymlink(path);
}

enum cpp_marker_flag {
    CPP_MARKER_ENTER = 1u << 0,
    CPP_MARKER_RETURN = 1u << 1,
    CPP_MARKER_SYSTEM = 1u << 2,
    CPP_MARKER_EXTERN_C = 1u << 3,
};

struct cpp_marker {
    char path[PATH_MAX];
    long line;
    unsigned flags;
    bool has_flags;
};

enum cpp_provenance_kind {
    CPP_PROVENANCE_IN_TREE,
    CPP_PROVENANCE_EXTERNAL,
    CPP_PROVENANCE_PSEUDO,
    CPP_PROVENANCE_DIRECTORY,
};

struct cpp_provenance {
    enum cpp_provenance_kind kind;
    char path[PATH_MAX];
    long line;
};

struct cpp_provenance_stack {
    struct cpp_provenance current;
    struct cpp_provenance tu_source;
    struct cpp_provenance frames[256];
    size_t count;
    bool bootstrap;
};

/* GCC emits # N "file" [1] [2] [3] [4].  Flags 1 and 2 describe physical
 * include-stack transitions; a bare marker can also be the output of a C
 * #line directive, so it is never allowed to switch physical source. */
static int marker_provenance(const char *line, struct cpp_marker *marker)
{
    if (line[0] != '#')
        return 0;
    const char *p = line + 1;
    while (*p == ' ' || *p == '\t')
        p++;
    char *end = NULL;
    errno = 0;
    long line_number = strtol(p, &end, 10);
    if (errno != 0 || end == p || line_number < 0)
        return 0;
    p = end;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != '"')
        return 0;
    p++;
    const char *quote = strchr(p, '"');
    if (quote == NULL || (size_t)(quote - p) >= sizeof(marker->path))
        return 0;
    memcpy(marker->path, p, (size_t)(quote - p));
    marker->path[quote - p] = '\0';
    marker->line = line_number;
    marker->flags = 0;
    marker->has_flags = false;
    p = quote + 1;
    while (*p == ' ' || *p == '\t')
        p++;
    while (*p != '\0' && *p != '\n' && *p != '\r') {
        char *flag_end = NULL;
        errno = 0;
        long flag = strtol(p, &flag_end, 10);
        if (errno != 0 || flag_end == p || flag < 1 || flag > 4 ||
            (marker->flags & (1u << (unsigned)(flag - 1))) != 0)
            return 0;
        marker->flags |= 1u << (unsigned)(flag - 1);
        marker->has_flags = true;
        p = flag_end;
        while (*p == ' ' || *p == '\t')
            p++;
    }
    return 1;
}

static int cpp_provenance_same_source(const struct cpp_provenance *left,
                                      const struct cpp_provenance *right)
{
    return left->kind == right->kind && strcmp(left->path, right->path) == 0;
}

static int cpp_source_from_path(const char *path, const char *kernel_root,
                                const struct canonical_path_list *tracked,
                                const struct dependency_snapshot *snapshot,
                                struct cpp_provenance *source)
{
    memset(source, 0, sizeof(*source));
    if (path[0] == '<') {
        if (strcmp(path, "<built-in>") != 0 &&
            strcmp(path, "<command-line>") != 0)
            return 0;
        source->kind = CPP_PROVENANCE_PSEUDO;
        return snprintf(source->path, sizeof(source->path), "%s", path) > 0;
    }
    char canonical[PATH_MAX];
    if (realpath(path, canonical) == NULL)
        return 0;
    struct stat marker_st;
    if (lstat(path, &marker_st) != 0 || S_ISLNK(marker_st.st_mode))
        return 0;
    if (S_ISDIR(marker_st.st_mode)) {
        source->kind = CPP_PROVENANCE_DIRECTORY;
        return snprintf(source->path, sizeof(source->path), "%s", canonical) > 0;
    }
    if (!S_ISREG(marker_st.st_mode) || !path_regular_nonsymlink(canonical) ||
        source_has_line_control_directive(canonical))
        return 0;
    int written = snprintf(source->path, sizeof(source->path), "%s", canonical);
    if (written <= 0 || (size_t)written >= sizeof(source->path))
        return 0;
    if (!path_is_within(canonical, kernel_root)) {
        if (snapshot != NULL && dependency_snapshot_find(snapshot, canonical) < 0)
            return 0;
        source->kind = CPP_PROVENANCE_EXTERNAL;
        return 1;
    }
    const char *relative = canonical + strlen(kernel_root);
    if (*relative != '/')
        return 0;
    relative++;
    if ((tracked != NULL && !canonical_path_list_contains(tracked, relative)) ||
        (snapshot != NULL && dependency_snapshot_find(snapshot, canonical) < 0))
        return 0;
    source->kind = CPP_PROVENANCE_IN_TREE;
    return 1;
}

static int cpp_provenance_init(struct cpp_provenance_stack *stack,
                               const char *mapped_source,
                               const char *kernel_root,
                               const struct canonical_path_list *tracked,
                               const struct dependency_snapshot *snapshot)
{
    memset(stack, 0, sizeof(*stack));
    if (!cpp_source_from_path(mapped_source, kernel_root, tracked, snapshot,
                              &stack->tu_source) ||
        stack->tu_source.kind != CPP_PROVENANCE_IN_TREE)
        return 0;
    stack->current = stack->tu_source;
    stack->bootstrap = true;
    return 1;
}

static int cpp_provenance_transition(struct cpp_provenance_stack *stack,
                                     const struct cpp_marker *marker,
                                     const char *kernel_root,
                                     const struct canonical_path_list *tracked,
                                     const struct dependency_snapshot *snapshot)
{
    struct cpp_provenance next;
    if (!cpp_source_from_path(marker->path, kernel_root, tracked, snapshot,
                              &next))
        return 0;
    next.line = marker->line;
    bool entering = (marker->flags & CPP_MARKER_ENTER) != 0;
    bool returning = (marker->flags & CPP_MARKER_RETURN) != 0;
    if (entering && returning)
        return 0;
    if (entering) {
        if (next.kind != CPP_PROVENANCE_IN_TREE &&
            next.kind != CPP_PROVENANCE_EXTERNAL)
            return 0;
        if (stack->count == sizeof(stack->frames) / sizeof(stack->frames[0]))
            return 0;
        stack->frames[stack->count++] = stack->current;
        stack->current = next;
        stack->bootstrap = false;
        return 1;
    }
    if (returning) {
        if (stack->count == 0)
            return 0;
        struct cpp_provenance previous = stack->frames[--stack->count];
        if (!cpp_provenance_same_source(&next, &previous))
            return 0;
        stack->current = previous;
        stack->current.line = marker->line;
        stack->bootstrap = false;
        return 1;
    }
    if (next.kind == CPP_PROVENANCE_PSEUDO ||
        next.kind == CPP_PROVENANCE_DIRECTORY) {
        if (!stack->bootstrap)
            return 0;
        stack->current = next;
        return 1;
    }
    if (cpp_provenance_same_source(&next, &stack->current)) {
        stack->current.line = marker->line;
        return 1;
    }
    if (stack->bootstrap && cpp_provenance_same_source(&next, &stack->tu_source)) {
        stack->current = next;
        stack->bootstrap = false;
        return 1;
    }
    return 0;
}

static int collect_preprocessor_dependency_source(
    const struct cpp_provenance *source, struct canonical_path_list *dependencies)
{
    return (source->kind != CPP_PROVENANCE_IN_TREE &&
            source->kind != CPP_PROVENANCE_EXTERNAL) ||
        canonical_path_list_add(dependencies, source->path);
}

static int dependency_snapshot_build(struct dependency_snapshot *snapshot,
                                     const struct canonical_path_list *dependencies)
{
    for (size_t i = 0; i < dependencies->count; i++)
        if (!dependency_snapshot_add(snapshot, dependencies->paths[i]))
            return 0;
    return snapshot->count == dependencies->count && snapshot->count != 0;
}

static int provenance_in_span(const char *path, long line,
                              const struct poll_source_span *span)
{
    return strcmp(path, span->path) == 0 && line >= span->first_line &&
        line <= span->last_line;
}

static int strict_plain_null_comparison(const char *statement, const char *at,
                                        const char *after_member);

static int plain_null_comparison(const char *statement, const char *at,
                                 const char *after_member)
{
    return strict_plain_null_comparison(statement, at, after_member);
#if 0
    const char *after = after_member;
    while (*after == ' ' || *after == '\t' || *after == '\r' ||
           *after == '\n')
        after++;
    if (after[0] == '!' && after[1] == '=') {
        const char *rhs = after + 2;
        while (*rhs == ' ' || *rhs == '\t' || *rhs == '\r' ||
               *rhs == '\n')
            rhs++;
        if (strncmp(rhs, "NULL", 4) == 0 &&
            !(rhs[4] == '_' || (rhs[4] >= 'A' && rhs[4] <= 'Z') ||
              (rhs[4] >= 'a' && rhs[4] <= 'z') ||
              (rhs[4] >= '0' && rhs[4] <= '9')))
            return 1;
        bool saw_zero = false;
        bool null_shape = true;
        for (const char *p = rhs; *p != '\0' && *p != ';' &&
             *p != '{' && *p != '}'; p++) {
            char c = *p;
            if (saw_zero && (c == ')' || c == ',' || c == '&' || c == '|' ||
                              c == '?'))
                break;
            if (c == '0') {
                saw_zero = true;
                continue;
            }
            if (!(c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
                  c == '(' || c == ')' || c == '*' || c == 'v' ||
                  c == 'o' || c == 'i' || c == 'd'))
            {
                if (saw_zero)
                    break;
                null_shape = false;
                break;
            }
            if (saw_zero)
                continue;
        }
        if (saw_zero && null_shape)
            return 1;
    }
    const char *neq = NULL;
    for (const char *p = statement; p + 1 < at; p++)
        if (p[0] == '!' && p[1] == '=')
            neq = p;
    if (neq == NULL)
        return 0;
    const char *left_end = neq;
    while (left_end > statement &&
           (left_end[-1] == ' ' || left_end[-1] == '\t' ||
            left_end[-1] == '\r' || left_end[-1] == '\n'))
        left_end--;
    const char *left_start = left_end;
    while (left_start > statement &&
           ((left_start[-1] >= 'A' && left_start[-1] <= 'Z') ||
            (left_start[-1] >= 'a' && left_start[-1] <= 'z') ||
            (left_start[-1] >= '0' && left_start[-1] <= '9') ||
            left_start[-1] == '_'))
        left_start--;
    if (left_end - left_start != 4 || memcmp(left_start, "NULL", 4) != 0) {
        const char *ret = strstr(statement, "return");
        if (ret == NULL || ret >= neq)
            return 0;
        bool saw_zero = false;
        for (const char *p = ret + strlen("return"); p < neq; p++) {
            char c = *p;
            if (c == '0') {
                saw_zero = true;
                continue;
            }
            if (!(c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
                  c == '(' || c == ')' || c == '*' || c == 'v' ||
                  c == 'o' || c == 'i' || c == 'd'))
                return 0;
        }
        if (!saw_zero)
            return 0;
    }
    for (const char *p = neq + 2; p < at; p++) {
        char c = *p;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == ' ' ||
              c == '\t' || c == '\r' || c == '\n' || c == '-' ||
              c == '>' || c == '.'))
            return 0;
    }
    return 1;
#endif
}

/* Keep a comparison local to the matched member.  The legacy parser accepts
 * casts and return forms, but may otherwise find an unrelated `!=` in a long
 * preprocessed statement; that must never turn consume(f->ops->poll) into a
 * permitted null test. */
static const char *skip_c_space(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

static int null_endpoint(const char *start, const char **after)
{
    const char *p = skip_c_space(start);
    if (strncmp(p, "NULL", 4) == 0 &&
        !((p[4] >= 'A' && p[4] <= 'Z') || (p[4] >= 'a' && p[4] <= 'z') ||
          (p[4] >= '0' && p[4] <= '9') || p[4] == '_')) {
        *after = p + 4;
        return 1;
    }
    /* Accept only the compiler's exact null-cast token shape, with
     * whitespace between tokens: ((void *)0). */
    if (*p++ != '(')
        return 0;
    p = skip_c_space(p);
    if (*p++ != '(')
        return 0;
    p = skip_c_space(p);
    if (strncmp(p, "void", 4) != 0 ||
        ((p[4] >= 'A' && p[4] <= 'Z') || (p[4] >= 'a' && p[4] <= 'z') ||
         (p[4] >= '0' && p[4] <= '9') || p[4] == '_'))
        return 0;
    p = skip_c_space(p + 4);
    if (*p++ != '*')
        return 0;
    p = skip_c_space(p);
    if (*p++ != ')')
        return 0;
    p = skip_c_space(p);
    if (*p++ != '0')
        return 0;
    p = skip_c_space(p);
    if (*p++ != ')')
        return 0;
    *after = p;
    return 1;
}

static int null_endpoint_delimiter(const char *after)
{
    const char *p = skip_c_space(after);
    return *p == '\0' || *p == ')' || *p == ';' || *p == '{' ||
        *p == '}' || *p == ',' || *p == '?' || *p == ':' ||
        (p[0] == '&' && p[1] == '&') || (p[0] == '|' && p[1] == '|');
}

static int direct_member_expression(const char *start, const char *end)
{
    if (start >= end)
        return 0;
    for (const char *p = start; p < end; p++) {
        char c = *p;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == ' ' ||
              c == '\t' || c == '\r' || c == '\n' || c == '-' ||
              c == '>' || c == '.'))
            return 0;
    }
    return 1;
}

static int strict_plain_null_comparison(const char *statement, const char *at,
                                        const char *after_member)
{
    const char *after = NULL;
    const char *right = skip_c_space(after_member);
    if (right[0] == '!' && right[1] == '=')
        return null_endpoint(right + 2, &after) &&
            null_endpoint_delimiter(after);
    const char *neq = NULL;
    for (const char *p = statement; p + 1 < at; p++)
        if (p[0] == '!' && p[1] == '=')
            neq = p;
    if (neq == NULL || !direct_member_expression(neq + 2, at))
        return 0;
    for (const char *candidate = statement; candidate < neq; candidate++)
        if (null_endpoint(candidate, &after) && skip_c_space(after) == neq)
            return 1;
    return 0;
#if 0
    const char *right = after_member;
    while (*right == ' ' || *right == '\t' || *right == '\r' ||
           *right == '\n')
        right++;
    if (right[0] == '!' && right[1] == '=') {
        right += 2;
        while (*right == ' ' || *right == '\t' || *right == '\r' ||
               *right == '\n')
            right++;
        if (strncmp(right, "NULL", 4) == 0 &&
            !((right[4] >= 'A' && right[4] <= 'Z') ||
              (right[4] >= 'a' && right[4] <= 'z') ||
              (right[4] >= '0' && right[4] <= '9') || right[4] == '_'))
            return 1;
        bool saw_zero = false;
        for (const char *p = right; *p != '\0' && *p != ';' &&
             *p != '{' && *p != '}'; p++) {
            char c = *p;
            if (saw_zero && (c == ',' || c == '&' || c == '|' || c == '?'))
                return 1;
            if (c == '0') {
                saw_zero = true;
                continue;
            }
            if (!(c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
                  c == '(' || c == ')' || c == '*' || c == 'v' ||
                  c == 'o' || c == 'i' || c == 'd'))
                return 0;
        }
        return saw_zero;
    }
    const char *neq = NULL;
    for (const char *p = statement; p + 1 < at; p++)
        if (p[0] == '!' && p[1] == '=')
            neq = p;
    if (neq == NULL)
        return 0;
    for (const char *p = neq + 2; p < at; p++) {
        char c = *p;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == ' ' ||
              c == '\t' || c == '\r' || c == '\n' || c == '-' ||
              c == '>' || c == '.'))
            return 0;
    }
    const char *left = neq;
    while (left > statement && (left[-1] == ' ' || left[-1] == '\t' ||
                                left[-1] == '\r' || left[-1] == '\n'))
        left--;
    if (left - statement >= 4 && memcmp(left - 4, "NULL", 4) == 0 &&
        (left - statement == 4 || !((left[-5] >= 'A' && left[-5] <= 'Z') ||
                                     (left[-5] >= 'a' && left[-5] <= 'z') ||
                                     (left[-5] >= '0' && left[-5] <= '9') ||
                                     left[-5] == '_')))
        return 1;
    return left > statement && left[-1] == '0';
#endif
}

/* The sole dispatcher has one boolean availability guard (`if (f->ops &&
 * f->ops->poll)`).  Permit only that direct, non-nested if-condition shape;
 * a wrapper such as consume(f->ops->poll) has an additional '(' and is a raw
 * function-pointer escape, not a harmless condition. */
static int plain_member_truthiness(const char *statement, const char *at,
                                   const char *after_member)
{
    const char *p = statement;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (strncmp(p, "if", 2) != 0 ||
        ((p[2] >= 'A' && p[2] <= 'Z') || (p[2] >= 'a' && p[2] <= 'z') ||
         (p[2] >= '0' && p[2] <= '9') || p[2] == '_'))
        return 0;
    p += 2;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (*p++ != '(')
        return 0;
    for (; p < at; p++) {
        char c = *p;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == ' ' ||
              c == '\t' || c == '\r' || c == '\n' || c == '-' ||
              c == '>' || c == '.' || c == '&' || c == '|' || c == '!'))
            return 0;
    }
    p = after_member;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (*p++ != ')')
        return 0;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return *p == '{';
}

static int expanded_line_is_guarded(const char *line, const char *path,
                                    long line_number,
                                    const struct poll_source_span *dispatcher,
                                    const struct poll_source_span *vfs,
                                    regex_t *member_regex, int *members,
                                    int *calls)
{
    char *clean = sanitize_c_source(line);
    size_t offset = 0;
    regmatch_t match;
    while (regexec(member_regex, clean + offset, 1, &match, 0) == 0) {
        size_t position = offset + (size_t)match.rm_so;
        const char *at = clean + position;
        (*members)++;
        const char *after_member = strstr(at, "poll");
        if (after_member == NULL) {
            free(clean);
            return 0;
        }
        after_member += strlen("poll");
        const char *after_space = after_member;
        while (*after_space == ' ' || *after_space == '\t' ||
               *after_space == '\r' || *after_space == '\n')
            after_space++;
        bool member_write = after_space[0] == '=' && after_space[1] != '=';
        const char *statement = at;
        while (statement > clean && statement[-1] != ';' &&
               statement[-1] != '{' && statement[-1] != '}')
            statement--;
        bool execution_whitelisted =
            provenance_in_span(path, line_number, dispatcher) ||
            provenance_in_span(path, line_number, vfs);
        const char *call = after_member;
        while (*call == ' ' || *call == '\t' || *call == '\r' ||
               *call == '\n' || *call == ')')
            call++;
        bool direct_call = *call == '(';
        if (!member_write &&
            !(plain_null_comparison(statement, at, after_member) &&
              strict_plain_null_comparison(statement, at, after_member)) &&
            !(execution_whitelisted &&
              (direct_call || plain_member_truthiness(statement, at, after_member)))) {
            free(clean);
            return 0;
        }
        if (direct_call)
            (*calls)++;
        offset += (size_t)match.rm_eo;
    }
    free(clean);
    return 1;
}

/* The compiler expands NULL to ((void *)0) in ordinary no-brace conditions.
 * Accept that exact endpoint only up to the condition's immediate closing ')';
 * do not let the capability comparison bless a later raw member retrieval in
 * the unbraced return statement. */
static void test_no_brace_null_condition_guard(void)
{
    regex_t member_regex;
    const char *member_pattern =
        "(->|\\.)[[:space:]]*ops[[:space:]]*\\)*[[:space:]]*"
        "(->|\\.)[[:space:]]*poll([^_A-Za-z0-9]|$)";
    require(regcomp(&member_regex, member_pattern, REG_EXTENDED) == 0,
            "no-brace null-condition regex compilation failed");
    const struct poll_source_span none = {0};
    int members = 0;
    int calls = 0;
    static const char normal[] =
        "if (f->ops->poll != ((void *)0)) return 0;";
    require(expanded_line_is_guarded(normal, "synthetic.c", 1, &none, &none,
                                     &member_regex, &members, &calls),
            "bounded null endpoint rejected no-brace capability condition");

    members = 0;
    calls = 0;
    static const char raw_escape[] =
        "if (f->ops->poll != ((void *)0)) return f->ops->poll;";
    require(!expanded_line_is_guarded(raw_escape, "synthetic.c", 1, &none,
                                      &none, &member_regex, &members, &calls),
            "no-brace capability condition blessed raw poll retrieval");
    regfree(&member_regex);
}

static int preprocessed_translation_unit_dependencies(
    const char *command, const char *mapped_source, const char *owner_kernel,
    const char *guard_kernel, const struct canonical_path_list *tracked,
    struct canonical_path_list *dependencies)
{
    const char *output_flag = last_text(command, " -o ");
    if (output_flag == NULL)
        return 0;
    char *prefix = strndup(command, (size_t)(output_flag - command));
    if (prefix == NULL)
        return 0;
    char *rewritten_prefix = replace_all_text(prefix, owner_kernel,
                                              guard_kernel);
    char *rewritten_file = strdup(mapped_source);
    free(prefix);
    if (rewritten_prefix == NULL || rewritten_file == NULL ||
        !path_regular_nonsymlink(rewritten_file)) {
        free(rewritten_prefix);
        free(rewritten_file);
        return 0;
    }
    char *quoted_file = shell_quote(rewritten_file);
    if (quoted_file == NULL) {
        free(rewritten_prefix);
        free(rewritten_file);
        return 0;
    }
    size_t command_size = strlen(rewritten_prefix) + strlen(quoted_file) + 32;
    char *preprocess = malloc(command_size);
    if (preprocess == NULL) {
        free(quoted_file);
        free(rewritten_prefix);
        free(rewritten_file);
        return 0;
    }
    snprintf(preprocess, command_size, "%s -E %s 2>/dev/null",
             rewritten_prefix, quoted_file);
    FILE *pipe = popen(preprocess, "r");
    free(preprocess);
    free(quoted_file);
    free(rewritten_prefix);
    if (pipe == NULL) {
        free(rewritten_file);
        return 0;
    }

    struct cpp_provenance_stack provenance;
    int ok = cpp_provenance_init(&provenance, rewritten_file, guard_kernel,
                                 tracked, NULL) &&
        collect_preprocessor_dependency_source(&provenance.current, dependencies);
    char line[65536];
    size_t line_count = 0;
    while (ok && fgets(line, sizeof(line), pipe) != NULL) {
        if (++line_count > 10000000) {
            ok = 0;
            break;
        }
        if (strchr(line, '\n') == NULL && !feof(pipe)) {
            ok = 0;
            break;
        }
        struct cpp_marker marker;
        if (marker_provenance(line, &marker)) {
            if (!cpp_provenance_transition(&provenance, &marker, guard_kernel,
                                           tracked, NULL)) {
                ok = 0;
            } else {
                ok = collect_preprocessor_dependency_source(&provenance.current,
                                                           dependencies);
            }
        }
    }
    int status = pclose(pipe);
    free(rewritten_file);
    return ok && status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int preprocessed_translation_unit_guard(
    const char *command, const char *mapped_source, const char *owner_kernel,
    const char *guard_kernel, const struct dependency_snapshot *dependencies,
    const struct poll_source_span *dispatcher, const struct poll_source_span *vfs,
    regex_t *member_regex, int *members, int *calls)
{
    const char *output_flag = last_text(command, " -o ");
    if (output_flag == NULL)
        return 0;
    char *prefix = strndup(command, (size_t)(output_flag - command));
    if (prefix == NULL)
        return 0;
    char *rewritten_prefix = replace_all_text(prefix, owner_kernel,
                                              guard_kernel);
    char *rewritten_file = strdup(mapped_source);
    free(prefix);
    if (rewritten_prefix == NULL || rewritten_file == NULL ||
        !path_regular_nonsymlink(rewritten_file) ||
        dependency_snapshot_find(dependencies, rewritten_file) < 0) {
        free(rewritten_prefix);
        free(rewritten_file);
        return 0;
    }
    char *quoted_file = shell_quote(rewritten_file);
    if (quoted_file == NULL) {
        free(rewritten_prefix);
        free(rewritten_file);
        return 0;
    }
    size_t command_size = strlen(rewritten_prefix) + strlen(quoted_file) + 32;
    char *preprocess = malloc(command_size);
    if (preprocess == NULL) {
        free(quoted_file);
        free(rewritten_prefix);
        free(rewritten_file);
        return 0;
    }
    snprintf(preprocess, command_size, "%s -E %s 2>/dev/null",
             rewritten_prefix, quoted_file);
    FILE *pipe = popen(preprocess, "r");
    free(preprocess);
    free(quoted_file);
    free(rewritten_prefix);
    if (pipe == NULL) {
        free(rewritten_file);
        return 0;
    }

    struct cpp_provenance_stack provenance_state;
    int ok = cpp_provenance_init(&provenance_state, rewritten_file, guard_kernel,
                                 NULL, dependencies);
    char provenance[PATH_MAX];
    int copied = ok ? snprintf(provenance, sizeof(provenance), "%s",
                               provenance_state.current.path) : 0;
    long provenance_line = 1;
    ok = ok && copied > 0 && (size_t)copied < sizeof(provenance);
    char line[65536];
    char statement[262144];
    size_t statement_size = 0;
    char statement_path[PATH_MAX] = {0};
    long statement_line = 0;
    size_t line_count = 0;
    while (ok && fgets(line, sizeof(line), pipe) != NULL) {
        if (++line_count > 10000000) {
            ok = 0;
            break;
        }
        if (strchr(line, '\n') == NULL && !feof(pipe)) {
            ok = 0;
            break;
        }
        struct cpp_marker marker;
        if (marker_provenance(line, &marker)) {
            if (!cpp_provenance_transition(&provenance_state, &marker,
                                           guard_kernel, NULL, dependencies)) {
                ok = 0;
                break;
            }
            copied = snprintf(provenance, sizeof(provenance), "%s",
                              provenance_state.current.path);
            ok = copied > 0 && (size_t)copied < sizeof(provenance);
            provenance_line = provenance_state.current.line;
            continue;
        }
        for (const char *p = line; ok && *p != '\0'; p++) {
            char c = *p == '\n' ? ' ' : *p;
            if (statement_size == 0 &&
                (c == ' ' || c == '\t' || c == '\r'))
                continue;
            if (statement_size == 0) {
                copied = snprintf(statement_path, sizeof(statement_path), "%s",
                                  provenance);
                ok = copied > 0 && (size_t)copied < sizeof(statement_path);
                statement_line = provenance_line;
            }
            if (statement_size + 2 >= sizeof(statement)) {
                ok = 0;
                break;
            }
            statement[statement_size++] = c;
            if (c != ';' && c != '{' && c != '}')
                continue;
            statement[statement_size] = '\0';
            if (!expanded_line_is_guarded(statement, statement_path,
                                          statement_line, dispatcher, vfs,
                                          member_regex, members, calls)) {
                ok = 0;
                break;
            }
            statement_size = 0;
        }
        provenance_line++;
    }
    if (ok && statement_size != 0) {
        statement[statement_size] = '\0';
        ok = expanded_line_is_guarded(statement, statement_path,
                                      statement_line, dispatcher, vfs,
                                      member_regex, members, calls);
    }
    int status = pclose(pipe);
    free(rewritten_file);
    return ok && status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int compiler_expanded_poll_guard(const char *repo_root)
{
    char kernel_root[PATH_MAX];
    int written = snprintf(kernel_root, sizeof(kernel_root), "%s/kernel", repo_root);
    if (written <= 0 || (size_t)written >= sizeof(kernel_root) ||
        !kernel_tree_has_no_source_symlinks(kernel_root))
        return 0;
    char compile_db[PATH_MAX];
    if (!find_compile_database(repo_root, compile_db, sizeof(compile_db)))
        return 0;
    char owner_kernel[PATH_MAX];
    if (!compile_db_owner_kernel_root(compile_db, owner_kernel,
                                      sizeof(owner_kernel)))
        return 0;
    char canonical_kernel_root[PATH_MAX];
    if (realpath(kernel_root, canonical_kernel_root) == NULL)
        return 0;
    struct guard_state_snapshot before_snapshot;
    if (!guard_state_snapshot(repo_root, compile_db, canonical_kernel_root,
                              &before_snapshot))
        return 0;
    char *db = read_source(compile_db);
    if (!compile_db_source_inventory(db, owner_kernel, canonical_kernel_root)) {
        free(db);
        return 0;
    }

    struct canonical_path_list tracked_paths = {0};
    struct canonical_path_list dependency_paths = {0};
    struct dependency_snapshot dependency_snapshot = {0};
    if (!tracked_kernel_manifest(canonical_kernel_root, &tracked_paths)) {
        canonical_path_list_destroy(&tracked_paths);
        free(db);
        return 0;
    }
    int dependency_translation_units = 0;
    int dependency_ok = 1;
    const char *dependency_cursor = db;
    while (dependency_ok) {
        const char *command_key = strstr(dependency_cursor, "\"command\":");
        if (command_key == NULL)
            break;
        const char *object_end = strchr(command_key, '}');
        if (object_end == NULL) {
            dependency_ok = 0;
            break;
        }
        char *command = json_object_string_field(command_key, object_end,
                                                 "command");
        char *file = json_object_string_field(command_key, object_end, "file");
        if (command == NULL || file == NULL) {
            free(command);
            free(file);
            dependency_ok = 0;
            break;
        }
        size_t file_len = strlen(file);
        char *operand = compile_entry_source_operand(command_key, object_end,
                                                     command);
        bool command_c = operand != NULL && strlen(operand) > 2 &&
            strcmp(operand + strlen(operand) - 2, ".c") == 0;
        bool file_c = file_len > 2 && strcmp(file + file_len - 2, ".c") == 0;
        if (command_c || file_c) {
            char *mapped = NULL;
            if (!verified_compile_db_entry(operand, file, owner_kernel,
                                           canonical_kernel_root, &mapped) ||
                !preprocessed_translation_unit_dependencies(
                    command, mapped, owner_kernel, canonical_kernel_root,
                    &tracked_paths, &dependency_paths)) {
                dependency_ok = 0;
            }
            free(mapped);
            dependency_translation_units++;
        }
        free(operand);
        free(command);
        free(file);
        dependency_cursor = object_end + 1;
    }
    if (!dependency_ok || dependency_translation_units < 100 ||
        !dependency_snapshot_build(&dependency_snapshot, &dependency_paths)) {
        dependency_snapshot_destroy(&dependency_snapshot);
        canonical_path_list_destroy(&dependency_paths);
        canonical_path_list_destroy(&tracked_paths);
        free(db);
        return 0;
    }

    char dispatcher_path[PATH_MAX];
    char vfs_path[PATH_MAX];
    written = snprintf(dispatcher_path, sizeof(dispatcher_path),
                       "%s/kernel/kqueue/kqueue.c", canonical_kernel_root);
    if (written <= 0 || (size_t)written >= sizeof(dispatcher_path)) {
        dependency_snapshot_destroy(&dependency_snapshot);
        canonical_path_list_destroy(&dependency_paths);
        canonical_path_list_destroy(&tracked_paths);
        free(db);
        return 0;
    }
    written = snprintf(vfs_path, sizeof(vfs_path), "%s/kernel/vfs/vfs_syscall.c",
                       canonical_kernel_root);
    if (written <= 0 || (size_t)written >= sizeof(vfs_path)) {
        dependency_snapshot_destroy(&dependency_snapshot);
        canonical_path_list_destroy(&dependency_paths);
        canonical_path_list_destroy(&tracked_paths);
        free(db);
        return 0;
    }
    struct poll_source_span dispatcher, vfs;
    if (!poll_function_span(dispatcher_path,
                            "static int knote_file_poll_dispatch",
                            &dispatcher) ||
        !poll_function_span(vfs_path, "static int __vfs_poll_scan", &vfs)) {
        dependency_snapshot_destroy(&dependency_snapshot);
        canonical_path_list_destroy(&dependency_paths);
        canonical_path_list_destroy(&tracked_paths);
        free(db);
        return 0;
    }

    regex_t member_regex;
    const char *member_pattern =
        "(->|\\.)[[:space:]]*ops[[:space:]]*\\)*[[:space:]]*"
        "(->|\\.)[[:space:]]*poll([^_A-Za-z0-9]|$)";
    if (regcomp(&member_regex, member_pattern, REG_EXTENDED) != 0)
        fail("expanded poll member regex compilation failed");

    int translation_units = 0;
    int members = 0;
    int calls = 0;
    int ok = 1;
    const char *cursor = db;
    while (ok) {
        const char *command_key = strstr(cursor, "\"command\":");
        if (command_key == NULL)
            break;
        const char *object_end = strchr(command_key, '}');
        if (object_end == NULL) {
            ok = 0;
            break;
        }
        char *command = json_object_string_field(command_key, object_end,
                                                 "command");
        char *file = json_object_string_field(command_key, object_end, "file");
        if (command == NULL || file == NULL) {
            free(command);
            free(file);
            ok = 0;
            break;
        }
        size_t file_len = strlen(file);
        char *operand = compile_entry_source_operand(command_key, object_end,
                                                     command);
        bool command_c = operand != NULL && strlen(operand) > 2 &&
            strcmp(operand + strlen(operand) - 2, ".c") == 0;
        bool file_c = file_len > 2 && strcmp(file + file_len - 2, ".c") == 0;
        if (command_c || file_c) {
            char *mapped = NULL;
            if (command == NULL ||
                !verified_compile_db_entry(operand, file, owner_kernel,
                                           canonical_kernel_root, &mapped) ||
                !preprocessed_translation_unit_guard(command, mapped,
                                                       owner_kernel,
                                                       canonical_kernel_root,
                                                       &dependency_snapshot,
                                                       &dispatcher, &vfs,
                                                       &member_regex, &members,
                                                       &calls)) {
                ok = 0;
            }
            free(mapped);
            translation_units++;
        }
        free(operand);
        free(command);
        free(file);
        cursor = object_end + 1;
    }
    regfree(&member_regex);
    if (ok && !post_snapshot_test_hook(canonical_kernel_root, compile_db,
                                       &dependency_snapshot))
        ok = 0;
    if (ok && !dependency_snapshot_verify(&dependency_snapshot))
        ok = 0;
    struct guard_state_snapshot after_snapshot;
    if (ok && (!guard_state_snapshot(repo_root, compile_db, canonical_kernel_root,
                                     &after_snapshot) ||
               before_snapshot.digest != after_snapshot.digest ||
               before_snapshot.source_count != after_snapshot.source_count))
        ok = 0;
    dependency_snapshot_destroy(&dependency_snapshot);
    canonical_path_list_destroy(&dependency_paths);
    canonical_path_list_destroy(&tracked_paths);
    free(db);
    return ok && translation_units >= 100 &&
        members >= 20 && calls == 5;
}

static int static_source_gate_text(const char *text)
{
    const char *dispatch = strstr(text, "static int knote_file_poll_dispatch");
    const char *dispatch_end =
        strstr(text, "static void knote_generation_advance_locked");
    const char *file_poll = strstr(text, "f->ops->poll(f, events)");
    const char *cdev_poll =
        strstr(text, "f->cdev->ops.poll(f->cdev, events)");

    if (dispatch == NULL || dispatch_end == NULL || dispatch >= dispatch_end ||
        file_poll == NULL || cdev_poll == NULL || file_poll <= dispatch ||
        file_poll >= dispatch_end || cdev_poll <= dispatch ||
        cdev_poll >= dispatch_end)
        return 0;
    if (!direct_poll_calls_are_guarded(text))
        return 0;
    const char *assert_call =
        strstr(dispatch, "kqueue_assert_poll_unlocked(kq)");
    if (assert_call == NULL || assert_call >= dispatch_end ||
        strstr(text, "knote_file_poll_revents") != NULL ||
        count_text(text, "knote_file_poll_locked(kq, kn, &valid,") < 6)
        return 0;
    if (strstr(text, "Never carry a list cursor across that") == NULL ||
        strstr(text, "next_registration_id") == NULL ||
        strstr(text, "high_water") == NULL ||
        strstr(text, "poll_scan_cookie") != NULL)
        return 0;
    if (count_text(text, "kn->ops->event(kn, 0)") != 1 ||
        count_text(text, "kqueue_file_filter_event_unlocked(kn)") != 2)
        return 0;
    if (strstr(text, "knote_poll_snapshot_matches_locked") == NULL ||
        strstr(text, "registration_generation") == NULL ||
        strstr(text, "visible_fd_refs") == NULL ||
        strstr(text, "kq->pollers++") == NULL ||
        strstr(text, "kq->pollers--") == NULL ||
        strstr(text, "KN_DELIVERING") == NULL ||
        strstr(text, "KN_PENDING") == NULL ||
        strstr(text, "kqueue_materialize_pending_locked") == NULL)
        return 0;
    if (strstr(text, "next_registration_id == ~(uint64)0") == NULL ||
        strstr(text, "kev->data = -ENOSPC") == NULL ||
        strstr(text, "registration id overflow") != NULL ||
        strstr(text, "kqueue_graph_reaches_locked") == NULL ||
        strstr(text, "__kqueue_graph_lock") == NULL ||
        strstr(text, "ret = -ELOOP") == NULL ||
        strstr(text, "error = -EOVERFLOW") == NULL)
        return 0;
    return 1;
}

static void static_source_gate(const char *repo_root)
{
    char *text = read_kernel_sources(repo_root);
    require(static_source_gate_text(text), "real kqueue source guard failed");
    require(compiler_expanded_poll_guard(repo_root),
            "compiler-expanded poll provenance guard failed");

    size_t size = strlen(text);
    static const char *mutations[] = {
        "\nstatic int inject_kqueue_c(struct vfs_file *f) "
        "{ return f -> ops -> poll \n (f, 1); }\n",
        "\nstatic int inject_filters_c(struct vfs_file *f) "
        "{ return (f->ops->poll) (f, 1); }\n",
        "\nstatic int inject_cdev(struct vfs_file *f) "
        "{ return (f->cdev->ops . poll) \n (f->cdev, 1); }\n",
        "\nstatic int inject_deref(struct vfs_file *f) "
        "{ return (*f /* legal gap */ ->ops->poll) (f, 1); }\n",
        "\nstatic int inject_alias(struct vfs_file *f) { "
        "int (*cached)(void *, int); cached = f->ops->poll; "
        "return cached(f, 1); }\n",
        "\nstatic int inject_deref_alias(struct vfs_file *f) { "
        "int (*cached)(void *, int); cached = ((f->ops))->poll; "
        "return (*cached)(f, 1); }\n",
        "\nstatic int inject_double_deref_alias(struct vfs_file *f) { "
        "int (*cached)(void *, int); int (**alias)(void *, int); "
        "cached = f->ops->poll; alias = &cached; "
        "return (**alias)(f, 1); }\n",
        "\nstatic int inject_cdev_alias(struct vfs_file *f) { "
        "int (*cached_cdev)(void *, int); cached_cdev = f->cdev->ops . poll; "
        "return (*cached_cdev)(f->cdev, 1); }\n",
        "\nstatic int inject_cdev_double_alias(struct vfs_file *f) { "
        "int (*cached)(void *, int); int (**alias)(void *, int); "
        "cached = ((f->cdev->ops)) . poll; alias = &cached; "
        "return (**alias)(f->cdev, 1); }\n",
        "\nstatic void inject_raw_argument(struct vfs_file *f) { "
        "consume_poll(f->ops->poll); }\n",
        "\nstatic void inject_raw_cdev_argument(struct vfs_file *f) { "
        "consume_poll((f->cdev->ops).poll); }\n",
        "\nstatic unsigned long inject_raw_cast(struct vfs_file *f) { "
        "return (unsigned long)(void *)(f->ops->poll); }\n",
        "\nstatic void inject_whitespace_escape(struct vfs_file *f) { "
        "consume_poll(((f /* gap */ -> ops)) -> poll); }\n",
        "\nstatic void inject_raw_return(struct vfs_file *f) { "
        "return f->cdev->ops . poll; }\n",
    };
    for (size_t i = 0; i < sizeof(mutations) / sizeof(mutations[0]); i++) {
        size_t mutation_size = strlen(mutations[i]);
        char *synthetic = malloc(size + mutation_size + 1);
        require(synthetic != NULL, "synthetic source allocation failed");
        memcpy(synthetic, text, size);
        memcpy(synthetic + size, mutations[i], mutation_size + 1);
        require(!static_source_gate_text(synthetic),
                "source guard accepted mutated direct poll dispatch");
        free(synthetic);
    }

    static const char ignored[] =
        "\n/* f->ops->poll (f, 1); */\n"
        "static const char *poll_text = \"f->cdev->ops.poll(f->cdev, 1)\";\n";
    char *false_positive = malloc(size + sizeof(ignored));
    require(false_positive != NULL, "false-positive source allocation failed");
    memcpy(false_positive, text, size);
    memcpy(false_positive + size, ignored, sizeof(ignored));
    require(static_source_gate_text(false_positive),
            "source guard rejected comment/literal-only poll text");
    free(false_positive);

    static const char allowed[] =
        "\nstatic int cap_right(struct vfs_file *f) "
        "{ return f->ops->poll /* gap */ != NULL; }\n"
        "static int cap_left(struct vfs_file *f) "
        "{ return NULL != f->ops->poll; }\n"
        "static int cap_cdev(struct vfs_file *f) "
        "{ return f->cdev->ops.poll != NULL; }\n"
        "struct guard_fake_ops { int (*poll)(void); };\n"
        "static struct guard_fake_ops guard_designator = "
        "{ .poll = guard_poll };\n";
    char *allowed_source = malloc(size + sizeof(allowed));
    require(allowed_source != NULL, "allowed source allocation failed");
    memcpy(allowed_source, text, size);
    memcpy(allowed_source + size, allowed, sizeof(allowed));
    require(static_source_gate_text(allowed_source),
            "source guard rejected explicit capability/designator syntax");
    free(allowed_source);
    free(text);
}

static void queue_init(struct model_queue *q)
{
    memset(q, 0, sizeof(*q));
    require(pthread_mutex_init(&q->lock, NULL) == 0, "mutex init failed");
}

static void queue_destroy(struct model_queue *q)
{
    require(pthread_mutex_destroy(&q->lock) == 0, "mutex destroy failed");
}

static void file_init(struct model_file *f, int identity)
{
    memset(f, 0, sizeof(*f));
    f->identity = identity;
    atomic_init(&f->refs, 1);
    atomic_init(&f->visible, 1);
    atomic_init(&f->ready, 0);
}

static void reg_init(struct model_reg *r, struct model_file *f, uint32_t flags)
{
    memset(r, 0, sizeof(*r));
    r->ident = 7;
    r->filter = 1;
    r->mask = 0x41;
    r->flags = flags;
    r->generation = 1;
    r->enabled = 1;
    r->attached = 1;
    r->file = f;
}

static void notify(struct model_queue *q, struct model_reg *r)
{
    pthread_mutex_lock(&q->lock);
    if (!q->closed && q->reg == r && r->attached && r->enabled) {
        if (r->delivering) {
            r->pending = 1;
            if (r->queued)
                r->duplicate_while_delivering = 1;
        } else if (!r->queued) {
            r->queued = 1;
            q->nready++;
        }
    }
    pthread_mutex_unlock(&q->lock);
}

static int snapshot_begin_locked(struct model_queue *q, struct model_reg *r,
                                 struct model_snapshot *s)
{
    if (q->closed || q->reg != r || !r->enabled || !r->attached ||
        r->file == NULL)
        return 0;
    r->poll_refs++;
    atomic_fetch_add_explicit(&r->file->refs, 1, memory_order_relaxed);
    *s = (struct model_snapshot){
        .reg = r,
        .file = r->file,
        .ident = r->ident,
        .filter = r->filter,
        .mask = r->mask,
        .flags = r->flags,
        .generation = r->generation,
    };
    return 1;
}

static int snapshot_matches_locked(struct model_queue *q,
                                   const struct model_snapshot *s)
{
    struct model_reg *r = s->reg;
    return !q->closed && q->reg == r && r->enabled && r->attached &&
           r->file == s->file && r->ident == s->ident &&
           r->filter == s->filter && r->mask == s->mask &&
           r->flags == s->flags && r->generation == s->generation &&
           atomic_load_explicit(&s->file->visible, memory_order_acquire);
}

static void snapshot_release_locked(struct model_snapshot *s)
{
    require(s->reg->poll_refs > 0, "poll pin underflow");
    s->reg->poll_refs--;
    if (s->reg->poll_refs == 0 && s->reg->free_pending)
        s->reg->retired = 1;
}

static int rescan(struct model_queue *q);

static int poll_dispatch(struct model_queue *q, struct model_snapshot *s)
{
    int lock_result = pthread_mutex_trylock(&q->lock);
    require(lock_result == 0, "poll dispatched while queue lock held");
    pthread_mutex_unlock(&q->lock);

    int captured_ready = s->file->capture_ready_before_sync
        ? atomic_load_explicit(&s->file->ready, memory_order_acquire)
        : 0;
    if (active_sync != NULL) {
        pthread_barrier_wait(&active_sync->entered);
        pthread_barrier_wait(&active_sync->resume);
    }
    if (s->file->nested != NULL)
        rescan(s->file->nested);
    for (int i = 0; i < s->file->synchronous_notifies; i++)
        notify(q, s->reg);
    if (s->file->nested != NULL)
        return s->file->nested->nready != 0;
    if (s->file->capture_ready_before_sync)
        return captured_ready;
    return atomic_load_explicit(&s->file->ready, memory_order_acquire);
}

/* Caller enters and returns with q->lock held. */
static int poll_locked(struct model_queue *q, struct model_reg *r, int *valid)
{
    struct model_snapshot s;
    *valid = 0;
    if (!snapshot_begin_locked(q, r, &s))
        return 0;
    pthread_mutex_unlock(&q->lock);
    int revents = poll_dispatch(q, &s);
    atomic_fetch_sub_explicit(&s.file->refs, 1, memory_order_relaxed);
    pthread_mutex_lock(&q->lock);
    *valid = snapshot_matches_locked(q, &s);
    snapshot_release_locked(&s);
    return revents;
}

static int rescan(struct model_queue *q)
{
    pthread_mutex_lock(&q->lock);
    struct model_reg *r = q->reg;
    if (r == NULL || q->closed || !r->enabled || !r->attached || r->queued) {
        pthread_mutex_unlock(&q->lock);
        return 0;
    }
    int valid = 0;
    int ready = poll_locked(q, r, &valid);
    int activated = 0;
    if (valid) {
        if (!ready)
            r->edge_active = 0;
        else if (!(r->flags & EV_CLEAR_MODEL) || !r->edge_active) {
            if (r->flags & EV_CLEAR_MODEL)
                r->edge_active = 1;
            if (!r->queued) {
                r->queued = 1;
                q->nready++;
                activated = 1;
            }
        }
    }
    int observed = activated || (valid && r->queued);
    pthread_mutex_unlock(&q->lock);
    return observed;
}

static void model_materialize_locked(struct model_queue *q,
                                     struct model_reg *r)
{
    if (!r->pending || r->delivering || !r->enabled || !r->attached)
        return;
    r->pending = 0;
    if (!r->queued) {
        r->queued = 1;
        q->nready++;
    }
}

static int deliver(struct model_queue *q)
{
    pthread_mutex_lock(&q->lock);
    struct model_reg *r = q->reg;
    if (r == NULL || !r->queued) {
        pthread_mutex_unlock(&q->lock);
        return 0;
    }
    r->queued = 0;
    q->nready--;
    r->delivering = 1;

    int valid = 0;
    int ready = poll_locked(q, r, &valid);
    if (!valid || (!ready && !(r->flags & EV_CLEAR_MODEL))) {
        if (valid) {
            r->delivering = 0;
            model_materialize_locked(q, r);
        }
        pthread_mutex_unlock(&q->lock);
        return 0;
    }
    q->deliveries++;

    if (r->flags & EV_CLEAR_MODEL) {
        int clear_valid = 0;
        int clear_ready = poll_locked(q, r, &clear_valid);
        if (!clear_valid) {
            pthread_mutex_unlock(&q->lock);
            return 1;
        }
        r->edge_active = clear_ready != 0;
    }
    if (r->flags & EV_ONESHOT_MODEL) {
        r->generation++;
        r->enabled = 0;
        r->pending = 0;
        if (r->queued) {
            r->queued = 0;
            q->nready--;
        }
        if (r->native_oneshot) {
            r->attached = 0;
            r->deleted = 1;
            q->reg = NULL;
        }
    }
    r->delivering = 0;
    model_materialize_locked(q, r);
    pthread_mutex_unlock(&q->lock);
    return 1;
}

struct poll_thread_arg {
    struct model_queue *q;
    struct model_reg *r;
    int valid;
    int revents;
};

struct delivery_thread_arg {
    struct model_queue *q;
    int delivered;
};

static void *delivery_thread(void *opaque)
{
    struct delivery_thread_arg *arg = opaque;
    arg->delivered = deliver(arg->q);
    return NULL;
}

static void *poll_thread(void *opaque)
{
    struct poll_thread_arg *arg = opaque;
    pthread_mutex_lock(&arg->q->lock);
    arg->revents = poll_locked(arg->q, arg->r, &arg->valid);
    pthread_mutex_unlock(&arg->q->lock);
    return NULL;
}

static void sync_init(struct poll_sync *sync)
{
    require(pthread_barrier_init(&sync->entered, NULL, 2) == 0,
            "entered barrier init failed");
    require(pthread_barrier_init(&sync->resume, NULL, 2) == 0,
            "resume barrier init failed");
}

static void sync_destroy(struct poll_sync *sync)
{
    pthread_barrier_destroy(&sync->entered);
    pthread_barrier_destroy(&sync->resume);
}

static void test_synchronous_callbacks(void)
{
    struct model_queue q;
    struct model_file f;
    struct model_reg r;
    queue_init(&q);
    file_init(&f, 1);
    reg_init(&r, &f, 0);
    q.reg = &r;
    f.synchronous_notifies = 2;
    atomic_store(&f.ready, 1);

    require(rescan(&q), "synchronous poll readiness lost");
    require(q.nready == 1 && r.queued,
            "dual synchronous callbacks duplicated readiness");
    require(deliver(&q) && q.nready == 1 && r.queued,
            "deferred synchronous callback was not preserved for next wait");
    require(!r.duplicate_while_delivering,
            "synchronous callback appended a duplicate during delivery");
    queue_destroy(&q);
}

static void test_signal_during_poll(void)
{
    struct model_queue q;
    struct model_file f;
    struct model_reg r;
    struct poll_sync sync;
    struct poll_thread_arg arg = {0};
    pthread_t thread;
    queue_init(&q);
    file_init(&f, 2);
    reg_init(&r, &f, 0);
    q.reg = &r;
    sync_init(&sync);
    active_sync = &sync;
    arg.q = &q;
    arg.r = &r;
    require(pthread_create(&thread, NULL, poll_thread, &arg) == 0,
            "signal poll thread create failed");
    pthread_barrier_wait(&sync.entered);
    atomic_store(&f.ready, 1);
    notify(&q, &r);
    pthread_barrier_wait(&sync.resume);
    pthread_join(thread, NULL);
    active_sync = NULL;
    require(arg.valid && arg.revents && q.nready == 1,
            "signal timed during poll was lost");
    sync_destroy(&sync);
    queue_destroy(&q);
}

enum mutation { MUT_DEL, MUT_VISIBLE_CLOSE_REUSE, MUT_READD, MUT_DISABLE,
                MUT_MOD, MUT_CLOSE_QUEUE };

static void test_inflight_mutation(enum mutation mutation)
{
    struct model_queue q;
    struct model_file old_file, new_file;
    struct model_reg old_reg, new_reg;
    struct poll_sync sync;
    struct poll_thread_arg arg = {0};
    pthread_t thread;
    queue_init(&q);
    file_init(&old_file, 10 + mutation);
    file_init(&new_file, 100 + mutation);
    reg_init(&old_reg, &old_file, 0);
    reg_init(&new_reg, &new_file, 0);
    q.reg = &old_reg;
    atomic_store(&old_file.ready, 1);
    sync_init(&sync);
    active_sync = &sync;
    arg.q = &q;
    arg.r = &old_reg;
    require(pthread_create(&thread, NULL, poll_thread, &arg) == 0,
            "mutation poll thread create failed");
    pthread_barrier_wait(&sync.entered);

    pthread_mutex_lock(&q.lock);
    switch (mutation) {
    case MUT_DEL:
        old_reg.generation++;
        old_reg.attached = 0;
        old_reg.free_pending = 1;
        q.reg = NULL;
        break;
    case MUT_VISIBLE_CLOSE_REUSE:
        atomic_store(&old_file.visible, 0);
        break;
    case MUT_READD:
        old_reg.generation++;
        old_reg.attached = 0;
        old_reg.free_pending = 1;
        q.reg = &new_reg;
        break;
    case MUT_DISABLE:
        old_reg.generation++;
        old_reg.enabled = 0;
        break;
    case MUT_MOD:
        old_reg.generation++;
        old_reg.mask ^= 0x20;
        break;
    case MUT_CLOSE_QUEUE:
        q.closed = 1;
        old_reg.generation++;
        old_reg.attached = 0;
        old_reg.free_pending = 1;
        q.reg = NULL;
        break;
    }
    pthread_mutex_unlock(&q.lock);
    pthread_barrier_wait(&sync.resume);
    pthread_join(thread, NULL);
    active_sync = NULL;
    require(!arg.valid, "stale in-flight poll result was accepted");
    require(q.nready == 0, "stale in-flight result mutated ready count");
    if (mutation == MUT_DEL || mutation == MUT_READD ||
        mutation == MUT_CLOSE_QUEUE)
        require(old_reg.retired, "pinned detached registration was not retired");
    if (mutation == MUT_READD)
        require(q.reg == &new_reg && !new_reg.queued,
                "delete/re-add result contaminated new registration");
    if (mutation == MUT_VISIBLE_CLOSE_REUSE)
        require(old_reg.file == &old_file && new_file.identity != old_file.identity,
                "fd reuse identity setup failed");
    sync_destroy(&sync);
    queue_destroy(&q);
}

static void test_nested_readiness(void)
{
    struct model_queue inner, outer;
    struct model_file source, nested_file;
    struct model_reg inner_reg, outer_reg;
    queue_init(&inner);
    queue_init(&outer);
    file_init(&source, 30);
    file_init(&nested_file, 31);
    reg_init(&inner_reg, &source, 0);
    reg_init(&outer_reg, &nested_file, 0);
    inner.reg = &inner_reg;
    outer.reg = &outer_reg;
    source.synchronous_notifies = 1;
    atomic_store(&source.ready, 1);
    nested_file.nested = &inner;
    require(rescan(&outer), "nested kqueue readiness was not observed");
    require(inner.nready == 1 && outer.nready == 1,
            "nested readiness accounting mismatch");
    queue_destroy(&outer);
    queue_destroy(&inner);
}

static void test_delivery_modes(void)
{
    struct model_queue q;
    struct model_file f;
    struct model_reg r;

    queue_init(&q);
    file_init(&f, 40);
    reg_init(&r, &f, 0);
    q.reg = &r;
    atomic_store(&f.ready, 1);
    require(rescan(&q) && deliver(&q), "level delivery failed");
    require(rescan(&q), "level readiness did not repeat");
    queue_destroy(&q);

    queue_init(&q);
    file_init(&f, 41);
    reg_init(&r, &f, EV_CLEAR_MODEL);
    q.reg = &r;
    atomic_store(&f.ready, 1);
    require(rescan(&q), "EV_CLEAR initial readiness failed");
    f.synchronous_notifies = 2;
    require(deliver(&q), "EV_CLEAR delivery failed");
    require(!r.duplicate_while_delivering && q.nready == 1,
            "EV_CLEAR callback was duplicated or lost");
    f.synchronous_notifies = 0;
    require(deliver(&q), "EV_CLEAR deferred callback was not next-wait ready");
    require(!rescan(&q) && q.nready == 0,
            "EV_CLEAR repeated without a not-ready transition");
    atomic_store(&f.ready, 0);
    require(!rescan(&q) && !r.edge_active,
            "EV_CLEAR did not observe the clear transition");
    atomic_store(&f.ready, 1);
    require(rescan(&q), "EV_CLEAR did not rearm after transition");
    queue_destroy(&q);

    queue_init(&q);
    file_init(&f, 42);
    reg_init(&r, &f, EV_ONESHOT_MODEL);
    q.reg = &r;
    atomic_store(&f.ready, 1);
    require(rescan(&q), "oneshot initial readiness failed");
    f.synchronous_notifies = 2;
    require(deliver(&q), "oneshot delivery failed");
    require(!r.enabled && !r.pending && !r.queued && q.nready == 0 &&
                !r.duplicate_while_delivering && !rescan(&q),
            "oneshot callback requeued before disable");
    f.synchronous_notifies = 0;
    pthread_mutex_lock(&q.lock);
    r.generation++;
    r.enabled = 1;
    pthread_mutex_unlock(&q.lock);
    require(rescan(&q), "oneshot MOD rearm failed");
    queue_destroy(&q);

    queue_init(&q);
    file_init(&f, 43);
    reg_init(&r, &f, EV_ONESHOT_MODEL);
    r.native_oneshot = 1;
    q.reg = &r;
    atomic_store(&f.ready, 1);
    require(rescan(&q), "native oneshot initial readiness failed");
    f.synchronous_notifies = 2;
    require(deliver(&q), "native oneshot delivery failed");
    require(r.deleted && !r.attached && q.reg == NULL && q.nready == 0 &&
                !r.pending && !r.queued,
            "native oneshot callback escaped delete semantics");
    queue_destroy(&q);
}

struct scan_reg {
    uint64_t id;
    int present;
    int pins;
    atomic_int polls;
};

struct scan_queue {
    pthread_mutex_t lock;
    struct scan_reg *regs[8];
    int nregs;
    uint64_t next_id;
    pthread_barrier_t at_first;
    pthread_barrier_t resume_first;
};

struct scan_result {
    struct scan_queue *q;
    int first_visits;
    int peer_visits;
    int readd_visits;
};

static void *concurrent_rescan(void *opaque)
{
    struct scan_result *result = opaque;
    struct scan_queue *q = result->q;
    pthread_mutex_lock(&q->lock);
    uint64_t high_water = q->next_id;
    uint64_t cursor = 0;

    for (;;) {
        struct scan_reg *candidate = NULL;
        for (int i = 0; i < q->nregs; i++) {
            struct scan_reg *r = q->regs[i];
            if (r->present && r->id > cursor && r->id <= high_water &&
                (candidate == NULL || r->id < candidate->id))
                candidate = r;
        }
        if (candidate == NULL)
            break;
        cursor = candidate->id;
        candidate->pins++;
        pthread_mutex_unlock(&q->lock);

        atomic_fetch_add_explicit(&candidate->polls, 1, memory_order_relaxed);
        if (candidate->id == 1) {
            pthread_barrier_wait(&q->at_first);
            pthread_barrier_wait(&q->resume_first);
        }

        pthread_mutex_lock(&q->lock);
        int valid = candidate->present && candidate->id == cursor;
        candidate->pins--;
        if (valid) {
            if (candidate->id == 1)
                result->first_visits++;
            else if (candidate->id == 2)
                result->peer_visits++;
            else
                result->readd_visits++;
        }
    }
    pthread_mutex_unlock(&q->lock);
    return NULL;
}

static void test_concurrent_rescans(void)
{
    struct scan_queue q = {0};
    struct scan_reg first = {.id = 1, .present = 1};
    struct scan_reg peer = {.id = 2, .present = 1};
    struct scan_reg readd = {0};
    struct scan_result a = {.q = &q};
    struct scan_result b = {.q = &q};
    pthread_t ta, tb;
    atomic_init(&first.polls, 0);
    atomic_init(&peer.polls, 0);
    atomic_init(&readd.polls, 0);
    require(pthread_mutex_init(&q.lock, NULL) == 0, "scan mutex init failed");
    require(pthread_barrier_init(&q.at_first, NULL, 3) == 0,
            "scan first barrier init failed");
    require(pthread_barrier_init(&q.resume_first, NULL, 3) == 0,
            "scan resume barrier init failed");
    q.regs[0] = &first;
    q.regs[1] = &peer;
    q.nregs = 2;
    q.next_id = 2;
    require(pthread_create(&ta, NULL, concurrent_rescan, &a) == 0 &&
                pthread_create(&tb, NULL, concurrent_rescan, &b) == 0,
            "concurrent rescan thread creation failed");

    pthread_barrier_wait(&q.at_first);
    pthread_mutex_lock(&q.lock);
    require(first.pins == 2, "concurrent rescans did not independently pin");
    first.present = 0;             /* DEL while both polls are in flight */
    readd.id = ++q.next_id;        /* re-add is above both start high-waters */
    readd.present = 1;
    q.regs[q.nregs++] = &readd;
    pthread_mutex_unlock(&q.lock);
    pthread_barrier_wait(&q.resume_first);
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);

    require(a.first_visits == 0 && b.first_visits == 0,
            "deleted registration accepted stale scan result");
    require(a.peer_visits == 1 && b.peer_visits == 1 &&
                atomic_load(&peer.polls) == 2,
            "concurrent scan state starved or duplicated peer B");
    require(a.readd_visits == 0 && b.readd_visits == 0 &&
                atomic_load(&readd.polls) == 0,
            "re-add escaped scan-local high-water bound");
    require(first.pins == 0 && atomic_load(&first.polls) == 2,
            "deleted scan pins did not balance");
    pthread_barrier_destroy(&q.resume_first);
    pthread_barrier_destroy(&q.at_first);
    pthread_mutex_destroy(&q.lock);
}

struct fair_reg {
    int queued;
    int delivering;
    int pending;
    int enabled;
    int repeated_notifies;
    int deliveries;
};

struct fair_queue {
    struct fair_reg *ready[8];
    int nready;
};

static void fair_enqueue(struct fair_queue *q, struct fair_reg *r)
{
    if (!r->enabled)
        return;
    if (r->delivering) {
        r->pending = 1;
        return;
    }
    if (r->queued)
        return;
    require(q->nready < 8, "fair ready overflow");
    r->queued = 1;
    q->ready[q->nready++] = r;
}

static struct fair_reg *fair_wait_maxevents_one(struct fair_queue *q)
{
    if (q->nready == 0)
        return NULL;
    struct fair_reg *r = q->ready[0];
    memmove(&q->ready[0], &q->ready[1],
            (size_t)(q->nready - 1) * sizeof(q->ready[0]));
    q->nready--;
    r->queued = 0;
    r->delivering = 1;
    for (int i = 0; i < r->repeated_notifies; i++)
        fair_enqueue(q, r);
    r->deliveries++;
    r->delivering = 0;
    if (r->pending) {
        r->pending = 0;
        fair_enqueue(q, r);
    }
    return r;
}

static void test_peer_fairness(void)
{
    struct fair_queue q = {0};
    struct fair_reg hot = {.enabled = 1, .repeated_notifies = 8};
    struct fair_reg peer = {.enabled = 1};
    fair_enqueue(&q, &hot);
    fair_enqueue(&q, &peer);
    require(fair_wait_maxevents_one(&q) == &hot && hot.deliveries == 1,
            "hot source first maxevents delivery failed");
    require(q.nready == 2 && q.ready[0] == &peer && q.ready[1] == &hot,
            "deferred hot callback bypassed ready peer");
    require(fair_wait_maxevents_one(&q) == &peer && peer.deliveries == 1,
            "repeated notifications starved peer at maxevents=1");
    require(q.nready == 1 && q.ready[0] == &hot,
            "fairness ready accounting mismatch");
}

struct epoll_pair_reg {
    int enabled;
    int queued;
    int delivering;
    int pending;
    int deliveries;
};

struct epoll_pair_queue {
    struct epoll_pair_reg read_filter;
    struct epoll_pair_reg write_filter;
    struct epoll_pair_reg peer;
    struct epoll_pair_reg *ready[6];
    int nready;
};

static void epoll_pair_enqueue(struct epoll_pair_queue *q,
                               struct epoll_pair_reg *r)
{
    if (!r->enabled)
        return;
    if (r->delivering) {
        r->pending = 1;
        return;
    }
    if (!r->queued) {
        r->queued = 1;
        q->ready[q->nready++] = r;
    }
}

static void epoll_pair_remove_ready(struct epoll_pair_queue *q,
                                    struct epoll_pair_reg *target)
{
    for (int i = 0; i < q->nready; i++) {
        if (q->ready[i] != target)
            continue;
        memmove(&q->ready[i], &q->ready[i + 1],
                (size_t)(q->nready - i - 1) * sizeof(q->ready[0]));
        q->nready--;
        target->queued = 0;
        return;
    }
}

static struct epoll_pair_reg *epoll_pair_wait_one(struct epoll_pair_queue *q,
                                                   int synchronous_callbacks)
{
    if (q->nready == 0)
        return NULL;
    struct epoll_pair_reg *delivered = q->ready[0];
    epoll_pair_remove_ready(q, delivered);
    delivered->delivering = 1;
    for (int i = 0; i < synchronous_callbacks; i++) {
        epoll_pair_enqueue(q, &q->read_filter);
        epoll_pair_enqueue(q, &q->write_filter);
    }
    delivered->deliveries++;

    /* EPOLLONESHOT disables both fd filters before pending materialization. */
    q->read_filter.enabled = 0;
    q->write_filter.enabled = 0;
    q->read_filter.pending = 0;
    q->write_filter.pending = 0;
    epoll_pair_remove_ready(q, &q->read_filter);
    epoll_pair_remove_ready(q, &q->write_filter);
    delivered->delivering = 0;
    return delivered;
}

static void test_epoll_oneshot_pair(void)
{
    struct epoll_pair_queue q = {0};
    q.read_filter.enabled = 1;
    q.write_filter.enabled = 1;
    q.peer.enabled = 1;
    epoll_pair_enqueue(&q, &q.read_filter);
    epoll_pair_enqueue(&q, &q.peer);
    epoll_pair_enqueue(&q, &q.write_filter);
    require(epoll_pair_wait_one(&q, 4) == &q.read_filter,
            "epoll oneshot first filter delivery failed");
    require(!q.read_filter.enabled && !q.write_filter.enabled &&
                !q.read_filter.pending && !q.write_filter.pending &&
                q.nready == 1 && q.ready[0] == &q.peer,
            "epoll oneshot callback bypassed disable-both or peer fairness");
    require(epoll_pair_wait_one(&q, 0) == &q.peer,
            "epoll oneshot maxevents=1 starved peer");

    /* Explicit MOD rearm is the only path that permits another fd event. */
    q.read_filter.enabled = 1;
    q.write_filter.enabled = 1;
    epoll_pair_enqueue(&q, &q.read_filter);
    epoll_pair_enqueue(&q, &q.write_filter);
    require(q.nready == 2 &&
                epoll_pair_wait_one(&q, 3) == &q.read_filter &&
                q.nready == 0 && q.read_filter.deliveries == 2,
            "epoll oneshot explicit rearm semantics failed");
}

struct phase_notify_arg {
    struct model_queue *q;
    struct model_reg *r;
    pthread_barrier_t *attempt;
};

static void *phase_notify_thread(void *opaque)
{
    struct phase_notify_arg *arg = opaque;
    pthread_barrier_wait(arg->attempt);
    notify(arg->q, arg->r);
    return NULL;
}

static void test_delivery_callback_phases(void)
{
    struct model_queue q;
    struct model_file f;
    struct model_reg r;

    /* Callback after DELIVERING is set but before the stale poll dispatch. */
    queue_init(&q);
    file_init(&f, 70);
    reg_init(&r, &f, 0);
    q.reg = &r;
    r.queued = 1;
    q.nready = 1;
    pthread_mutex_lock(&q.lock);
    r.queued = 0;
    q.nready--;
    r.delivering = 1;
    pthread_mutex_unlock(&q.lock);
    notify(&q, &r);
    pthread_mutex_lock(&q.lock);
    require(r.pending && !r.queued && q.nready == 0,
            "pre-stale-poll callback appended same-drain duplicate");
    r.delivering = 0;
    model_materialize_locked(&q, &r);
    pthread_mutex_unlock(&q.lock);
    require(r.queued && q.nready == 1,
            "pre-stale-poll callback lost next-wait readiness");
    queue_destroy(&q);

    /* Callback attempts after stale revalidation while finalization owns lock. */
    queue_init(&q);
    file_init(&f, 71);
    reg_init(&r, &f, 0);
    q.reg = &r;
    r.delivering = 1;
    pthread_barrier_t attempt;
    pthread_barrier_init(&attempt, NULL, 2);
    struct phase_notify_arg arg = {.q = &q, .r = &r, .attempt = &attempt};
    pthread_t thread;
    pthread_mutex_lock(&q.lock);
    require(pthread_create(&thread, NULL, phase_notify_thread, &arg) == 0,
            "post-revalidation notify thread creation failed");
    pthread_barrier_wait(&attempt);
    r.delivering = 0;
    model_materialize_locked(&q, &r);
    pthread_mutex_unlock(&q.lock);
    pthread_join(thread, NULL);
    require(r.queued && q.nready == 1 && !r.duplicate_while_delivering,
            "post-revalidation callback duplicated or was lost");
    pthread_barrier_destroy(&attempt);
    queue_destroy(&q);

    /* Callback attempts while pending materialization owns the queue lock. */
    queue_init(&q);
    file_init(&f, 72);
    reg_init(&r, &f, 0);
    q.reg = &r;
    r.pending = 1;
    pthread_barrier_init(&attempt, NULL, 2);
    arg = (struct phase_notify_arg){.q = &q, .r = &r, .attempt = &attempt};
    pthread_mutex_lock(&q.lock);
    require(pthread_create(&thread, NULL, phase_notify_thread, &arg) == 0,
            "materialization notify thread creation failed");
    pthread_barrier_wait(&attempt);
    model_materialize_locked(&q, &r);
    require(r.queued && q.nready == 1,
            "pending materialization did not create one ready node");
    pthread_mutex_unlock(&q.lock);
    pthread_join(thread, NULL);
    require(r.queued && q.nready == 1 && !r.pending,
            "materialization callback duplicated ready accounting");
    pthread_barrier_destroy(&attempt);
    queue_destroy(&q);
}

static void test_stale_ready_callback_race(void)
{
    struct model_queue q;
    struct model_file f;
    struct model_reg r;
    struct poll_sync sync;
    struct delivery_thread_arg arg = {0};
    pthread_t thread;
    queue_init(&q);
    file_init(&f, 73);
    reg_init(&r, &f, 0);
    q.reg = &r;
    r.queued = 1;                 /* old readiness already queued */
    q.nready = 1;
    f.capture_ready_before_sync = 1;
    atomic_store(&f.ready, 0);    /* stale poll snapshot returns zero */
    sync_init(&sync);
    active_sync = &sync;
    arg.q = &q;
    require(pthread_create(&thread, NULL, delivery_thread, &arg) == 0,
            "stale delivery thread creation failed");
    pthread_barrier_wait(&sync.entered);
    atomic_store(&f.ready, 1);    /* callback becomes ready after snapshot */
    notify(&q, &r);               /* in flight while kq lock is dropped */
    pthread_barrier_wait(&sync.resume);
    pthread_join(thread, NULL);
    active_sync = NULL;
    require(arg.delivered == 0 && q.deliveries == 0,
            "stale readiness was delivered in the same drain");
    require(q.nready == 1 && r.queued && !r.pending &&
                !r.delivering && !r.duplicate_while_delivering,
            "stale callback did not become exactly one next-wait event");
    f.capture_ready_before_sync = 0;
    require(deliver(&q) == 1 && q.deliveries == 1 && q.nready == 0,
            "next-wait callback readiness was lost or duplicated");
    sync_destroy(&sync);
    queue_destroy(&q);
}

static int reserve_registration_id(uint64_t *next, uint64_t *reserved)
{
    if (*next == UINT64_MAX)
        return -ENOSPC;
    *reserved = ++*next;
    return 0;
}

static void test_registration_id_exhaustion(void)
{
    uint64_t next = UINT64_MAX - 2;
    uint64_t first = 0, second = 0, rejected = 0xfeed;
    require(reserve_registration_id(&next, &first) == 0 &&
                first == UINT64_MAX - 1,
            "near-max registration id reservation failed");
    require(reserve_registration_id(&next, &second) == 0 &&
                second == UINT64_MAX,
            "max registration id reservation failed");
    require(reserve_registration_id(&next, &rejected) == -ENOSPC &&
                reserve_registration_id(&next, &rejected) == -ENOSPC &&
                next == UINT64_MAX && rejected == 0xfeed,
            "registration id exhaustion wrapped or mutated output");

    uint64_t ids[] = {first, second};
    uint64_t cursor = 0;
    uint64_t high_water = next;
    int visits = 0;
    for (;;) {
        uint64_t candidate = 0;
        for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
            if (ids[i] > cursor && ids[i] <= high_water &&
                (candidate == 0 || ids[i] < candidate))
                candidate = ids[i];
        }
        if (candidate == 0)
            break;
        cursor = candidate;
        visits++;
        require(visits <= 2, "near-max scan failed bounded progress");
    }
    require(visits == 2 && cursor == UINT64_MAX,
            "near-max high-water scan lost ordering");
}

#define CYCLE_NODES 6
struct cycle_graph {
    int edges[CYCLE_NODES][CYCLE_NODES];
    int closed[CYCLE_NODES];
};

static int cycle_reaches(const struct cycle_graph *g, int start, int needle)
{
    int queue[CYCLE_NODES];
    int seen[CYCLE_NODES] = {0};
    int begin = 0, end = 0;
    queue[end++] = start;
    seen[start] = 1;
    while (begin < end) {
        int node = queue[begin++];
        if (node == needle)
            return 1;
        if (g->closed[node])
            continue;
        for (int child = 0; child < CYCLE_NODES; child++) {
            if (g->edges[node][child] && !seen[child]) {
                seen[child] = 1;
                require(end < CYCLE_NODES, "cycle BFS overflow");
                queue[end++] = child;
            }
        }
    }
    return 0;
}

static int cycle_admit(struct cycle_graph *g, int from, int to)
{
    if (cycle_reaches(g, to, from))
        return -ELOOP;
    g->edges[from][to] = 1;
    return 0;
}

struct cycle_close_race {
    pthread_mutex_t lock;
    pthread_barrier_t pinned;
    pthread_barrier_t resume;
    struct cycle_graph graph;
    atomic_int b_refs;
    int reaches;
};

static void *cycle_close_walk(void *opaque)
{
    struct cycle_close_race *race = opaque;
    atomic_fetch_add(&race->b_refs, 1);
    pthread_barrier_wait(&race->pinned);
    pthread_barrier_wait(&race->resume);
    pthread_mutex_lock(&race->lock);
    race->reaches = cycle_reaches(&race->graph, 1, 0);
    pthread_mutex_unlock(&race->lock);
    atomic_fetch_sub(&race->b_refs, 1);
    return NULL;
}

static void test_kqueue_cycles(void)
{
    struct cycle_graph g = {0};
    require(cycle_admit(&g, 0, 0) == -ELOOP,
            "raw kqueue self-cycle accepted");
    require(cycle_admit(&g, 0, 1) == 0 &&
                cycle_admit(&g, 1, 0) == -ELOOP,
            "raw two-node cycle accepted");

    memset(&g, 0, sizeof(g));
    require(cycle_admit(&g, 0, 1) == 0 &&
                cycle_admit(&g, 1, 2) == 0 &&
                cycle_admit(&g, 2, 3) == 0 &&
                cycle_admit(&g, 3, 0) == -ELOOP,
            "raw longer cycle accepted");
    require(cycle_admit(&g, 3, 4) == 0 && cycle_reaches(&g, 0, 4),
            "valid acyclic nested chain rejected");

    struct cycle_close_race race = {0};
    pthread_t thread;
    pthread_mutex_init(&race.lock, NULL);
    pthread_barrier_init(&race.pinned, NULL, 2);
    pthread_barrier_init(&race.resume, NULL, 2);
    atomic_init(&race.b_refs, 0);
    race.graph.edges[1][2] = 1;
    race.graph.edges[2][0] = 1;
    require(pthread_create(&thread, NULL, cycle_close_walk, &race) == 0,
            "cycle close walk thread creation failed");
    pthread_barrier_wait(&race.pinned);
    require(atomic_load(&race.b_refs) == 1,
            "cycle traversal did not pin node B");
    pthread_mutex_lock(&race.lock);
    race.graph.closed[1] = 1;
    race.graph.edges[1][2] = 0; /* concurrent close/detach removes path */
    pthread_mutex_unlock(&race.lock);
    pthread_barrier_wait(&race.resume);
    pthread_join(thread, NULL);
    require(race.reaches == 0 && atomic_load(&race.b_refs) == 0,
            "close/detach traversal lost lifetime or retained stale path");
    pthread_barrier_destroy(&race.resume);
    pthread_barrier_destroy(&race.pinned);
    pthread_mutex_destroy(&race.lock);
}

struct shared_graph_node {
    pthread_mutex_t lock;
    int numeric_identity;
    int closed;
    struct shared_graph_node **children;
    int child_count;
    int child_capacity;
    atomic_int refs;
};

struct shared_graph_context {
    pthread_barrier_t *snapshot_pinned;
    pthread_barrier_t *snapshot_resume;
    struct shared_graph_node *hook_node;
    atomic_int hook_fired;
};

static void shared_node_init(struct shared_graph_node *node, int numeric,
                             int child_capacity)
{
    memset(node, 0, sizeof(*node));
    pthread_mutex_init(&node->lock, NULL);
    node->numeric_identity = numeric;
    node->child_capacity = child_capacity;
    if (child_capacity > 0) {
        node->children = calloc((size_t)child_capacity,
                                sizeof(node->children[0]));
        require(node->children != NULL, "shared graph children allocation failed");
    }
    atomic_init(&node->refs, 1);
}

static void shared_retain(struct shared_graph_node *node)
{
    require(atomic_fetch_add(&node->refs, 1) > 0,
            "shared graph retain resurrected dead node");
}

static void shared_release_node(struct shared_graph_node *node)
{
    require(atomic_fetch_sub(&node->refs, 1) > 1,
            "shared graph reference underflow");
}

static int shared_add_edge(struct shared_graph_node *from,
                           struct shared_graph_node *to)
{
    pthread_mutex_lock(&from->lock);
    if (from->closed || from->child_count == from->child_capacity) {
        pthread_mutex_unlock(&from->lock);
        return -ENOSPC;
    }
    shared_retain(to); /* persistent edge reference */
    from->children[from->child_count++] = to;
    pthread_mutex_unlock(&from->lock);
    return 0;
}

static void shared_remove_all_edges(struct shared_graph_node *node)
{
    struct shared_graph_node *release[16];
    require(node->child_capacity <= (int)(sizeof(release) / sizeof(release[0])),
            "shared test node capacity exceeds cleanup scratch");
    pthread_mutex_lock(&node->lock);
    int count = node->child_count;
    for (int i = 0; i < count; i++)
        release[i] = node->children[i];
    node->child_count = 0;
    pthread_mutex_unlock(&node->lock);
    for (int i = 0; i < count; i++)
        shared_release_node(release[i]);
}

static void shared_node_destroy(struct shared_graph_node *node)
{
    shared_remove_all_edges(node);
    require(atomic_load(&node->refs) == 1,
            "shared graph node leaked references");
    free(node->children);
    pthread_mutex_destroy(&node->lock);
}

static void *shared_identity(void *opaque, void *reference)
{
    (void)opaque;
    return reference; /* object identity, never reusable numeric identity */
}

static void shared_release(void *opaque, void *reference)
{
    (void)opaque;
    shared_release_node(reference);
}

static int shared_snapshot(void *opaque, void *parent_reference,
                           void **children, int capacity, int *count)
{
    struct shared_graph_context *context = opaque;
    struct shared_graph_node *parent = parent_reference;
    *count = 0;
    if (context != NULL && context->hook_node == parent &&
        atomic_exchange(&context->hook_fired, 1) == 0) {
        pthread_barrier_wait(context->snapshot_pinned);
        pthread_barrier_wait(context->snapshot_resume);
    }

    pthread_mutex_lock(&parent->lock);
    if (parent->closed) {
        pthread_mutex_unlock(&parent->lock);
        return 0;
    }
    int error = 0;
    for (int i = 0; i < parent->child_count; i++) {
        if (*count == capacity) {
            error = -EOVERFLOW;
            break;
        }
        shared_retain(parent->children[i]);
        children[(*count)++] = parent->children[i];
    }
    pthread_mutex_unlock(&parent->lock);
    return error;
}

static const struct kqueue_graph_walk_ops shared_walk_ops = {
    .identity = shared_identity,
    .snapshot_children = shared_snapshot,
    .release = shared_release,
};

static int shared_reaches(struct shared_graph_context *context,
                          struct shared_graph_node *start,
                          struct shared_graph_node *needle, int capacity)
{
    void **references = calloc((size_t)capacity, sizeof(void *));
    void **scratch = calloc((size_t)capacity, sizeof(void *));
    require(references != NULL && scratch != NULL,
            "shared walker workspace allocation failed");
    struct kqueue_graph_walk_state state = {
        .references = references,
        .scratch = scratch,
        .capacity = capacity,
    };
    shared_retain(start);
    int ret = kqueue_graph_walk_reaches(&shared_walk_ops, context, &state,
                                         start, needle, -EOVERFLOW);
    kqueue_graph_walk_release_all(&shared_walk_ops, context, &state);
    free(scratch);
    free(references);
    return ret;
}

struct shared_admission_graph {
    pthread_mutex_t serialization;
};

static int shared_admit(struct shared_admission_graph *graph,
                        struct shared_graph_node *from,
                        struct shared_graph_node *to)
{
    pthread_mutex_lock(&graph->serialization);
    int reaches = shared_reaches(NULL, to, from, 64);
    int ret = reaches > 0 ? -ELOOP : reaches;
    if (ret == 0)
        ret = shared_add_edge(from, to);
    pthread_mutex_unlock(&graph->serialization);
    return ret;
}

struct shared_admit_arg {
    struct shared_admission_graph *graph;
    struct shared_graph_node *from;
    struct shared_graph_node *to;
    pthread_barrier_t *start;
    int ret;
};

static void *shared_admit_thread(void *opaque)
{
    struct shared_admit_arg *arg = opaque;
    pthread_barrier_wait(arg->start);
    arg->ret = shared_admit(arg->graph, arg->from, arg->to);
    return NULL;
}

struct shared_walk_thread_arg {
    struct shared_graph_context *context;
    struct shared_graph_node *start;
    struct shared_graph_node *needle;
    int ret;
};

static void *shared_walk_thread(void *opaque)
{
    struct shared_walk_thread_arg *arg = opaque;
    arg->ret = shared_reaches(arg->context, arg->start, arg->needle, 16);
    return NULL;
}

static void test_shared_graph_walker(void)
{
    struct shared_admission_graph graph;
    pthread_mutex_init(&graph.serialization, NULL);
    struct shared_graph_node a, b;
    shared_node_init(&a, 1, 4);
    shared_node_init(&b, 2, 4);
    pthread_barrier_t start;
    pthread_barrier_init(&start, NULL, 3);
    struct shared_admit_arg left = {
        .graph = &graph, .from = &a, .to = &b, .start = &start};
    struct shared_admit_arg right = {
        .graph = &graph, .from = &b, .to = &a, .start = &start};
    pthread_t ta, tb;
    pthread_create(&ta, NULL, shared_admit_thread, &left);
    pthread_create(&tb, NULL, shared_admit_thread, &right);
    pthread_barrier_wait(&start);
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
    require((left.ret == 0 && right.ret == -ELOOP) ||
                (right.ret == 0 && left.ret == -ELOOP),
            "serialized A/B admission did not admit exactly one edge");
    shared_remove_all_edges(&a);
    shared_remove_all_edges(&b);
    shared_node_destroy(&a);
    shared_node_destroy(&b);
    pthread_barrier_destroy(&start);
    pthread_mutex_destroy(&graph.serialization);

    /* Exact production bound: a 1025-node chain must fail closed at 1024. */
    enum { BOUND = 1024, NODE_COUNT = BOUND + 2 };
    struct shared_graph_node *nodes = calloc(NODE_COUNT, sizeof(*nodes));
    require(nodes != NULL, "overflow graph allocation failed");
    for (int i = 0; i < NODE_COUNT; i++)
        shared_node_init(&nodes[i], i, i + 1 < NODE_COUNT ? 1 : 0);
    for (int i = 0; i < NODE_COUNT - 1; i++)
        require(shared_add_edge(&nodes[i], &nodes[i + 1]) == 0,
                "overflow graph edge setup failed");
    require(shared_reaches(NULL, &nodes[0], &nodes[NODE_COUNT - 1], BOUND) ==
                -EOVERFLOW,
            "KQUEUE_GRAPH_MAX_NODES did not fail closed");
    for (int i = 0; i < NODE_COUNT; i++)
        shared_node_destroy(&nodes[i]);
    free(nodes);

    /* Pinned old object must not alias a new object reusing numeric identity. */
    struct shared_graph_node root, old_file, reused_file, target;
    shared_node_init(&root, 10, 2);
    shared_node_init(&old_file, 42, 2);
    shared_node_init(&reused_file, 42, 2);
    shared_node_init(&target, 99, 2);
    require(shared_add_edge(&root, &old_file) == 0,
            "ABA old edge setup failed");
    pthread_barrier_t pinned, resume;
    pthread_barrier_init(&pinned, NULL, 2);
    pthread_barrier_init(&resume, NULL, 2);
    struct shared_graph_context context = {
        .snapshot_pinned = &pinned,
        .snapshot_resume = &resume,
        .hook_node = &old_file,
    };
    atomic_init(&context.hook_fired, 0);
    struct shared_walk_thread_arg walk_arg = {
        .context = &context, .start = &root, .needle = &reused_file};
    pthread_t walker;
    pthread_create(&walker, NULL, shared_walk_thread, &walk_arg);
    pthread_barrier_wait(&pinned);
    shared_remove_all_edges(&root);
    pthread_mutex_lock(&old_file.lock);
    old_file.closed = 1;
    pthread_mutex_unlock(&old_file.lock);
    require(shared_add_edge(&reused_file, &target) == 0,
            "ABA reused object setup failed");
    pthread_barrier_wait(&resume);
    pthread_join(walker, NULL);
    require(walk_arg.ret == 0 && old_file.numeric_identity ==
                reused_file.numeric_identity &&
                atomic_load(&old_file.refs) == 1,
            "pinned walker confused reused numeric identity or leaked ref");
    pthread_barrier_destroy(&resume);
    pthread_barrier_destroy(&pinned);
    shared_node_destroy(&root);
    shared_node_destroy(&old_file);
    shared_node_destroy(&reused_file);
    shared_node_destroy(&target);
}

static void run_dynamic_matrix(void)
{
    test_registration_id_exhaustion();
    test_kqueue_cycles();
    test_shared_graph_walker();
    test_concurrent_rescans();
    test_synchronous_callbacks();
    test_signal_during_poll();
    test_inflight_mutation(MUT_DEL);
    test_inflight_mutation(MUT_VISIBLE_CLOSE_REUSE);
    test_inflight_mutation(MUT_READD);
    test_inflight_mutation(MUT_DISABLE);
    test_inflight_mutation(MUT_MOD);
    test_inflight_mutation(MUT_CLOSE_QUEUE);
    test_nested_readiness();
    test_delivery_modes();
    test_peer_fairness();
    test_epoll_oneshot_pair();
    test_delivery_callback_phases();
    test_stale_ready_callback_race();
}

int main(int argc, char **argv)
{
    require(argc == 2 || argc == 3,
            "usage: reducer /path/to/xv6-os [dynamic-iterations|--compiler-only]");
    if (argc == 3 && strcmp(argv[2], "--compiler-only") == 0) {
        test_no_brace_null_condition_guard();
        require(compiler_expanded_poll_guard(argv[1]),
                "compiler-expanded poll provenance guard failed");
        puts("kqueue_compiler_expanded_guard status=PASS");
        return 0;
    }
    unsigned long iterations = 1;
    if (argc == 3) {
        char *end = NULL;
        errno = 0;
        iterations = strtoul(argv[2], &end, 10);
        require(errno == 0 && end != argv[2] && *end == '\0' &&
                    iterations > 0 && iterations <= 1000000,
                "dynamic-iterations must be in [1, 1000000]");
    }

    /* Source parsing and adversarial mutations are deterministic and costly. */
    static_source_gate(argv[1]);
    for (unsigned long i = 0; i < iterations; i++)
        run_dynamic_matrix();
    puts("kqueue_poll_unlocked_matrix status=PASS "
         "concurrent_rescans=PASS highwater_mutation=PASS peer_progress=PASS "
         "id_exhaustion=PASS self_cycle=PASS two_cycle=PASS long_cycle=PASS "
         "close_detach_cycle=PASS acyclic_graph=PASS "
         "shared_walker=PASS admission_race=PASS graph_overflow=PASS "
         "walker_aba=PASS "
         "sync_notify=PASS dual_callbacks=PASS signal_timing=PASS "
         "del=PASS visible_close_fd_reuse=PASS readd=PASS disable=PASS mod=PASS "
         "kqueue_close=PASS nested=PASS level=PASS edge=PASS "
         "oneshot_callbacks=PASS ev_clear_callbacks=PASS fairness=PASS "
         "native_oneshot_delete=PASS epoll_oneshot_pair=PASS "
         "epoll_rearm=PASS oneshot_peer_fairness=PASS "
         "pre_poll_callback=PASS post_revalidate_callback=PASS "
         "materialize_callback=PASS nready_balance=PASS "
         "stale_zero_race=PASS next_wait_exactly_once=PASS "
         "all_source_guard=PASS synthetic_reject=PASS");
    printf("kqueue_poll_unlocked_stress dynamic_iterations=%lu status=PASS\n",
           iterations);
    return 0;
}

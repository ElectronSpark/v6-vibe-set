#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int show_all;

static int should_skip(const char *name) {
    return !show_all && name[0] == '.';
}

static int list_dir(const char *path) {
    DIR *dir = opendir(path);
    struct dirent *de;

    if (!dir) {
        fprintf(stderr, "ls: cannot open %s: %s\n", path, strerror(errno));
        return 1;
    }

    while ((de = readdir(dir)) != NULL) {
        if (should_skip(de->d_name))
            continue;
        puts(de->d_name);
    }
    closedir(dir);
    return 0;
}

static int list_path(const char *path) {
    struct stat st;

    if (stat(path, &st) < 0) {
        fprintf(stderr, "ls: cannot access %s: %s\n", path, strerror(errno));
        return 1;
    }
    if (S_ISDIR(st.st_mode))
        return list_dir(path);

    puts(path);
    return 0;
}

int main(int argc, char **argv) {
    int status = 0;
    int first_path = 1;
    int path_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0)
            show_all = 1;
        else if (argv[i][0] != '-')
            path_count++;
    }

    if (path_count == 0)
        return list_path(".");

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0 ||
            strcmp(argv[i], "-1") == 0)
            continue;
        if (argv[i][0] == '-') {
            fprintf(stderr, "ls: unsupported option: %s\n", argv[i]);
            status = 1;
            continue;
        }
        if (path_count > 1) {
            if (!first_path)
                putchar('\n');
            printf("%s:\n", argv[i]);
        }
        first_path = 0;
        status |= list_path(argv[i]);
    }
    return status;
}

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>

#define PIDNS_RUN_DIR "/var/run/pidns"

int list(void)
{
    struct dirent *entry;
    DIR *dir;

    dir = opendir(PIDNS_RUN_DIR);
    if (!dir)
        return EXIT_SUCCESS;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0)
            continue;
        if (strcmp(entry->d_name, "..") == 0)
            continue;
        printf("%s\n", entry->d_name);
    }
    closedir(dir);
    return EXIT_SUCCESS;
}

int add(int argc, char** argv)
{
    return EXIT_SUCCESS;
}

int exec(int argc, char** argv)
{
    return EXIT_SUCCESS;
}

int delete(int argc, char** argv)
{
    return EXIT_SUCCESS;
}

void usage(void)
{
    fputs("Usage: pidns list\n"
          "       pidns add NAME cmd ...\n"
          "       pidns exec NAME cmd ...\n"
          "       pidns delete NAME\n",
          stderr);
}

int main(int argc, char** argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage();
            return EXIT_SUCCESS;
        }
    }

    if (argc <= 1) {
        return list();
    }

    if (strcmp(argv[1], "help") == 0) {
        usage();
        return EXIT_SUCCESS;
    }

    if (strcmp(argv[1], "list") == 0) {
        return list();
    }

    if (strcmp(argv[1], "add") == 0) {
        return add(argc - 1, argv+1);
    }

    if (strcmp(argv[1], "exec") == 0) {
        return exec(argc - 1, argv+1);
    }

    if (strcmp(argv[1], "delete") == 0) {
        return delete(argc - 1, argv+1);
    }

    usage();
    return EXIT_FAILURE;
}

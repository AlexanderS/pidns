#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/param.h>

#define PIDNS_RUN_DIR "/var/run/pidns"

static int namespace_alive(const char *name)
{
    char pidns_path[MAXPATHLEN];

    snprintf(pidns_path, sizeof(pidns_path), "%s/%s/pid", PIDNS_RUN_DIR, name);
    if (access(pidns_path, F_OK) != -1) {
        return 0;
    }

    return -1;
}

static int namespace_cleanup(const char *name)
{
    char pidns_path[MAXPATHLEN];

    snprintf(pidns_path, sizeof(pidns_path), "%s/%s", PIDNS_RUN_DIR, name);
    umount2(pidns_path, MNT_DETACH);
    if (rmdir(pidns_path) < 0) {
        return errno;
    }

    return 0;
}

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

        if (namespace_alive(entry->d_name) == 0) {
            printf("%s\n", entry->d_name);
        } else {
            namespace_cleanup(entry->d_name);
        }
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
    const char *name;
    int ret;

    if (argc < 1) {
        fprintf(stderr, "No pidns name specified\n");
        return EXIT_FAILURE;
    }

    name = argv[0];
    ret = namespace_cleanup(name);
    if (ret < 0) {
        fprintf(stderr, "Cannot remove namespace \"%s\": %s\n",
                name, strerror(ret));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void usage(void)
{
    fprintf(stderr,
            "Usage: pidns list\n"
            "       pidns add NAME cmd ...\n"
            "       pidns exec NAME cmd ...\n"
            "       pidns delete NAME\n");
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

    if (argc < 2) {
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
        return add(argc - 2, argv+2);
    }

    if (strcmp(argv[1], "exec") == 0) {
        return exec(argc - 2, argv+2);
    }

    if (strcmp(argv[1], "delete") == 0) {
        return delete(argc - 2, argv+2);
    }

    usage();
    return EXIT_FAILURE;
}

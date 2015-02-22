/**
 * pidns: simple helper for managing named pid namespaces
 * Copyright (C) 2015  Alexander Sulfrian <alexander@sulfrian.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define PIDNS_RUN_DIR "/var/run/pidns"

static void continue_as_child(void)
{
    pid_t child = fork();
    int status;
    pid_t ret;

    if (child < 0) {
        fprintf(stderr, "Fork failed.\n");
        exit(EXIT_FAILURE);
    }

    /* Only the child returns */
    if (child == 0)
        return;

    for (;;) {
        ret = waitpid(child, &status, WUNTRACED);
        if ((ret == child) && (WIFSTOPPED(status))) {
            /* The child suspended so suspend us as well */
            kill(getpid(), SIGSTOP);
            kill(child, SIGCONT);
        } else {
            break;
        }
    }

    /* Return the child's exit code if possible */
    if (WIFEXITED(status)) {
        exit(WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        kill(getpid(), WTERMSIG(status));
    }
    exit(EXIT_FAILURE);
}

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

static int remount_proc(const char *name)
{
    if (umount2("/proc", MNT_DETACH) < 0) {
        fprintf(stderr, "umount of /proc failed: %s\n", strerror(errno));
        return errno;
    }
    if (mount(name, "/proc", "proc", 0, NULL) < 0) {
        fprintf(stderr, "mount of /proc failed: %s\n", strerror(errno));
        return errno;
    }

    return 0;
}

static int is_pid(const char *str)
{
	int ch;
	for (; (ch = *str); str++) {
		if (!isdigit(ch))
			return 0;
	}
	return 1;
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
    const char *name, *cmd;
    char pidns_path[MAXPATHLEN];
    int made_pidns_run_dir_mount = 0;

    if (argc < 1) {
        fprintf(stderr, "No pidns name specified\n");
        return EXIT_FAILURE;
    }
    if (argc < 2) {
        fprintf(stderr, "No command specified\n");
        return EXIT_FAILURE;
    }
    name = argv[0];
    cmd = argv[1];

    if (namespace_alive(name) < 0) {
        namespace_cleanup(name);
    }

    /* Create the base pidns directory if it doesn't exist */
    mkdir(PIDNS_RUN_DIR, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);

    /* Make it possible for network namespace mounts to propogate between
     * mount namespaces.  This makes it likely that a unmounting a network
     * namespace file in one namespace will unmount the network namespace
     * file in all namespaces allowing the network namespace to be freed
     * sooner.
     */
    while (mount("", PIDNS_RUN_DIR, "none", MS_SHARED | MS_REC, NULL)) {
        /* Fail unless we need to make the mount point */
        if (errno != EINVAL || made_pidns_run_dir_mount) {
            fprintf(stderr, "mount --make-shared %s failed: %s\n",
                    PIDNS_RUN_DIR, strerror(errno));
            return EXIT_FAILURE;
        }

        /* Upgrade PIDNS_RUN_DIR to a mount point */
        if (mount(PIDNS_RUN_DIR, PIDNS_RUN_DIR, "none", MS_BIND, NULL)) {
            fprintf(stderr, "mount --bind %s %s failed: %s\n",
                    PIDNS_RUN_DIR, PIDNS_RUN_DIR, strerror(errno));
            return EXIT_FAILURE;
        }
        made_pidns_run_dir_mount = 1;
    }

    /* Create the filesystem state */
    snprintf(pidns_path, sizeof(pidns_path), "%s/%s/", PIDNS_RUN_DIR, name);
    if (mkdir(pidns_path, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH) < 0) {
        fprintf(stderr, "Cannot not create namespace dir \"%s\": %s\n",
                pidns_path, strerror(errno));
        return EXIT_FAILURE;
    }

    /* Create the new pid namespace */
    if (unshare(CLONE_NEWPID) < 0) {
        fprintf(stderr, "unshare failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Fork! (only childs are in the new pid namespace) */
    continue_as_child();

    /* Bind the namespace directory to get the pid namespace later */
    if (mount("/proc/self/ns/", pidns_path, "none", MS_BIND, NULL) < 0) {
        fprintf(stderr, "Bind /proc/self/ns/ -> %s failed: %s\n",
                pidns_path, strerror(errno));
    }

    if (unshare(CLONE_NEWNS) < 0) {
        fprintf(stderr, "unshare failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Don't let any mounts propogate back to the parent */
    if (mount("", "/", "none", MS_SLAVE | MS_REC, NULL)) {
        fprintf(stderr, "\"mount --make-rslave /\" failed: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }

    /* Mount a version of /proc that describes the pid namespace */
    if (remount_proc(name) != 0) {
        return EXIT_FAILURE;
    }

    if (execvp(cmd, argv + 1) < 0)
        fprintf(stderr, "exec of \"%s\" failed: %s\n",
                cmd, strerror(errno));

    return EXIT_FAILURE;
}

int exec(int argc, char** argv)
{
    const char *name, *cmd;
    char pidns_path[MAXPATHLEN];
    int pidns;

    if (argc < 1) {
        fprintf(stderr, "No pidns name specified\n");
        return EXIT_FAILURE;
    }
    if (argc < 2) {
        fprintf(stderr, "No command specified\n");
        return EXIT_FAILURE;
    }
    name = argv[0];
    cmd = argv[1];

    if (namespace_alive(name) < 0) {
        namespace_cleanup(name);
    }

    /* Get the pid namespace from the bind mounted directory */
    snprintf(pidns_path, sizeof(pidns_path), "%s/%s/pid", PIDNS_RUN_DIR, name);
    pidns = open(pidns_path, O_RDONLY);
    if (pidns < 0) {
        fprintf(stderr, "Cannot open pid namespace \"%s\": %s\n",
                name, strerror(errno));
        return EXIT_FAILURE;
    }

    /* Set the target pid namespace */
    if (setns(pidns, CLONE_NEWPID) < 0) {
        fprintf(stderr, "seting the pid namespace \"%s\" failed: %s\n",
                name, strerror(errno));
        return EXIT_FAILURE;
    }

    /* Fork! (only childs are in the new pid namespace) */
    continue_as_child();

    if (unshare(CLONE_NEWNS) < 0) {
        fprintf(stderr, "unshare failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Don't let any mounts propogate back to the parent */
    if (mount("", "/", "none", MS_SLAVE | MS_REC, NULL)) {
        fprintf(stderr, "\"mount --make-rslave /\" failed: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }

    /* Mount a version of /proc that describes the pid namespace */
    if (remount_proc(name) != 0) {
        return EXIT_FAILURE;
    }

    if (execvp(cmd, argv + 1) < 0)
        fprintf(stderr, "exec of \"%s\" failed: %s\n",
                cmd, strerror(errno));

    return EXIT_FAILURE;
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

int identify(int argc, char **argv)
{
    const char *pidstr;
    int pidns;
    char pidns_path[MAXPATHLEN];
    struct stat pidst;
    DIR *dir;
    struct dirent *entry;

    if (argc < 1) {
        fprintf(stderr, "No pid specified\n");
        return EXIT_FAILURE;
    }
    if (argc > 1) {
        fprintf(stderr, "extra arguments specified\n");
        return EXIT_FAILURE;
    }
    pidstr = argv[0];

    if (!is_pid(pidstr)) {
        fprintf(stderr, "Specified string '%s' is not a pid\n",
                pidstr);
        return EXIT_FAILURE;
    }

    snprintf(pidns_path, sizeof(pidns_path), "/proc/%s/ns/pid", pidstr);
    pidns = open(pidns_path, O_RDONLY);
    if (pidns < 0) {
        fprintf(stderr, "Cannot open pid namespace: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }
    if (fstat(pidns, &pidst) < 0) {
        fprintf(stderr, "Stat of pidns failed: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }
    dir = opendir(PIDNS_RUN_DIR);
    if (!dir) {
        /* Succeed treat a missing directory as an empty directory */
        if (errno == ENOENT) {
            return EXIT_SUCCESS;
        }

        fprintf(stderr, "Failed to open directory %s:%s\n",
                PIDNS_RUN_DIR, strerror(errno));
        return EXIT_FAILURE;
    }

    while ((entry = readdir(dir))) {
        char path[MAXPATHLEN];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0)
            continue;
        if (strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(path, sizeof(path), "%s/%s/pid", PIDNS_RUN_DIR,
                 entry->d_name);

        if (stat(path, &st) != 0)
            continue;

        if ((st.st_dev == pidst.st_dev) &&
            (st.st_ino == pidst.st_ino)) {
            printf("%s\n", entry->d_name);
        }
    }
    closedir(dir);

    return EXIT_SUCCESS;
}

void usage(void)
{
    fprintf(stderr,
            "Usage: pidns list\n"
            "       pidns add NAME cmd ...\n"
            "       pidns exec NAME cmd ...\n"
            "       pidns delete NAME\n"
            "       pidns identify PID\n");
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

    if (strcmp(argv[1], "identify") == 0) {
        return identify(argc - 2, argv+2);
    }

    usage();
    return EXIT_FAILURE;
}

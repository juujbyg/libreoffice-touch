/* The few file tools the launcher needs. Inside the app sandbox only programs shipped in the click
 * (or in its data folder) may run, so /usr/bin/mkdir, cp and sleep are out of reach.
 *   office-util mkdir DIR...     like mkdir -p
 *   office-util copy SRC DST     copies the tree SRC into DST (merging; files are overwritten)
 *   office-util sleep SECONDS
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int mkdirs(const char *path) {
    char buf[PATH_MAX];
    if (snprintf(buf, sizeof buf, "%s", path) >= (int)sizeof buf) return -1;
    for (char *p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        if (mkdir(buf, 0755) < 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    return mkdir(buf, 0755) < 0 && errno != EEXIST ? -1 : 0;
}

static int copy_file(const char *src, const char *dst) {
    int in = open(src, O_RDONLY | O_CLOEXEC);
    if (in < 0) return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out < 0) { close(in); return -1; }
    char buf[65536];
    ssize_t n;
    int rc = 0;
    while ((n = read(in, buf, sizeof buf)) > 0)
        if (write(out, buf, n) != n) { rc = -1; break; }
    if (n < 0) rc = -1;
    close(in);
    if (close(out) < 0) rc = -1;
    return rc;
}

static int copy_tree(const char *src, const char *dst) {
    if (mkdirs(dst) < 0) return -1;
    DIR *d = opendir(src);
    if (!d) return -1;
    int rc = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char s[PATH_MAX], t[PATH_MAX];
        if (snprintf(s, sizeof s, "%s/%s", src, e->d_name) >= (int)sizeof s ||
            snprintf(t, sizeof t, "%s/%s", dst, e->d_name) >= (int)sizeof t) { rc = -1; continue; }
        struct stat st;
        if (lstat(s, &st) < 0) { rc = -1; continue; }
        if (S_ISDIR(st.st_mode)) { if (copy_tree(s, t) < 0) rc = -1; }
        else if (S_ISREG(st.st_mode)) { if (copy_file(s, t) < 0) rc = -1; }
    }
    closedir(d);
    return rc;
}

int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "mkdir")) {
        int rc = 0;
        for (int i = 2; i < argc; i++)
            if (mkdirs(argv[i]) < 0) { perror(argv[i]); rc = 1; }
        return rc;
    }
    if (argc == 4 && !strcmp(argv[1], "copy")) {
        if (copy_tree(argv[2], argv[3]) < 0) { fprintf(stderr, "office-util: copying %s failed\n", argv[2]); return 1; }
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "sleep")) {
        sleep((unsigned)atoi(argv[2]));
        return 0;
    }
    fprintf(stderr, "usage: office-util mkdir DIR... | copy SRC DST | sleep SECONDS\n");
    return 2;
}

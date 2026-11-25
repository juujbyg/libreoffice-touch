/* Part of libnoparent.so: keeps LibreOffice off the internet.
 *
 * Documents come from anywhere, and a document can ask LibreOffice to fetch things: linked images,
 * stylesheets, web service formulas, external data. Each request tells the sender that the document
 * was opened, from where and when, and can reach devices on the local network. LibreOffice on the
 * phone needs no internet itself (the app's start screen, a separate process, does the downloads),
 * so before soffice.bin starts this installs a seccomp filter that makes the kernel refuse every
 * internet socket for it and for anything it starts. Local (Unix) sockets for Wayland, D-Bus,
 * the keyboard and LibreOffice's own single-instance pipe still work. The filter can't be removed.
 *
 * The profile's settings (bin/office) block remote links and document macros too; this is the
 * layer that holds whatever those settings or LibreOffice's bugs would let through. If the kernel
 * can't install the filter, LibreOffice still starts and a line in last.log says so.
 * LO_SHIM_ALLOW_NETWORK=1 skips it, for tests of the settings layer alone. */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>

#if defined(__aarch64__)
#define NATIVE_ARCH AUDIT_ARCH_AARCH64
#elif defined(__x86_64__)
#define NATIVE_ARCH AUDIT_ARCH_X86_64
#else
#error "offline.c: add this architecture's AUDIT_ARCH"
#endif

#define DENY(error) (SECCOMP_RET_ERRNO | ((error) & SECCOMP_RET_DATA))

/* Refuse socket() for IPv4, IPv6 and raw packets, and io_uring (which can open sockets without
 * socket()). A call made with another architecture's numbering is refused too. */
int lo_block_network(void)
{
    struct sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, NATIVE_ARCH, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, DENY(EPERM)),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
#ifdef __NR_io_uring_setup
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_io_uring_setup, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, DENY(ENOSYS)),
#endif
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_socket, 0, 5),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_INET, 2, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_INET6, 1, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_PACKET, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, DENY(EACCES)),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog program = {.len = sizeof filter / sizeof filter[0], .filter = filter};
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
        return -1;
    return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program, 0, 0);
}

__attribute__((constructor)) static void offline(void)
{
    /* Only LibreOffice itself; its children inherit the filter. */
    if (strcmp(program_invocation_short_name, "soffice.bin") != 0)
        return;
    if (getenv("LO_SHIM_ALLOW_NETWORK")) {
        fprintf(stderr, "offline: network left open (LO_SHIM_ALLOW_NETWORK)\n");
        return;
    }
    if (lo_block_network() != 0)
        fprintf(stderr, "offline: couldn't block the network (%s); relying on the profile's settings\n", strerror(errno));
    else
        fprintf(stderr, "offline: internet sockets blocked for LibreOffice\n");
}

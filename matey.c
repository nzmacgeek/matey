/*
 * matey - getty implementation for BlueyOS
 * "G'day! Come on in!" - Bandit Heeler
 *
 * matey manages the terminal login sequence for BlueyOS. It opens the TTY,
 * displays the login banner, reads the username, and execs the login program.
 *
 * Built as i386 ELF, statically linked against musl libc by default.
 *
 * Usage:
 *   matey [tty-device]
 *
 * Examples:
 *   matey             - use inherited stdin/stdout (console)
 *   matey /dev/tty1   - open and use /dev/tty1
 *
 * Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
 * licensed by BBC Studios. This is an unofficial fan/research project with
 * no affiliation to Ludo Studio or the BBC.
 *
 * ⚠️  VIBE CODED RESEARCH PROJECT - NOT FOR PRODUCTION USE ⚠️
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define MATEY_VERSION   "0.1.0"
#define LOGIN_PROGRAM   "/sbin/login"
#define ROOT_SHELL      "/bin/bash"
#define SHELL_FALLBACK  "/bin/sh"
#define ROOT_LOGIN_USER "root"
#define ROOT_LOGIN_PASSWORD "password"
#define MAX_USERNAME    64
#define MAX_HOSTNAME    256
#define MAX_PASSWORD    512
#define ISSUE_FILE      "/etc/issue"
#define LOG_SOCKET_PATH "/run/log/yap.inbox"
#define LOG_FILE_PATH   "/var/log/system.log"
#define LOG_READY_PATH  "/run/log/yap.ready"
/* Backoff delay (µs) when stdout signals EAGAIN — avoids a busy-spin. */
#define WRITE_EAGAIN_DELAY_US   1000

/*
 * MATEY_DBG — always writes directly to the active TTY so every step is
 * visible on-screen, independent of the log daemon.  Also emits a structured
 * log entry when verbosity is sufficient (-v -v).
 *
 * NOTE: write_str() is defined later in this file; the macro is only
 *       expanded inside main() and other functions that follow write_str's
 *       definition, so no forward declaration is required.
 */
#define MATEY_DBG(fmt, ...) do { \
    char _m_dbg_[512]; \
    snprintf(_m_dbg_, sizeof(_m_dbg_), \
             "[matey dbg %s:%d] " fmt "\r\n", \
             __FILE__, __LINE__, ##__VA_ARGS__); \
    write_str(_m_dbg_); \
    log_debug("[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
} while (0)

static int g_verbose = 0;
static const char *g_log_file_name = "matey.log";

static int open_log_socket(void)
{
    struct stat st;
    char path[256];

    if (stat(LOG_SOCKET_PATH, &st) != 0 || !S_ISDIR(st.st_mode))
        return -1;

    if (snprintf(path, sizeof(path), "%s/%s", LOG_SOCKET_PATH, g_log_file_name) >= (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    return open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
}

static int write_log_payload(int fd, const char *payload, size_t len)
{
    size_t offset = 0;

    while (offset < len) {
        ssize_t written = write(fd, payload + offset, len - offset);
        if (written > 0) {
            offset += (size_t)written;
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        return -1;
    }

    return 0;
}

static int lock_log_file_fd(int fd)
{
    struct flock lock;

    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;

    while (fcntl(fd, F_SETLKW, &lock) < 0) {
        if (errno == EINTR)
            continue;
        return -1;
    }

    return 0;
}

static const char *severity_name(int severity)
{
    switch (severity) {
    case 7:
        return "debug";
    case 6:
        return "info";
    case 5:
        return "notice";
    case 4:
        return "warning";
    case 3:
        return "err";
    case 2:
        return "crit";
    default:
        return "info";
    }
}

static void write_log_file_payload(int severity, const char *message)
{
    char payload[896];
    char timestamp[32];
    char hostname[MAX_HOSTNAME];
    time_t now;
    struct tm *tm_info;
    int fd;
    int len;

    if (access(LOG_READY_PATH, F_OK) != 0 || access(LOG_FILE_PATH, F_OK) != 0)
        return;

    now = time(NULL);
    tm_info = localtime(&now);
    if (!tm_info)
        return;

    if (gethostname(hostname, sizeof(hostname)) != 0) {
        strcpy(hostname, "blueyos");
    } else {
        hostname[sizeof(hostname) - 1] = '\0';
    }

    strftime(timestamp, sizeof(timestamp), "%b %e %H:%M:%S", tm_info);
    len = snprintf(payload,
                   sizeof(payload),
                   "%s %s matey[%d]: <daemon.%s> %s\n",
                   timestamp,
                   hostname,
                   getpid(),
                   severity_name(severity),
                   message);
    if (len <= 0)
        return;
    if ((size_t)len >= sizeof(payload))
        len = (int)sizeof(payload) - 1;

    fd = open(LOG_FILE_PATH, O_WRONLY | O_APPEND);
    if (fd < 0)
        return;

    if (lock_log_file_fd(fd) < 0) {
        close(fd);
        return;
    }

    if (lseek(fd, 0, SEEK_END) < 0) {
        close(fd);
        return;
    }

    (void)write_log_payload(fd, payload, (size_t)len);
    close(fd);
}

static void send_log_message(int severity, const char *fmt, ...)
{
    char message[512];
    char payload[768];
    int fd;
    int pri;
    int len;
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    pri = (3 << 3) | severity; /* LOG_DAEMON */
    len = snprintf(payload, sizeof(payload), "<%d>matey[%d]: %s\n", pri, getpid(), message);
    if (len <= 0)
        return;
    if ((size_t)len >= sizeof(payload))
        len = (int)sizeof(payload) - 1;

    fd = open_log_socket();
    if (fd >= 0) {
        (void)write_log_payload(fd, payload, (size_t)len);
        close(fd);
    }

    write_log_file_payload(severity, message);
}

static void log_notice(const char *fmt, ...)
{
    char message[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    send_log_message(5, "%s", message);
}

static void log_info(const char *fmt, ...)
{
    char message[512];
    va_list ap;

    if (g_verbose < 1)
        return;

    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    send_log_message(6, "%s", message);
}

static void log_debug(const char *fmt, ...)
{
    char message[512];
    va_list ap;

    if (g_verbose < 2)
        return;

    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    send_log_message(7, "%s", message);
}

static void log_error(const char *fmt, ...)
{
    char message[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    send_log_message(3, "%s", message);
}

static void print_usage(const char *progname)
{
    printf(
        "Usage: %s [-v] [tty-device]\n"
        "  -v, --verbose  increase logging verbosity (repeat for debug)\n"
        "      --version  show version information\n"
        "  -h, --help     show this help\n",
        progname
    );
}

static int parse_verbosity(const char *value)
{
    char *end = NULL;
    long parsed;

    if (!value || !*value)
        return 0;

    parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0')
        return 0;
    if (parsed < 0)
        return 0;
    if (parsed > 2)
        return 2;
    return (int)parsed;
}

/* BlueyOS ASCII banner — "Where Every Boot is a New Adventure!" */
static const char BANNER[] =
    "\r\n"
    "  ____  _                    ___  ____  \r\n"
    " | __ )| |_   _  ___ _   _ / _ \\/ ___| \r\n"
    " |  _ \\| | | | |/ _ \\ | | | | | \\___ \\ \r\n"
    " | |_) | | |_| |  __/ |_| | |_| |___) |\r\n"
    " |____/|_|\\__,_|\\___|\\__, |\\___/|____/ \r\n"
    "                     |___/              \r\n"
    " Where Every Boot is a New Adventure!  \r\n"
    " matey v" MATEY_VERSION " | Bandit's Login Doorbell\r\n"
    "\r\n";

/* -------------------------------------------------------------------------
 * Terminal helpers
 * ---------------------------------------------------------------------- */

/* Save/restore terminal state so raw reads work even if termios is present. */
static struct termios saved_termios;
static int termios_saved = 0;

static void setup_terminal(void)
{
    struct termios t;

    if (tcgetattr(STDIN_FILENO, &t) != 0)
        return; /* TTY may not support termios (BlueyOS VGA console) */

    saved_termios  = t;
    termios_saved  = 1;

    /* Canonical mode with echo — suitable for reading a login name. */
    t.c_lflag |= (ICANON | ECHO | ECHOE | ECHOK);
    t.c_lflag &= ~(unsigned)ECHONL;
    t.c_iflag |= (ICRNL | IXON);
    t.c_oflag |= (OPOST | ONLCR);
    t.c_cc[VMIN]  = 1;
    t.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

static void restore_terminal(void)
{
    if (termios_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
}

/* -------------------------------------------------------------------------
 * I/O helpers
 * ---------------------------------------------------------------------- */

static void write_str(const char *s)
{
    size_t len = strlen(s);
    while (len > 0) {
        ssize_t n = write(STDOUT_FILENO, s, len);
        if (n > 0) {
            s   += (size_t)n;
            len -= (size_t)n;
            continue;
        }
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(WRITE_EAGAIN_DELAY_US);
                continue;
            }
        }
        break;
    }
}

/*
 * Read one line from stdin into buf (up to maxlen-1 chars).
 * Returns the number of characters stored (excluding the NUL terminator).
 * Handles backspace locally when the terminal does not (raw/no-echo path).
 */
static int read_line(char *buf, int maxlen)
{
    int i = 0;

    while (i < maxlen - 1) {
        unsigned char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (n == 0)
            break;
        if (c == '\n' || c == '\r') {
            if (i == 0)
                continue;
            break;
        }
        /* Backspace / DEL — erase last character if terminal doesn't */
        if (c == '\b' || c == 127) {
            if (i > 0)
                i--;
            continue;
        }
        /* Ignore other control characters */
        if (c < 0x20)
            continue;
        buf[i++] = (char)c;
    }
    buf[i] = '\0';
    return i;
}

static void secure_zero(void *ptr, size_t len)
{
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) *p++ = 0;
}

static int read_password_line(char *buf, int maxlen)
{
    struct termios old, noecho;
    int have_termios = (tcgetattr(STDIN_FILENO, &old) == 0);

    if (have_termios) {
        noecho = old;
        noecho.c_lflag &= ~(tcflag_t)(ECHO | ECHOE | ECHOK | ECHONL);
        tcsetattr(STDIN_FILENO, TCSANOW, &noecho);
    }

    int len = read_line(buf, maxlen);

    if (have_termios) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old);
    }
    write_str("\r\n");
    return len;
}

/* -------------------------------------------------------------------------
 * Optional /etc/issue display
 * ---------------------------------------------------------------------- */

static void print_issue(void)
{
    char buf[256];
    ssize_t n;
    int fd = open(ISSUE_FILE, O_RDONLY);
    if (fd < 0)
        return;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        ssize_t written = write(STDOUT_FILENO, buf, (size_t)n);
        (void)written;
    }
    close(fd);
}

/* -------------------------------------------------------------------------
 * Debug helpers
 * ---------------------------------------------------------------------- */

/* Dump the current process environment to the TTY — called just before exec. */
static void dump_env_to_tty(void)
{
    extern char **environ;
    char **ep;

    write_str("[matey dbg] environment before exec:\r\n");
    for (ep = environ; ep && *ep; ep++) {
        write_str("  ");
        write_str(*ep);
        write_str("\r\n");
    }
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    char hostname[MAX_HOSTNAME];
    char username[MAX_USERNAME];
    char prompt[MAX_HOSTNAME + 32];
    int show_banner = 1;
    const char *ttydev = NULL;
    int tty_fd = -1;

    g_verbose = parse_verbosity(getenv("VERBOSE"));

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            if (g_verbose < 2)
                g_verbose++;
            continue;
        }
        if (strcmp(argv[i], "--version") == 0) {
            printf("matey %s\n", MATEY_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (argv[i][0] == '-') {
            print_usage(argv[0]);
            return 1;
        }
        if (ttydev != NULL) {
            print_usage(argv[0]);
            return 1;
        }
        ttydev = argv[i];
    }

    /* Open the TTY device if one was specified on the command line. */
    if (ttydev != NULL) {
        tty_fd = open(ttydev, O_RDWR | O_NOCTTY);
        if (tty_fd < 0) {
            /* Non-fatal: fall back to the inherited stdio fds. */
            const char *err = strerror(errno);
            write_str("matey: cannot open ");
            write_str(ttydev);
            write_str(": ");
            write_str(err);
            write_str("\r\n");
            log_error("cannot open %s: %s", ttydev, err);
        } else {
            /* Become a session leader before claiming a controlling TTY. */
            if (setsid() < 0) {
                const char *err = strerror(errno);
                write_str("matey: setsid failed for ");
                write_str(ttydev);
                write_str(": ");
                write_str(err);
                write_str("\r\n");
                log_error("setsid failed for %s: %s", ttydev, err);
                close(tty_fd);
            } else if (ioctl(tty_fd, TIOCSCTTY, 1) < 0) {
                const char *err = strerror(errno);
                write_str("matey: cannot set controlling tty ");
                write_str(ttydev);
                write_str(": ");
                write_str(err);
                write_str("\r\n");
                log_error("cannot set controlling tty %s: %s", ttydev, err);
                close(tty_fd);
            } else {
                /* Make this TTY our controlling terminal and redirect stdio. */
                dup2(tty_fd, STDIN_FILENO);
                dup2(tty_fd, STDOUT_FILENO);
                dup2(tty_fd, STDERR_FILENO);
                if (tty_fd > STDERR_FILENO)
                    close(tty_fd);
                MATEY_DBG("TTY claimed: %s (pid=%d) — debug output now active",
                          ttydev, (int)getpid());
                log_debug("claimed controlling tty %s", ttydev);
            }
        }
    }

    /* Configure the terminal for login-name input. */
    setup_terminal();

    /* Determine hostname for the login prompt. */
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        strncpy(hostname, "blueyos", sizeof(hostname) - 1);
        hostname[sizeof(hostname) - 1] = '\0';
    }

    /* Build the prompt string once. */
    snprintf(prompt, sizeof(prompt), "%s login: ", hostname);
    log_notice("starting login service on %s", ttydev ? ttydev : "console");
    log_info("hostname=%s prompt initialised", hostname);

    /* -----------------------------------------------------------------------
     * Main login loop — display banner, read username, exec login.
     * On exec failure we loop so the console remains usable.
     * -------------------------------------------------------------------- */
    for (;;) {
        if (show_banner) {
            write_str(BANNER);
            print_issue();
            show_banner = 0;
            log_debug("displayed login banner");
        }
        write_str(prompt);

        int len;
        read_line(username, sizeof(username));
        write_str("\r\n");

        /* Trim leading whitespace. */
        char *name = username;
        while (*name == ' ' || *name == '\t')
            name++;

        /* Calculate trimmed length. */
        len = (int)strlen(name);
        if (len == 0)
            continue;

        /* Trim trailing whitespace. */
        while (len > 0 && (name[len - 1] == ' ' || name[len - 1] == '\t'))
            name[--len] = '\0';

        if (len == 0)
            continue;

        if (strcmp(name, ROOT_LOGIN_USER) == 0) {
            char password[MAX_PASSWORD];
            int pwlen;
            int pwmatched;

            MATEY_DBG("root login attempt on %s (pid=%d)",
                      ttydev ? ttydev : "console", (int)getpid());

            for (;;) {
                write_str("Password: ");
                MATEY_DBG("waiting for password input");
                pwlen = read_password_line(password, sizeof(password));
                MATEY_DBG("password read: %d chars", pwlen);
                pwmatched = (pwlen > 0 &&
                             strcmp(password, ROOT_LOGIN_PASSWORD) == 0);
                if (pwmatched) {
                    MATEY_DBG("password MATCHED — proceeding to shell");
                    break;
                }
                MATEY_DBG("password MISMATCH (received len=%d, expected len=%d)",
                          pwlen, (int)strlen(ROOT_LOGIN_PASSWORD));
                secure_zero(password, sizeof(password));
                write_str("Login incorrect.\r\n");
                sleep(1);
            }
            secure_zero(password, sizeof(password));

            restore_terminal();
            MATEY_DBG("terminal restored; setting environment");
            setenv("HOME", "/root", 1);
            setenv("SHELL", ROOT_SHELL, 1);
            setenv("USER", ROOT_LOGIN_USER, 1);
            setenv("LOGNAME", ROOT_LOGIN_USER, 1);
            setenv("TERM", "linux", 1);
            setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin", 1);
            dump_env_to_tty();

            if (chdir("/root") != 0) {
                MATEY_DBG("chdir /root FAILED (%s) — using /", strerror(errno));
                chdir("/");
            } else {
                MATEY_DBG("chdir /root: ok");
            }

            log_notice("starting root shell on %s", ttydev ? ttydev : "console");
            MATEY_DBG("execl(\"%s\", \"-bash\", NULL) — launching login shell",
                      ROOT_SHELL);
            execl(ROOT_SHELL, "-bash", (char *)NULL);
            MATEY_DBG("execl %s FAILED: %s — trying fallback %s",
                      ROOT_SHELL, strerror(errno), SHELL_FALLBACK);

            MATEY_DBG("execl(\"%s\", \"-sh\", NULL)", SHELL_FALLBACK);
            execl(SHELL_FALLBACK, "-sh", (char *)NULL);
            MATEY_DBG("execl %s FAILED: %s — no shell available",
                      SHELL_FALLBACK, strerror(errno));

            write_str("matey: exec shell failed\r\n");
            log_error("exec shell failed for root console fallback");
            setup_terminal();
            continue;
        }

        /* Restore terminal before handing over to login / shell. */
        restore_terminal();
        log_notice("handing console session to %s on %s", LOGIN_PROGRAM, ttydev ? ttydev : "console");

        /* Exec the login program, passing the username. */
        execl(LOGIN_PROGRAM, "login", name, (char *)NULL);

        /* login exec failed — try a shell as last resort. */
        write_str("matey: exec " LOGIN_PROGRAM " failed: ");
        write_str(strerror(errno));
        write_str("\r\nmatey: falling back to " SHELL_FALLBACK "\r\n");
        log_error("exec %s failed: %s", LOGIN_PROGRAM, strerror(errno));

        execl(SHELL_FALLBACK, "sh", (char *)NULL);

        /* Shell exec also failed — pause briefly, re-setup terminal and loop. */
        write_str("matey: exec " SHELL_FALLBACK " failed: ");
        write_str(strerror(errno));
        write_str("\r\n");
        log_error("exec %s failed: %s", SHELL_FALLBACK, strerror(errno));
        sleep(1);
        setup_terminal();
    }

    /* unreachable */
    return 0;
}

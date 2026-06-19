#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    pid_t *items;
    size_t len;
    size_t cap;
} PidList;

typedef struct {
    pid_t pid;
    pid_t ppid;
} ProcInfo;

typedef struct {
    ProcInfo *items;
    size_t len;
    size_t cap;
} ProcList;

typedef struct {
    char **items;
    size_t len;
    size_t cap;
} StrList;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Buffer;

typedef struct {
    char *out;
    int status;
} CmdResult;

static void die_usage(const char *argv0)
{
    fprintf(stderr, "USAGE: %s up|right|down|left\n", argv0);
    exit(1);
}

static void *xrealloc(void *ptr, size_t size)
{
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        perror("realloc");
        exit(1);
    }
    return new_ptr;
}

static char *xstrdup(const char *s)
{
    char *copy = strdup(s);
    if (!copy) {
        perror("strdup");
        exit(1);
    }
    return copy;
}

static void pid_list_push(PidList *list, pid_t pid)
{
    if (pid <= 0) {
        return;
    }
    if (list->len == list->cap) {
        list->cap = list->cap ? list->cap * 2 : 16;
        list->items = xrealloc(list->items, list->cap * sizeof(*list->items));
    }
    list->items[list->len++] = pid;
}

static bool pid_list_contains(const PidList *list, pid_t pid)
{
    for (size_t i = 0; i < list->len; i++) {
        if (list->items[i] == pid) {
            return true;
        }
    }
    return false;
}

static void pid_list_push_unique(PidList *list, pid_t pid)
{
    if (!pid_list_contains(list, pid)) {
        pid_list_push(list, pid);
    }
}

static int compare_pids(const void *a, const void *b)
{
    pid_t lhs = *(const pid_t *)a;
    pid_t rhs = *(const pid_t *)b;
    return (lhs > rhs) - (lhs < rhs);
}

static void pid_list_sort_unique(PidList *list)
{
    if (list->len == 0) {
        return;
    }

    qsort(list->items, list->len, sizeof(*list->items), compare_pids);
    size_t out = 1;
    for (size_t i = 1; i < list->len; i++) {
        if (list->items[i] != list->items[out - 1]) {
            list->items[out++] = list->items[i];
        }
    }
    list->len = out;
}

static void proc_list_push(ProcList *list, pid_t pid, pid_t ppid)
{
    if (pid <= 0) {
        return;
    }
    if (list->len == list->cap) {
        list->cap = list->cap ? list->cap * 2 : 256;
        list->items = xrealloc(list->items, list->cap * sizeof(*list->items));
    }
    list->items[list->len++] = (ProcInfo){pid, ppid};
}

static void str_list_push_unique(StrList *list, const char *s)
{
    if (!s || !*s) {
        return;
    }
    for (size_t i = 0; i < list->len; i++) {
        if (strcmp(list->items[i], s) == 0) {
            return;
        }
    }
    if (list->len == list->cap) {
        list->cap = list->cap ? list->cap * 2 : 8;
        list->items = xrealloc(list->items, list->cap * sizeof(*list->items));
    }
    list->items[list->len++] = xstrdup(s);
}

static void str_list_free(StrList *list)
{
    for (size_t i = 0; i < list->len; i++) {
        free(list->items[i]);
    }
    free(list->items);
}

static void buffer_append(Buffer *buf, const char *data, size_t len)
{
    if (buf->len + len + 1 > buf->cap) {
        while (buf->len + len + 1 > buf->cap) {
            buf->cap = buf->cap ? buf->cap * 2 : 4096;
        }
        buf->data = xrealloc(buf->data, buf->cap);
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
}

static void close_if_valid(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

static double parse_timeout_seconds(void)
{
    const char *value = getenv("VIM_NIRI_NAV_TIMEOUT");
    char *end = NULL;
    double seconds;

    if (!value || !*value) {
        return 0.1;
    }

    errno = 0;
    seconds = strtod(value, &end);
    if (errno != 0 || end == value) {
        return 0.1;
    }

    if (strcmp(end, "ms") == 0) {
        return seconds / 1000.0;
    }
    if (strcmp(end, "s") == 0 || *end == '\0') {
        return seconds;
    }

    return seconds;
}

static CmdResult run_command(char *const argv[], double timeout_seconds)
{
    int out_pipe[2] = {-1, -1};
    pid_t pid;
    Buffer out = {0};
    int status = 127;

    if (pipe(out_pipe) != 0) {
        return (CmdResult){xstrdup(""), 127};
    }

    pid = fork();
    if (pid < 0) {
        close_if_valid(out_pipe[0]);
        close_if_valid(out_pipe[1]);
        return (CmdResult){xstrdup(""), 127};
    }

    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        close(out_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        close(out_pipe[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(out_pipe[1]);

    bool child_done = false;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (true) {
        char chunk[4096];
        fd_set readfds;
        struct timeval tv = {0, 10000};
        int ready;

        if (timeout_seconds > 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = (double)(now.tv_sec - start.tv_sec) +
                (double)(now.tv_nsec - start.tv_nsec) / 1000000000.0;
            double remaining = timeout_seconds - elapsed;
            if (remaining <= 0 && !child_done) {
                struct timespec grace = {0, 10000000};
                kill(pid, SIGTERM);
                nanosleep(&grace, NULL);
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                child_done = true;
            } else if (remaining > 0 && remaining < 0.01) {
                tv.tv_sec = 0;
                tv.tv_usec = (suseconds_t)(remaining * 1000000.0);
            }
        }

        FD_ZERO(&readfds);
        FD_SET(out_pipe[0], &readfds);
        ready = select(out_pipe[0] + 1, &readfds, NULL, NULL, &tv);
        if (ready > 0 && FD_ISSET(out_pipe[0], &readfds)) {
            ssize_t n = read(out_pipe[0], chunk, sizeof(chunk));
            if (n > 0) {
                buffer_append(&out, chunk, (size_t)n);
                continue;
            }
            if (n == 0) {
                break;
            }
        }

        if (!child_done) {
            pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) {
                child_done = true;
            }
        }

        if (child_done && ready == 0) {
            ssize_t n = read(out_pipe[0], chunk, sizeof(chunk));
            if (n > 0) {
                buffer_append(&out, chunk, (size_t)n);
                continue;
            }
            break;
        }
    }

    close(out_pipe[0]);
    if (!child_done) {
        waitpid(pid, &status, 0);
    }

    if (WIFEXITED(status)) {
        status = WEXITSTATUS(status);
    } else {
        status = 128;
    }

    if (!out.data) {
        out.data = xstrdup("");
    }

    return (CmdResult){out.data, status};
}

static bool read_file_line(const char *path, char *buf, size_t len)
{
    FILE *file = fopen(path, "r");
    if (!file) {
        return false;
    }
    bool ok = fgets(buf, (int)len, file) != NULL;
    fclose(file);
    if (ok) {
        buf[strcspn(buf, "\r\n")] = '\0';
    }
    return ok;
}

static bool read_comm(pid_t pid, char *buf, size_t len)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/proc/%ld/comm", (long)pid);
    return read_file_line(path, buf, len);
}

static bool is_vim_comm(const char *comm)
{
    static regex_t regex;
    static bool compiled = false;

    if (!compiled) {
        if (regcomp(&regex, "^g?(view|n?vim?x?)(diff)?$", REG_EXTENDED | REG_ICASE | REG_NOSUB) != 0) {
            return false;
        }
        compiled = true;
    }

    return regexec(&regex, comm, 0, NULL, 0) == 0;
}

static bool read_cmdline(pid_t pid, char *buf, size_t len)
{
    char path[PATH_MAX];
    int fd;
    ssize_t n;

    snprintf(path, sizeof(path), "/proc/%ld/cmdline", (long)pid);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    n = read(fd, buf, len - 1);
    close(fd);
    if (n <= 0) {
        return false;
    }
    for (ssize_t i = 0; i < n; i++) {
        if (buf[i] == '\0') {
            buf[i] = ' ';
        }
    }
    buf[n] = '\0';
    return true;
}

static bool read_ppid(pid_t pid, pid_t *ppid)
{
    char path[PATH_MAX];
    char comm[256];
    char state;
    long parent;
    FILE *file;

    snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    file = fopen(path, "r");
    if (!file) {
        return false;
    }

    bool ok = fscanf(file, "%*d (%255[^)]) %c %ld", comm, &state, &parent) == 3;
    fclose(file);
    (void)comm;
    (void)state;
    if (ok) {
        *ppid = (pid_t)parent;
    }
    return ok;
}

static void scan_processes(ProcList *processes)
{
    DIR *dir = opendir("/proc");
    struct dirent *entry;

    if (!dir) {
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        char *end = NULL;
        long pid_long = strtol(entry->d_name, &end, 10);
        pid_t ppid;
        if (!end || *end != '\0' || pid_long <= 0) {
            continue;
        }
        if (read_ppid((pid_t)pid_long, &ppid)) {
            proc_list_push(processes, (pid_t)pid_long, ppid);
        }
    }

    closedir(dir);
}

static void collect_descendant_pids(const ProcList *processes, pid_t pid, PidList *pids)
{
    pid_list_push_unique(pids, pid);

    for (size_t cursor = 0; cursor < pids->len; cursor++) {
        pid_t parent = pids->items[cursor];
        for (size_t i = 0; i < processes->len; i++) {
            if (processes->items[i].ppid == parent) {
                pid_list_push_unique(pids, processes->items[i].pid);
            }
        }
    }
}

static bool read_tty(pid_t pid, char *buf, size_t len)
{
    char path[PATH_MAX];
    ssize_t n;

    snprintf(path, sizeof(path), "/proc/%ld/fd/0", (long)pid);
    n = readlink(path, buf, len - 1);
    if (n < 0) {
        return false;
    }
    buf[n] = '\0';
    return strncmp(buf, "/dev/pts/", 9) == 0 || strncmp(buf, "/dev/tty", 8) == 0;
}

static void collect_tree_ttys(const PidList *pids, StrList *ttys)
{
    char tty[PATH_MAX];
    for (size_t i = 0; i < pids->len; i++) {
        if (read_tty(pids->items[i], tty, sizeof(tty))) {
            str_list_push_unique(ttys, tty);
        }
    }
}

static bool pid_has_tree_tty(pid_t pid, const StrList *ttys)
{
    char tty[PATH_MAX];
    if (!read_tty(pid, tty, sizeof(tty))) {
        return false;
    }
    for (size_t i = 0; i < ttys->len; i++) {
        if (strcmp(ttys->items[i], tty) == 0) {
            return true;
        }
    }
    return false;
}

static void add_embed_children(const ProcList *processes, pid_t pid, PidList *candidates)
{
    char cmdline[4096];

    for (size_t i = 0; i < processes->len; i++) {
        if (processes->items[i].ppid == pid &&
            read_cmdline(processes->items[i].pid, cmdline, sizeof(cmdline)) &&
            strstr(cmdline, "nvim --embed")) {
            pid_list_push_unique(candidates, processes->items[i].pid);
        }
    }
}

static const char *runtime_dir(void)
{
    const char *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    return xdg_runtime_dir && *xdg_runtime_dir ? xdg_runtime_dir : "/tmp";
}

static bool parse_servername_pid(const char *name, pid_t *pid)
{
    const char *prefix = "vim-niri-nav.";
    const char *suffix = ".servername";
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    size_t name_len = strlen(name);
    char pid_buf[64];
    char *end = NULL;
    long value;

    if (name_len <= prefix_len + suffix_len ||
        strncmp(name, prefix, prefix_len) != 0 ||
        strcmp(name + name_len - suffix_len, suffix) != 0) {
        return false;
    }

    if (name_len - prefix_len - suffix_len >= sizeof(pid_buf)) {
        return false;
    }

    memcpy(pid_buf, name + prefix_len, name_len - prefix_len - suffix_len);
    pid_buf[name_len - prefix_len - suffix_len] = '\0';
    value = strtol(pid_buf, &end, 10);
    if (!end || *end != '\0' || value <= 0) {
        return false;
    }

    *pid = (pid_t)value;
    return true;
}

static void collect_vim_candidate_pids(const ProcList *processes, const PidList *tree_pids, const StrList *tree_ttys, PidList *candidates)
{
    char comm[256];
    DIR *dir;
    struct dirent *entry;

    for (size_t i = 0; i < tree_pids->len; i++) {
        pid_t pid = tree_pids->items[i];
        if (read_comm(pid, comm, sizeof(comm)) && is_vim_comm(comm)) {
            add_embed_children(processes, pid, candidates);
            pid_list_push_unique(candidates, pid);
        }
    }

    dir = opendir(runtime_dir());
    if (!dir) {
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        pid_t pid;
        char proc_path[PATH_MAX];
        if (!parse_servername_pid(entry->d_name, &pid)) {
            continue;
        }
        snprintf(proc_path, sizeof(proc_path), "/proc/%ld", (long)pid);
        if (access(proc_path, F_OK) == 0 && pid_has_tree_tty(pid, tree_ttys)) {
            pid_list_push_unique(candidates, pid);
        }
    }

    closedir(dir);
}

static char *find_nvim_server(pid_t pid)
{
    const char *roots[3];
    const char *tmpdir = getenv("TMPDIR");
    const char *user = getenv("USER");
    char tmp_user[PATH_MAX];
    char slash_tmp_user[PATH_MAX];

    snprintf(tmp_user, sizeof(tmp_user), "%s/nvim.%s", tmpdir && *tmpdir ? tmpdir : "", user && *user ? user : "");
    snprintf(slash_tmp_user, sizeof(slash_tmp_user), "/tmp/nvim.%s", user && *user ? user : "");

    roots[0] = getenv("XDG_RUNTIME_DIR");
    roots[1] = tmp_user;
    roots[2] = slash_tmp_user;

    for (size_t i = 0; i < 3; i++) {
        DIR *dir;
        struct dirent *entry;
        char prefix[64];
        const char *root = roots[i];

        if (!root || !*root) {
            continue;
        }

        dir = opendir(root);
        if (!dir) {
            continue;
        }

        snprintf(prefix, sizeof(prefix), "nvim.%ld.", (long)pid);
        while ((entry = readdir(dir)) != NULL) {
            char path[PATH_MAX];
            struct stat st;
            if (strncmp(entry->d_name, prefix, strlen(prefix)) != 0) {
                continue;
            }
            snprintf(path, sizeof(path), "%s/%s", root, entry->d_name);
            if (stat(path, &st) == 0 && S_ISSOCK(st.st_mode)) {
                closedir(dir);
                return xstrdup(path);
            }
        }

        closedir(dir);
    }

    return NULL;
}

static bool read_server_file(pid_t pid, char *program, size_t program_len, char *servername, size_t servername_len)
{
    char path[PATH_MAX];
    FILE *file;

    snprintf(path, sizeof(path), "%s/vim-niri-nav.%ld.servername", runtime_dir(), (long)pid);
    file = fopen(path, "r");
    if (!file) {
        return false;
    }
    bool ok = fscanf(file, "%63s %4095s", program, servername) == 2;
    fclose(file);
    (void)program_len;
    (void)servername_len;
    return ok;
}

static void write_server_file(pid_t pid, const char *program, const char *servername)
{
    char path[PATH_MAX];
    FILE *file;

    snprintf(path, sizeof(path), "%s/vim-niri-nav.%ld.servername", runtime_dir(), (long)pid);
    file = fopen(path, "w");
    if (!file) {
        return;
    }
    fprintf(file, "%s %s\n", program, servername);
    fclose(file);
}

static pid_t parse_focused_pid(const char *json)
{
    const char *pid_key = strstr(json, "\"pid\"");
    const char *cursor = pid_key ? pid_key + 5 : json;
    while (*cursor && *cursor != ':') {
        cursor++;
    }
    if (*cursor == ':') {
        cursor++;
    }
    while (*cursor && !isdigit((unsigned char)*cursor)) {
        cursor++;
    }
    return (pid_t)strtol(cursor, NULL, 10);
}

static CmdResult nvim_remote_expr(const char *program, const char *servername, const char *expr, double timeout_seconds)
{
    if (strcmp(program, "vim") == 0) {
        char *const argv[] = {"vim", "--servername", (char *)servername, "--remote-expr", (char *)expr, NULL};
        return run_command(argv, timeout_seconds);
    }

    if (strcmp(program, "nvim") == 0) {
        char *const argv[] = {"nvim", "--clean", "--headless", "--server", (char *)servername, "--remote-expr", (char *)expr, NULL};
        return run_command(argv, timeout_seconds);
    }

    return (CmdResult){xstrdup(""), 127};
}

static void trim_newlines(char *s)
{
    s[strcspn(s, "\r\n")] = '\0';
}

static void fallback_to_niri(const char *dir, const char *custom)
{
    pid_t pid = fork();
    if (pid == 0) {
        if (strcmp(dir, "up") == 0 || strcmp(dir, "down") == 0) {
            char action[128];
            snprintf(action, sizeof(action), "focus-window-%s%s", custom, dir);
            execlp("niri", "niri", "msg", "action", action, (char *)NULL);
        } else {
            char action[128];
            snprintf(action, sizeof(action), "focus-column-%s", dir);
            execlp("niri", "niri", "msg", "action", action, (char *)NULL);
        }
        _exit(127);
    }
    if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            exit(WEXITSTATUS(status));
        }
    }
    exit(1);
}

int main(int argc, char **argv)
{
    const char *dir;
    const char *custom = "";
    double timeout_seconds = parse_timeout_seconds();
    char expr[128];
    CmdResult focused;
    pid_t focused_pid;

    if (argc < 2) {
        die_usage(argv[0]);
    }

    dir = argv[1];
    if (strcmp(dir, "up") != 0 && strcmp(dir, "right") != 0 &&
        strcmp(dir, "down") != 0 && strcmp(dir, "left") != 0) {
        die_usage(argv[0]);
    }

    if (argc >= 3) {
        if (strcmp(argv[2], "w") == 0) {
            custom = "or-workspace-";
        } else if (strcmp(argv[2], "m") == 0) {
            custom = "or-monitor-";
        }
    }

    char *const focused_argv[] = {"niri", "msg", "--json", "focused-window", NULL};
    focused = run_command(focused_argv, 0);
    if (focused.status != 0) {
        free(focused.out);
        fallback_to_niri(dir, custom);
    }

    focused_pid = parse_focused_pid(focused.out);
    free(focused.out);
    if (focused_pid <= 0) {
        fallback_to_niri(dir, custom);
    }

    snprintf(expr, sizeof(expr), "VimNiriNav('%s', 1)", dir);

    ProcList processes = {0};
    PidList tree_pids = {0};
    StrList tree_ttys = {0};
    PidList candidates = {0};

    scan_processes(&processes);
    collect_descendant_pids(&processes, focused_pid, &tree_pids);
    collect_tree_ttys(&tree_pids, &tree_ttys);
    collect_vim_candidate_pids(&processes, &tree_pids, &tree_ttys, &candidates);
    pid_list_sort_unique(&candidates);

    for (size_t i = 0; i < candidates.len; i++) {
        pid_t vim_pid = candidates.items[i];
        char program[64] = "";
        char servername[PATH_MAX] = "";
        bool have_server = read_server_file(vim_pid, program, sizeof(program), servername, sizeof(servername));
        int remote_status = 1;

        if (have_server) {
            CmdResult remote = nvim_remote_expr(program, servername, expr, timeout_seconds);
            trim_newlines(remote.out);
            remote_status = remote.status;
            if (remote.status == 0) {
                if (strcmp(remote.out, "true") == 0) {
                    free(remote.out);
                    exit(0);
                }
                if (strcmp(remote.out, "false") == 0) {
                    free(remote.out);
                    break;
                }
            }
            free(remote.out);
        }

        if (remote_status != 0 && (!have_server || strcmp(program, "nvim") == 0)) {
            char *discovered = find_nvim_server(vim_pid);
            if (discovered) {
                CmdResult remote;
                strcpy(program, "nvim");
                snprintf(servername, sizeof(servername), "%s", discovered);
                free(discovered);

                remote = nvim_remote_expr(program, servername, expr, timeout_seconds);
                trim_newlines(remote.out);
                if (remote.status == 0) {
                    write_server_file(vim_pid, program, servername);
                    if (strcmp(remote.out, "true") == 0) {
                        free(remote.out);
                        exit(0);
                    }
                    if (strcmp(remote.out, "false") == 0) {
                        free(remote.out);
                        break;
                    }
                }
                free(remote.out);
            }
        }
    }

    free(processes.items);
    free(tree_pids.items);
    str_list_free(&tree_ttys);
    free(candidates.items);

    fallback_to_niri(dir, custom);
}

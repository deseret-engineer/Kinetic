#include "lw.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int lw_verbose;
extern char **environ;

double lw_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void lw_log(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fputs("kinetic-wall: ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
}

void lw_dbg(const char *fmt, ...) {
  if (!lw_verbose) return;
  va_list ap;
  va_start(ap, fmt);
  fputs("kinetic-wall: ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
}

char *lw_read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  size_t cap = 4096, n = 0;
  char *buf = malloc(cap);
  for (;;) {
    size_t r = fread(buf + n, 1, cap - n - 1, f);
    n += r;
    if (r == 0) break;
    if (n + 1 >= cap) buf = realloc(buf, cap *= 2);
  }
  fclose(f);
  buf[n] = 0;
  if (len) *len = n;
  return buf;
}

const char *lw_home(void) {
  const char *h = getenv("HOME");
  return h ? h : "/tmp";
}

static void xdg(char *out, const char *env, const char *fallback, const char *app, const char *rel) {
  const char *base = getenv(env);
  if (base && *base)
    snprintf(out, LW_PATH, "%s/%s%s%s", base, app, *rel ? "/" : "", rel);
  else
    snprintf(out, LW_PATH, "%s/%s/%s%s%s", lw_home(), fallback, app, *rel ? "/" : "", rel);
}

void lw_dir_config(char *out, const char *rel) { xdg(out, "XDG_CONFIG_HOME", ".config", "kinetic", rel); }
void lw_dir_state(char *out, const char *rel) { xdg(out, "XDG_STATE_HOME", ".local/state", "kinetic/wall", rel); }
void lw_dir_cache(char *out, const char *rel) { xdg(out, "XDG_CACHE_HOME", ".cache", "kinetic/wall", rel); }
void lw_dir_omarchy(char *out, const char *rel) { xdg(out, "XDG_STATE_HOME", ".local/state", "omarchy/current", rel); }

/* ~/.local/state/kinetic/signals/<name>: small files Kinetic's parts share.
 * "wall": "on <pid>" while the engine shows, for the bar (its own directory,
 * because the bar's watcher also fires for anything written beside it).
 * "motion": the workspace timing and parallax, written by `kinetic motion`. */
void lw_signal_path(char *out, const char *name) { xdg(out, "XDG_STATE_HOME", ".local/state", "kinetic/signals", name); }

void lw_socket_path(char *out) {
  const char *env = getenv("KINETIC_WALL_SOCKET");
  if (env && *env) {
    snprintf(out, LW_PATH, "%s", env);
    return;
  }
  const char *rt = getenv("XDG_RUNTIME_DIR");
  if (rt && *rt)
    snprintf(out, LW_PATH, "%s/kinetic/wall.sock", rt);
  else
    snprintf(out, LW_PATH, "/tmp/kinetic-wall-%d.sock", (int)getuid());
}

int lw_mkdir_p(const char *path) {
  char tmp[LW_PATH];
  snprintf(tmp, sizeof tmp, "%s", path);
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = 0;
      mkdir(tmp, 0755);
      *p = '/';
    }
  }
  return mkdir(tmp, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool lw_parse_hex(const char *s, float rgb[3]) {
  if (!s) return false;
  if (*s == '#') s++;
  if (strlen(s) < 6) return false;
  for (int i = 0; i < 3; i++) {
    int a = hexv(s[i * 2]), b = hexv(s[i * 2 + 1]);
    if (a < 0 || b < 0) return false;
    rgb[i] = (a * 16 + b) / 255.0f;
  }
  return true;
}

void lw_spawn(char *const argv[]) {
  pid_t pid;
  posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ); /* SIGCHLD is ignored, so no zombie */
}

/* SIGCHLD is ignored (no zombies), so there is no exit status to wait for: a
 * tiny sh prints it after the output instead, and the end of the pipe says the
 * command is done. coreutils' timeout bounds anything that hangs. */
static int spawn_piped(char *const argv[], double timeout, char *err, size_t errlen) {
  int p[2];
  if (pipe2(p, O_CLOEXEC | O_NONBLOCK) < 0) {
    snprintf(err, errlen, "cannot create a pipe");
    return -1;
  }
  char secs[16];
  snprintf(secs, sizeof secs, "%g", timeout > 0 ? timeout : 5);
  char *args[24] = {"/bin/sh", "-c", "timeout -k 1 \"$0\" \"$@\"; printf '\\036exit:%d\\n' $?", secs};
  int n = 4;
  for (int i = 0; argv[i] && n < 23; i++) args[n++] = argv[i];
  args[n] = NULL;
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_adddup2(&fa, p[1], 1);
  posix_spawn_file_actions_adddup2(&fa, p[1], 2);
  pid_t pid;
  int rc = posix_spawn(&pid, args[0], &fa, NULL, args, environ);
  posix_spawn_file_actions_destroy(&fa);
  close(p[1]);
  if (rc) {
    close(p[0]);
    snprintf(err, errlen, "%s", strerror(rc));
    return -1;
  }
  return p[0];
}

/* Reads what is there; true once the output has ended. */
static bool drain(int fd, char *out, size_t cap, size_t *n) {
  for (;;) {
    ssize_t got = read(fd, out + *n, cap - 1 - *n);
    if (got > 0) {
      *n += (size_t)got;
      if (*n >= cap - 1) memmove(out, out + cap / 2, *n -= cap / 2); /* keep the tail: the status is there */
      continue;
    }
    if (got < 0 && (errno == EAGAIN || errno == EINTR)) return false;
    return true;
  }
}

/* The exit status from the end of the output, which is trimmed in place. */
static int finish(char *out, size_t n, char **text) {
  out[n] = 0;
  int status = -1;
  char *mark = strstr(out, "\036exit:");
  if (mark) {
    status = atoi(mark + 6);
    *mark = 0;
  }
  char *s = out;
  while (*s == ' ' || *s == '\n') s++;
  for (size_t len = strlen(s); len && (s[len - 1] == '\n' || s[len - 1] == ' '); len--) s[len - 1] = 0;
  *text = s;
  return status;
}

struct run {
  int fd;
  lw_run_cb cb;
  void *data;
  size_t n;
  char out[4096];
};

static void run_read(void *data, uint32_t ev) {
  struct run *r = data;
  if (!drain(r->fd, r->out, sizeof r->out, &r->n)) return;
  char *text;
  int status = finish(r->out, r->n, &text);
  lw_loop_del(r->fd);
  close(r->fd);
  if (r->cb) r->cb(r->data, status, text);
  free(r);
}

void lw_run(char *const argv[], double timeout, lw_run_cb cb, void *data) {
  char err[128];
  int fd = spawn_piped(argv, timeout, err, sizeof err);
  if (fd < 0) {
    if (cb) cb(data, -1, err);
    return;
  }
  struct run *r = calloc(1, sizeof *r);
  r->fd = fd, r->cb = cb, r->data = data;
  lw_loop_add(r->fd, run_read, r);
}

int lw_run_wait(char *const argv[], double timeout, char *out, size_t len) {
  char buf[4096], err[128];
  size_t n = 0;
  int fd = spawn_piped(argv, timeout, err, sizeof err);
  if (fd < 0) {
    snprintf(out, len, "%s", err);
    return -1;
  }
  double end = lw_now() + timeout + 2; /* timeout itself ends it sooner */
  bool done = false;
  while (!done && lw_now() < end) {
    struct pollfd pfd = {.fd = fd, .events = POLLIN};
    if (poll(&pfd, 1, (int)((end - lw_now()) * 1000) + 1) < 0 && errno != EINTR) break;
    done = drain(fd, buf, sizeof buf, &n);
  }
  close(fd);
  char *text;
  int status = done ? finish(buf, n, &text) : 124;
  snprintf(out, len, "%s", done ? text : "no answer");
  return status;
}

float lw_clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

void lw_buf_printf(struct lw_buf *b, const char *fmt, ...) {
  for (;;) {
    va_list ap;
    va_start(ap, fmt);
    int need = vsnprintf(b->s ? b->s + b->n : NULL, b->s ? b->cap - b->n : 0, fmt, ap);
    va_end(ap);
    if (b->s && (size_t)need < b->cap - b->n) {
      b->n += (size_t)need;
      return;
    }
    b->cap = (b->cap ? b->cap * 2 : 4096) + (size_t)need;
    b->s = realloc(b->s, b->cap);
  }
}

void lw_buf_json(struct lw_buf *b, const char *str) {
  lw_buf_printf(b, "\"");
  for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
    if (*p == '"' || *p == '\\')
      lw_buf_printf(b, "\\%c", *p);
    else if (*p < 0x20)
      lw_buf_printf(b, " ");
    else
      lw_buf_printf(b, "%c", *p);
  }
  lw_buf_printf(b, "\"");
}

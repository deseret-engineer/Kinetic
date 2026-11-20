/* Hyprland module: follows the event socket and keeps each output's workspace,
 * window count and fullscreen state current. Purely event driven: if Hyprland is
 * not reachable yet when the engine starts, it tries again a bounded number of
 * times, backing off, then gives up and says so. Windows are not followed here:
 * their glass and light are drawn inside Hyprland by Kinetic's plugin (plugin.c
 * sends it the settings). */
#include "lw.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static int ev_fd = -1;
static char rbuf[8192];
static size_t rlen;
static int attempts; /* connection retries so far */
static void retry_later(const char *why);

static bool sock_path(struct sockaddr_un *addr, const char *name) {
  const char *rt = getenv("XDG_RUNTIME_DIR"), *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
  if (!rt || !sig) return false;
  memset(addr, 0, sizeof *addr);
  addr->sun_family = AF_UNIX;
  return snprintf(addr->sun_path, sizeof addr->sun_path, "%s/hypr/%s/%s", rt, sig, name) < (int)sizeof addr->sun_path;
}

char *lw_hypr_request(const char *cmd) {
  struct sockaddr_un addr;
  if (!sock_path(&addr, ".socket.sock")) return NULL;
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return NULL;
  if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0 || write(fd, cmd, strlen(cmd)) < 0) {
    close(fd);
    return NULL;
  }
  size_t cap = 16384, n = 0;
  char *buf = malloc(cap);
  ssize_t r;
  while ((r = read(fd, buf + n, cap - n - 1)) > 0) {
    n += (size_t)r;
    if (n + 1 >= cap) buf = realloc(buf, cap *= 2);
  }
  buf[n] = 0;
  close(fd);
  return buf;
}

static struct lw_output *find_output(const char *name) {
  for (struct lw_output *o = E.outputs; o; o = o->next)
    if (!strcmp(o->name, name)) return o;
  return NULL;
}

static void query(void) {
  char *mons = lw_hypr_request("j/monitors"), *wss = lw_hypr_request("j/workspaces");
  if (!mons || !wss) {
    free(mons);
    free(wss);
    return;
  }
  bool changed = false;
  for (const char *p = strchr(mons, '{'); p && *p == '{';) {
    char name[64];
    lw_json_str(lw_json_get(p, "name"), name, sizeof name);
    struct lw_output *o = find_output(name);
    const char *aw = lw_json_get(p, "activeWorkspace");
    const char *id = aw && *aw == '{' ? lw_json_get(aw, "id") : NULL;
    if (o && id) {
      int ws = atoi(id), windows = 0;
      bool fs = false;
      for (const char *w = strchr(wss, '{'); w && *w == '{';) {
        const char *wid = lw_json_get(w, "id");
        if (wid && atoi(wid) == ws) {
          const char *n = lw_json_get(w, "windows"), *f = lw_json_get(w, "hasfullscreen");
          windows = n ? atoi(n) : 0;
          fs = f && !strncmp(f, "true", 4);
          break;
        }
        w = lw_json_skip(w);
        while (*w == ',' || *w == '\n' || *w == ' ') w++;
      }
      if (ws != o->ws_id) {
        lw_motion_switch(o, ws);
        o->ws_id = ws;
        o->ws_t0 = lw_now();
        changed = true;
      }
      if (windows != o->ws_windows || fs != o->ws_fullscreen) changed = true;
      o->ws_windows = windows;
      o->ws_fullscreen = fs;
    }
    p = lw_json_skip(p);
    while (*p == ',' || *p == '\n' || *p == ' ') p++;
  }
  free(mons);
  free(wss);
  if (changed) {
    lw_engine_policy();
    lw_engine_invalidate(NULL);
  }
}

static void on_event(void *data, uint32_t ev) {
  bool need_query = false;
  for (;;) {
    ssize_t r = read(ev_fd, rbuf + rlen, sizeof rbuf - rlen - 1);
    if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR)) {
      lw_loop_del(ev_fd);
      close(ev_fd);
      ev_fd = -1;
      attempts = 0;
      retry_later("Hyprland closed its event socket"); /* if Hyprland is going away, Wayland goes too and the engine stops */
      return;
    }
    if (r < 0) break;
    rlen += (size_t)r;
    rbuf[rlen] = 0;
    char *line = rbuf, *nl;
    while ((nl = strchr(line, '\n'))) {
      *nl = 0;
      if (!strncmp(line, "workspace", 9) || !strncmp(line, "focusedmon", 10) || !strncmp(line, "fullscreen", 10) ||
          !strncmp(line, "openwindow", 10) || !strncmp(line, "closewindow", 11) || !strncmp(line, "movewindow", 10) ||
          !strncmp(line, "moveworkspace", 13) || !strncmp(line, "activespecial", 13) ||
          !strncmp(line, "destroyworkspace", 16) || !strncmp(line, "monitoradded", 12)) {
        need_query = true;
      } else if (!strncmp(line, "activewindow>>", 14)) {
        E.sys.focus_time = lw_now();
      } else if (!strncmp(line, "kinetic-glass>>ready", 20)) {
        lw_plugin_sync(true); /* the plugin was (re)loaded: it starts from nothing */
      } else if (!strncmp(line, "custom>>kinetic-travel ", 23)) {
        lw_motion_travel(line + 23); /* sent just before the first step, so it is here before that switch */
      } else if (!strncmp(line, "openlayer>>", 11) || !strncmp(line, "closelayer>>", 12)) {
        /* the shell's background or covers, owe's layer, or our own came or went:
         * see what is over Kinetic's layer now */
        const char *ns = strstr(line, ">>") + 2;
        if (!strcmp(ns, "omarchy-background") || !strcmp(ns, "owe-background") || !strcmp(ns, "kinetic-wall"))
          lw_shell_check(0.3);
      }
      line = nl + 1;
    }
    rlen = strlen(line);
    memmove(rbuf, line, rlen + 1);
    if (rlen >= sizeof rbuf - 2) rlen = 0; /* absurdly long line: drop it */
  }
  if (need_query) query();
}

/* ---------- connecting ---------- */
/* The instance comes from the environment. A service started without it (a
 * session outside uwsm, or the variable imported late) takes the newest running
 * instance under $XDG_RUNTIME_DIR/hypr, and children (omarchy-shell, hyprctl)
 * inherit it from here on. */
static bool find_instance(void) {
  if (getenv("HYPRLAND_INSTANCE_SIGNATURE")) return true;
  const char *rt = getenv("XDG_RUNTIME_DIR");
  char dir[LW_PATH], best[256] = "";
  if (!rt) return false;
  snprintf(dir, sizeof dir, "%s/hypr", rt);
  DIR *d = opendir(dir);
  if (!d) return false;
  time_t newest = 0;
  for (struct dirent *e; (e = readdir(d));) {
    char sock[LW_PATH + 300];
    struct stat st;
    if (e->d_name[0] == '.') continue;
    snprintf(sock, sizeof sock, "%s/%s/.socket2.sock", dir, e->d_name);
    if (stat(sock, &st) == 0 && S_ISSOCK(st.st_mode) && st.st_mtime >= newest) {
      newest = st.st_mtime;
      snprintf(best, sizeof best, "%s", e->d_name);
    }
  }
  closedir(d);
  if (!best[0]) return false;
  setenv("HYPRLAND_INSTANCE_SIGNATURE", best, 1);
  lw_log("hyprland: HYPRLAND_INSTANCE_SIGNATURE is not set, using the running instance %s", best);
  return true;
}

static bool on_hyprland(void) {
  const char *d = getenv("XDG_CURRENT_DESKTOP");
  return getenv("HYPRLAND_INSTANCE_SIGNATURE") || (d && strstr(d, "Hyprland"));
}

static int retry_fd = -1;
static const double backoff[] = {0.25, 0.5, 1, 2, 4, 8, 8, 15, 15}; /* about a minute in all */
#define ATTEMPTS (int)(sizeof backoff / sizeof backoff[0])

static bool try_connect(char *why, size_t len) {
  struct sockaddr_un addr;
  if (!find_instance() || !sock_path(&addr, ".socket2.sock")) {
    snprintf(why, len, "no Hyprland instance found");
    return false;
  }
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
    snprintf(why, len, "%s: %s", addr.sun_path, strerror(errno));
    if (fd >= 0) close(fd);
    return false;
  }
  ev_fd = fd;
  rlen = 0;
  fcntl(ev_fd, F_SETFL, fcntl(ev_fd, F_GETFL) | O_NONBLOCK);
  lw_loop_add(ev_fd, on_event, NULL);
  E.sys.focus_time = lw_now();
  query();
  lw_plugin_sync(true);
  for (struct lw_output *o = E.outputs; o; o = o->next) o->ws_smooth = (float)o->ws_id;
  lw_shell_check(0.2); /* layers mapped before now sent no event we could see */
  return true;
}

static void retry_fire(void *data, uint32_t ev) {
  uint64_t n;
  if (read(retry_fd, &n, sizeof n) < 0 || ev_fd >= 0) return;
  char why[LW_PATH + 64];
  if (try_connect(why, sizeof why)) {
    lw_log("hyprland: connected after %d %s", attempts, attempts == 1 ? "retry" : "retries");
    attempts = 0;
    return;
  }
  if (attempts >= ATTEMPTS) {
    lw_log("hyprland: still unreachable (%s); workspaces, window glass and keeping Kinetic above Omarchy's "
           "background are off until kinetic-wall restarts", why);
    return;
  }
  lw_debounce_arm(retry_fd, backoff[attempts++]);
}

static void retry_later(const char *why) {
  if (retry_fd < 0) {
    retry_fd = lw_debounce_fd();
    lw_loop_add(retry_fd, retry_fire, NULL);
  }
  if (!attempts) lw_log("hyprland: %s; trying again for about a minute", why);
  lw_debounce_arm(retry_fd, backoff[attempts++]);
}

bool lw_hypr_connected(void) { return ev_fd >= 0; }

static bool start(void) {
  char why[LW_PATH + 64];
  if (try_connect(why, sizeof why)) {
    lw_dbg("hyprland: connected (%s)", getenv("HYPRLAND_INSTANCE_SIGNATURE"));
    return true;
  }
  if (!on_hyprland()) {
    lw_log("hyprland: not running under Hyprland, workspace integration is off");
    return false;
  }
  retry_later(why);
  return true;
}

static void stop(void) {
  lw_plugin_off(); /* the engine is going: the plugin's glass and light, and Hyprland's blur, go back */
  if (ev_fd >= 0) close(ev_fd);
  if (retry_fd >= 0) close(retry_fd);
  ev_fd = retry_fd = -1;
}

struct lw_module lw_mod_hyprland = {"hyprland", start, stop, false};

/* Control socket: one text command per connection, one text reply. */
#include "lw.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

static int listen_fd = -1;

static int handle(char *cmd, char *out, size_t len) {
  /* Arguments are separated by 0x1f (what the CLI sends, so names may contain
   * spaces); a plain line split on whitespace also works, for socat and scripts. */
  char *argv[LW_MOTION_ARGS] = {0}; /* a motion line is the longest command */
  int argc = 0;
  cmd[strcspn(cmd, "\r\n")] = 0;
  const char *sep = strchr(cmd, '\x1f') ? "\x1f" : " \t";
  for (char *tok = strtok(cmd, sep); tok && argc < LW_MOTION_ARGS; tok = strtok(NULL, sep)) argv[argc++] = tok;
  if (!argc) return snprintf(out, len, "error: empty command\n");
  char err[1024] = "";
  const char *c = argv[0];

  if (!strcmp(c, "status")) return lw_engine_status_json(out, len);
  if (!strcmp(c, "stats")) return lw_engine_stats(out, len);
  if (!strcmp(c, "current")) return snprintf(out, len, "%s\n", E.current);
  if (!strcmp(c, "set") && argc >= 2) {
    if (!lw_engine_on()) lw_engine_set_enabled(true);
    if (!lw_engine_set(argv[1], argc > 2 ? argv[2] : NULL, err, sizeof err)) return snprintf(out, len, "error: %s\n", err);
    lw_wl_map_all(true);
    return snprintf(out, len, "ok %s\n", argv[1]);
  }
  if (!strcmp(c, "next") || !strcmp(c, "prev") || !strcmp(c, "random")) {
    if (!lw_engine_on()) lw_engine_set_enabled(true);
    int dir = !strcmp(c, "next") ? 1 : !strcmp(c, "prev") ? -1 : 0;
    if (!lw_engine_cycle(dir, err, sizeof err)) return snprintf(out, len, "error: %s\n", err);
    return snprintf(out, len, "ok %s\n", E.current);
  }
  if (!strcmp(c, "pause") || !strcmp(c, "resume") || !strcmp(c, "toggle-pause")) {
    E.manual_pause = !strcmp(c, "pause") ? true : !strcmp(c, "resume") ? false : !E.manual_pause;
    lw_engine_policy();
    return snprintf(out, len, "ok %s\n", E.manual_pause ? "paused" : "running");
  }
  if (!strcmp(c, "on") || !strcmp(c, "off") || !strcmp(c, "toggle")) {
    lw_engine_set_enabled(!strcmp(c, "on") ? true : !strcmp(c, "off") ? false : !lw_engine_on());
    return snprintf(out, len, "ok %s\n", lw_engine_on() ? "on" : E.still ? "off (still, for the glass)" : "off");
  }
  if (!strcmp(c, "param") && argc >= 3) {
    if (!lw_engine_param(argv[1], argv[2]))
      return snprintf(out, len, "error: the running wallpaper has no parameter '%s'\n", argv[1]);
    return snprintf(out, len, "ok %s = %s\n", argv[1], argv[2]);
  }
  if (!strcmp(c, "restart")) { /* start the running wallpaper over; a garden is replanted */
    int n = 0;
    for (struct lw_output *o = E.outputs; o; o = o->next)
      if (o->wp) {
        lw_wallpaper_request_restart(o->wp);
        lw_wl_schedule(o);
        n++;
      }
    return n ? snprintf(out, len, "ok\n") : snprintf(out, len, "error: no wallpaper is running\n");
  }
  if (!strcmp(c, "param-reset")) {
    lw_engine_param_reset();
    return snprintf(out, len, "ok\n");
  }
  if (!strcmp(c, "preset") && argc >= 2) {
    if (!lw_engine_preset(argv[1])) return snprintf(out, len, "error: the running wallpaper has no preset '%s'\n", argv[1]);
    return snprintf(out, len, "ok %s\n", argv[1]);
  }
  if (!strcmp(c, "params")) {
    size_t n = 0;
    for (struct lw_output *o = E.outputs; o; o = o->next) {
      if (!o->wp) continue;
      struct lw_wallpaper_info *info = lw_wallpaper_info(o->wp);
      char v[32], d[32];
      static const char *types[] = {"float", "int", "bool", "choice", "bits", "color"};
      for (int i = 0; i < info->nparams && n + 256 < len; i++) {
        struct lw_param *p = &info->params[i];
        lw_param_format(p, p->value, v);
        lw_param_format(p, p->def, d);
        n += (size_t)snprintf(out + n, len - n, "%-16s %-9s %-8s default %s", p->name, types[p->type], v, d);
        if (p->type == LW_P_CHOICE) {
          n += (size_t)snprintf(out + n, len - n, ", one of:");
          for (int k = 0; k < p->nopt; k++) n += (size_t)snprintf(out + n, len - n, " %d=%s", k, p->options[k]);
        } else if (p->type == LW_P_FLOAT || p->type == LW_P_INT) {
          n += (size_t)snprintf(out + n, len - n, ", range %g..%g", p->min, p->max);
        } else if (p->type == LW_P_BITS) {
          n += (size_t)snprintf(out + n, len - n, ", %d-bit mask", p->bits);
        }
        n += (size_t)snprintf(out + n, len - n, "\n");
      }
      for (int i = 0; i < info->npresets && n + 64 < len; i++)
        n += (size_t)snprintf(out + n, len - n, "%s%s", i ? ", " : "presets: ", info->presets[i].name);
      if (info->npresets) n += (size_t)snprintf(out + n, len - n, "\n");
      n += (size_t)snprintf(out + n, len - n, "@fps %g (default %g), @scale %g (default %g)\n", info->fps, info->def_fps,
                            info->scale, info->def_scale);
      break;
    }
    if (!n) n = (size_t)snprintf(out, len, "(no wallpaper running)\n");
    return (int)n;
  }
  if (!strcmp(c, "config")) {
    if (argc >= 3) {
      if (!lw_config_set(argv[1], strcmp(argv[2], "default") ? argv[2] : NULL, err, sizeof err))
        return snprintf(out, len, "error: %s\n", err);
      return snprintf(out, len, "ok %s\n", argv[1]);
    }
    struct lw_buf b = {0};
    lw_config_list(&b, argc == 2 ? argv[1] : NULL);
    int n = snprintf(out, len, "%s", b.s);
    free(b.s);
    return n;
  }
  if (!strcmp(c, "describe")) {
    struct lw_buf b = {0};
    lw_engine_describe(&b);
    int n = snprintf(out, len, "%s", b.s);
    free(b.s);
    return n;
  }
  if (!strcmp(c, "reload")) {
    lw_config_load();
    if (!lw_engine_reload(err, sizeof err)) return snprintf(out, len, "error: %s\n", err);
    lw_engine_apply();
    return snprintf(out, len, "ok\n");
  }
  if (!strcmp(c, "refresh")) { /* re-read Omarchy's theme right now */
    struct lw_theme t;
    lw_theme_load(&t);
    lw_engine_theme_changed(&t, true);
    return snprintf(out, len, "ok\n");
  }
  if (!strcmp(c, "lens")) return lw_lens_command(argc, argv, out, len);
  if (!strcmp(c, "motion")) return lw_motion_command(argc, argv, out, len);
  if (!strcmp(c, "quit")) {
    E.running = false;
    return snprintf(out, len, "ok\n");
  }
  return snprintf(out, len, "error: unknown command '%s'\n", c);
}

static void on_accept(void *data, uint32_t ev) {
  int fd = accept4(listen_fd, NULL, NULL, SOCK_CLOEXEC);
  if (fd < 0) return;
  /* bounded both ways, so a client that sends or reads nothing cannot hold up the frame loop */
  struct timeval rtv = {.tv_usec = 200000}, wtv = {.tv_usec = 500000};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof rtv);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &wtv, sizeof wtv);
  char cmd[4096]; /* a lens line with all 16 capsules is about 650 bytes */
  size_t n = 0;
  bool line = false;
  while (n < sizeof cmd - 1) {
    ssize_t r = read(fd, cmd + n, sizeof cmd - 1 - n);
    if (r <= 0) break;
    n += (size_t)r;
    if (memchr(cmd, '\n', n)) { line = true; break; }
  }
  cmd[n] = 0;
  static char out[262144];
  int len;
  if (!line && n == sizeof cmd - 1) /* cut off: refuse it rather than act on part of it */
    len = snprintf(out, sizeof out, "error: command longer than %zu bytes\n", sizeof cmd - 2);
  else
    len = handle(cmd, out, sizeof out);
  if (len > (int)sizeof out - 1) len = (int)sizeof out - 1;
  for (size_t done = 0; len > 0 && done < (size_t)len;) {
    ssize_t w = send(fd, out + done, (size_t)len - done, MSG_NOSIGNAL);
    if (w < 0 && errno == EINTR) continue;
    if (w <= 0) { lw_dbg("ipc write: %s", strerror(errno)); break; }
    done += (size_t)w;
  }
  close(fd);
}

static bool make_addr(struct sockaddr_un *addr) {
  char path[LW_PATH];
  lw_socket_path(path);
  memset(addr, 0, sizeof *addr);
  addr->sun_family = AF_UNIX;
  if (strlen(path) >= sizeof addr->sun_path) return false;
  strcpy(addr->sun_path, path);
  return true;
}

bool lw_ipc_init(void) {
  struct sockaddr_un addr;
  if (!make_addr(&addr)) return false;
  int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (connect(probe, (struct sockaddr *)&addr, sizeof addr) == 0) {
    close(probe);
    lw_log("another kinetic-wall daemon is already running");
    return false;
  }
  close(probe);
  char dir[LW_PATH];
  snprintf(dir, sizeof dir, "%s", addr.sun_path);
  char *slash = strrchr(dir, '/');
  if (slash && slash != dir) {
    *slash = 0;
    lw_mkdir_p(dir);
  }
  unlink(addr.sun_path);
  listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(listen_fd, 8) < 0) {
    lw_log("cannot listen on %s: %s", addr.sun_path, strerror(errno));
    return false;
  }
  lw_loop_add(listen_fd, on_accept, NULL);
  return true;
}

/* The bar watches this file to know whether the wallpaper engine is drawing,
 * so it can let the engine draw the glass instead of asking Hyprland to blur. */
void lw_ipc_publish(void) {
  char path[LW_PATH], tmp[LW_PATH + 8];
  lw_signal_path(path, "wall");
  char dir[LW_PATH];
  snprintf(dir, sizeof dir, "%s", path);
  *strrchr(dir, '/') = 0;
  lw_mkdir_p(dir);
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  /* the pid makes every start a change, so the bar re-sends its glass after a crash */
  char want[32];
  if (E.enabled && E.running) snprintf(want, sizeof want, "on %d\n", (int)getpid());
  else snprintf(want, sizeof want, "off\n");
  char *have = lw_read_file(path, NULL);
  bool same = have && !strcmp(have, want);
  free(have);
  if (same) return;
  FILE *f = fopen(tmp, "w");
  if (!f) return;
  fputs(want, f);
  fclose(f);
  rename(tmp, path);
}

void lw_ipc_fini(void) {
  if (listen_fd < 0) return;
  struct sockaddr_un addr;
  close(listen_fd);
  if (make_addr(&addr)) unlink(addr.sun_path);
  E.running = false;
  lw_ipc_publish(); /* "off": the bar takes the glass back */
}

/* Send one command; the reply lands in buf. Returns 0, 1 on an error reply, 2 if no daemon. */
int lw_ipc_query(const char *cmd, char *buf, size_t len) {
  struct sockaddr_un addr;
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (!make_addr(&addr) || connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
    close(fd);
    return 2;
  }
  dprintf(fd, "%s\n", cmd);
  size_t n = 0;
  ssize_t r;
  while (n < len - 1 && (r = read(fd, buf + n, len - 1 - n)) > 0) n += (size_t)r;
  buf[n] = 0;
  close(fd);
  return strncmp(buf, "error", 5) ? 0 : 1;
}

int lw_ipc_client(const char *cmd, bool quiet) {
  static char buf[262144];
  int rc = lw_ipc_query(cmd, buf, sizeof buf);
  if (rc == 2) {
    if (!quiet) fprintf(stderr, "kinetic-wall: the daemon is not running (start it with: systemctl --user start kinetic-wall)\n");
    return 2;
  }
  if (!quiet || rc) fputs(buf, rc ? stderr : stdout);
  return rc;
}

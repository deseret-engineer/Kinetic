#include "lw.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

struct wl_display *lw_wl_display(void);
void lw_loop_dispatch(int fd, uint32_t events);
void lw_engine_timer_fired(void *data, uint32_t ev);

static void on_signal(void *data, uint32_t ev) { E.running = false; }

static int daemon_main(void) {
  signal(SIGCHLD, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
  srand((unsigned)time(NULL));
  E.started = lw_now();
  E.epfd = epoll_create1(EPOLL_CLOEXEC);
  E.timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  lw_loop_add(E.timerfd, lw_engine_timer_fired, NULL);

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);
  sigprocmask(SIG_BLOCK, &mask, NULL);
  lw_loop_add(signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK), on_signal, NULL);

  lw_state_load();
  lw_config_load();
  lw_motion_load();
  const char *en = lw_state_get("enabled");
  E.enabled = !en || strcmp(en, "0");
  if (!E.enabled && E.cfg.glass_still && !lw_background_is_video()) E.enabled = E.still = true; /* off, but kept for the glass */
  if (!lw_ipc_init()) return 1;

  struct lw_module *mods[] = {&lw_mod_theme, &lw_mod_power, &lw_mod_shell,
#ifdef LW_AUDIO
                              &lw_mod_audio,
#endif
                              NULL};
  for (int i = 0; mods[i]; i++) mods[i]->running = mods[i]->start();

  const char *saved = lw_state_get("wallpaper");
  const char *mapped = lw_toml_str(E.cfg.doc, "themes", E.sys.theme_to.name, NULL);
  snprintf(E.current, sizeof E.current, "%s", saved ? saved : mapped ? mapped : E.cfg.wallpaper);

  E.running = true;
  if (!lw_wl_init()) {
    lw_ipc_fini();
    return 1;
  }
  lw_ipc_publish();
  lw_mod_hyprland.running = lw_mod_hyprland.start();

  struct wl_display *d = lw_wl_display();
  int wlfd = wl_display_get_fd(d);
  struct epoll_event ev = {.events = EPOLLIN, .data.fd = wlfd};
  epoll_ctl(E.epfd, EPOLL_CTL_ADD, wlfd, &ev);
  char outs[256] = "";
  for (struct lw_output *o = E.outputs; o; o = o->next)
    snprintf(outs + strlen(outs), sizeof outs - strlen(outs), "%s%s", outs[0] ? ", " : "", o->name);
  lw_log("daemon ready (wallpaper: %s, theme: %s; %s; display %s, outputs: %s; %s layer)", E.current, E.sys.theme_to.name,
         E.still ? "off, showing Omarchy's background itself for the glass (still mode)"
                 : E.enabled ? "live" : "off, Omarchy shows its own background",
         lw_wl_display_name(), outs[0] ? outs : "none yet", lw_wl_level() ? "bottom" : "background");

  int rc = 0;
  while (E.running) {
    while (wl_display_prepare_read(d) != 0) wl_display_dispatch_pending(d);
    if (wl_display_flush(d) < 0 && errno != EAGAIN) {
      wl_display_cancel_read(d);
      rc = 1;
      break;
    }
    struct epoll_event evs[16];
    int n = epoll_wait(E.epfd, evs, 16, -1);
    bool wl_ready = false;
    for (int i = 0; i < n; i++) wl_ready |= evs[i].data.fd == wlfd;
    if (wl_ready) {
      if (wl_display_read_events(d) < 0) {
        rc = 1;
        break;
      }
    } else {
      wl_display_cancel_read(d);
    }
    if (wl_display_dispatch_pending(d) < 0) {
      rc = 1;
      break;
    }
    for (int i = 0; i < n; i++)
      if (evs[i].data.fd != wlfd) lw_loop_dispatch(evs[i].data.fd, evs[i].events);
  }
  if (rc) lw_log("lost the Wayland connection");

  if (lw_mod_hyprland.running) lw_mod_hyprland.stop();
  for (int i = 0; mods[i]; i++)
    if (mods[i]->running) mods[i]->stop();
  if (!rc) lw_wl_fini();
  lw_ipc_fini();
  return rc;
}

/* ---------- client-side commands ---------- */
static void list_plain(const char *id, const char *dir, void *data) {
  struct lw_wallpaper_info info;
  char err[256];
  if (!lw_wallpaper_read_info(dir, &info, err, sizeof err)) return;
  printf("%-14s %-22s %s\n", id, info.title, info.description);
}

static void json_escape(const char *s) {
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') putchar('\\');
    putchar(*s == '\n' ? ' ' : *s);
  }
}

static void list_json(const char *id, const char *dir, void *data) {
  struct lw_wallpaper_info info;
  char err[256];
  int *count = data;
  if (!lw_wallpaper_read_info(dir, &info, err, sizeof err)) return;
  printf("%s{\"id\":\"%s\",\"name\":\"", (*count)++ ? "," : "", id);
  json_escape(info.title);
  printf("\",\"description\":\"");
  json_escape(info.description);
  printf("\",\"author\":\"");
  json_escape(info.author);
  printf("\",\"dir\":\"");
  json_escape(dir);
  printf("\",\"fps\":%g,\"scale\":%g,\"audio\":%s,\"pointer\":%s}", info.fps, info.scale, info.audio ? "true" : "false",
         info.pointer ? "true" : "false");
}

static void list_ids(const char *id, const char *dir, void *data) { puts(id); }

struct thumbs {
  int w, h, made;
  bool force;
};

static void thumb_one(const char *id, const char *dir, void *data) {
  struct thumbs *t = data;
  char out[LW_PATH], rel[96], manifest[LW_PATH + 32];
  snprintf(rel, sizeof rel, "thumbs/%s.png", id);
  lw_dir_cache(out, rel);
  snprintf(manifest, sizeof manifest, "%s/main.frag", dir);
  struct stat a, b;
  if (!t->force && stat(out, &a) == 0 && stat(manifest, &b) == 0 && a.st_mtime >= b.st_mtime) return;
  if (lw_cmd_thumbnail(id, out, t->w, t->h, -1) == 0) t->made++;
}

static void bench_one(const char *id, const char *dir, void *data) {
  int *size = data;
  lw_cmd_bench(id, size[0], size[1], 1.5, 0);
}

static int validate_rc;
static void validate_one(const char *id, const char *dir, void *data) { validate_rc |= lw_cmd_validate(id); }

static int cmd_new(const char *id) {
  char dir[LW_PATH], rel[128], path[LW_PATH + 32];
  snprintf(rel, sizeof rel, "wallpapers/%s", id);
  lw_dir_config(dir, rel);
  struct stat st;
  if (stat(dir, &st) == 0) {
    fprintf(stderr, "%s already exists\n", dir);
    return 1;
  }
  lw_mkdir_p(dir);
  snprintf(path, sizeof path, "%s/wallpaper.toml", dir);
  FILE *f = fopen(path, "w");
  if (!f) return 1;
  fprintf(f,
          "name = \"%s\"\nauthor = \"\"\ndescription = \"\"\nfps = 30          # 0 = redraw only when an input changes\n"
          "scale = 0.5       # fraction of native resolution to render at\npointer = false   # receive pointer motion\n\n"
          "[params]          # name = [default, min, max]; becomes `uniform float name;`\nspeed = [1.0, 0.0, 4.0]\n\n"
          "[image]\nshader = \"main.frag\"\nchannels = []     # image file, buffer name, \"@background\" or \"@audio\"\n",
          id);
  fclose(f);
  snprintf(path, sizeof path, "%s/main.frag", dir);
  f = fopen(path, "w");
  if (!f) return 1;
  fputs("// Shadertoy-style entry point. Theme colors arrive as uniforms.\n"
        "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
        "  vec2 uv = fragCoord / iResolution.xy;\n"
        "  float wave = 0.5 + 0.5 * sin(uv.x * 6.0 + iTime * speed);\n"
        "  vec3 col = mix(uBackground, uAccent, wave * uv.y * 0.6);\n"
        "  fragColor = vec4(col, 1.0);\n}\n",
        f);
  fclose(f);
  printf("%s\nEdit main.frag, then: kinetic wall set %s   (saves hot-reload while it is showing)\n", dir, id);
  return 0;
}

static void parse_size(const char *s, int *w, int *h) {
  if (s) sscanf(s, "%dx%d", w, h);
}

static int usage(FILE *f) {
  fputs("kinetic wall " LW_VERSION " - living wallpapers (part of Kinetic)\n\n"
        "Usage: kinetic wall <command> [args]    (or kinetic-wall <command>)\n\n"
        "Wallpapers:\n"
        "  list [--json|--ids]        List installed wallpapers\n"
        "  set <id> [monitor]         Switch wallpaper (optionally on one monitor only)\n"
        "  next | prev | random       Cycle wallpapers\n"
        "  current                    Print the active wallpaper\n"
        "  params                     Show the active wallpaper's parameters and presets\n"
        "  param <name> <value>       Change a parameter live (remembered per wallpaper)\n"
        "                             @fps and @scale override the wallpaper's own rate and resolution\n"
        "  preset <name>              Apply one of the wallpaper's presets\n"
        "  param-reset                Put the wallpaper's parameters back to their defaults\n"
        "  restart                    Start the wallpaper over (a garden is replanted)\n"
        "  panel [tab]                Toggle the Kinetic panel; tab = wallpaper or library (others open the app)\n\n"
        "Settings:\n"
        "  config                     List every global option\n"
        "  config <key> [value]       Read or set one (value 'default' resets it)\n"
        "  describe                   Everything a settings UI needs, as JSON\n\n"
        "Control:\n"
        "  on | off | toggle          Show or hide the live wallpaper (off hands the desktop back to Omarchy)\n"
        "  pause | resume | toggle-pause\n"
        "  status                     Daemon status as JSON\n"
        "  stats [-w]                 Live frame rate, frame times, GPU and CPU cost (-w: keep watching)\n"
        "  fps [corner|off]           Toggle the on-screen FPS monitor (bottom-left, top-right, ...)\n"
        "  reload                     Re-read wall.toml and recompile shaders\n"
        "  refresh                    Re-read the Omarchy theme now\n"
        "  quit                       Stop the daemon\n\n"
        "Authoring:\n"
        "  add <folder|archive|link>  Add wallpapers (one, or a collection) to ~/.config/kinetic/wallpapers\n"
        "                             [--as <id>] [--yes]; each is compiled first (kinetic wall only)\n"
        "  remove <id>                Remove a wallpaper you added   added: list them (kinetic wall only)\n"
        "  new <id>                   Scaffold a wallpaper in ~/.config/kinetic/wallpapers\n"
        "  validate [id|dir]          Compile a wallpaper (all of them if none given)\n"
        "  thumbnail <id> <out.png> [WxH] [seconds] [--parallax x,y] [--glass <refract> <frost> <dispersion> <x,y,w,h,r;...> [bevel=..,curve=..,sat=..,bright=..,tint=..,spec=..,light=..]]\n"
        "  thumbnails [--force]       Render previews into ~/.cache/kinetic/wall/thumbs\n"
        "  bench [id] [WxH] [scale]   Measure GPU cost per frame, offscreen\n\n"
        "  daemon [-v]                Run the engine (normally started by systemd)\n\n"
        "Kinetic integration (sent by the bar and the motion module):\n"
        "  lens <output> ...          Glass behind the bar's capsules\n"
        "  motion <ms> <curve> <pts> <parallax> h|v | motion off\n"
        "                             Workspace switches eased like Hyprland's, with wallpaper parallax\n",
        f);
  return f == stderr ? 2 : 0;
}

int main(int argc, char **argv) {
  if (argc < 2) return usage(stderr);
  lw_verbose = getenv("KINETIC_DEBUG") != NULL;
  const char *c = argv[1];
  if (!strcmp(c, "-h") || !strcmp(c, "--help") || !strcmp(c, "help")) return usage(stdout);
  if (!strcmp(c, "--version") || !strcmp(c, "version")) return puts("kinetic-wall " LW_VERSION), 0;
  if (!strcmp(c, "daemon")) {
    for (int i = 2; i < argc; i++) lw_verbose |= !strcmp(argv[i], "-v");
    return daemon_main();
  }
  if (!strcmp(c, "list")) {
    if (argc > 2 && !strcmp(argv[2], "--json")) {
      int count = 0;
      putchar('[');
      lw_wallpaper_each(list_json, &count);
      puts("]");
    } else if (argc > 2 && !strcmp(argv[2], "--ids")) {
      lw_wallpaper_each(list_ids, NULL);
    } else {
      lw_wallpaper_each(list_plain, NULL);
    }
    return 0;
  }
  if (!strcmp(c, "new") && argc > 2) return cmd_new(argv[2]);
  if (!strcmp(c, "validate")) {
    if (argc > 2) return lw_cmd_validate(argv[2]);
    lw_wallpaper_each(validate_one, NULL);
    return validate_rc;
  }
  if (!strcmp(c, "thumbnail") && argc > 3) {
    /* --parallax <x>,<y>: the view panned that many screens, as a workspace switch would */
    for (int i = 4; i + 1 < argc; i++)
      if (!strcmp(argv[i], "--parallax")) {
        if (sscanf(argv[i + 1], "%f,%f", &lw_thumb_parallax[0], &lw_thumb_parallax[1]) < 1) return fputs("bad --parallax\n", stderr), 1;
        for (int k = i; k + 2 <= argc; k++) argv[k] = argv[k + 2];
        argc -= 2;
        break;
      }
    /* --glass <refract> <frost> <dispersion> <x,y,w,h,r;...> [bevel=..,curve=..,...]: preview the bar glass on it */
    for (int i = 4; i + 4 < argc; i++)
      if (!strcmp(argv[i], "--glass")) {
        char out[256];
        char *lens[] = {"lens", "offscreen", argv[i + 1], argv[i + 2], argv[i + 3], argv[i + 4], i + 5 < argc ? argv[i + 5] : NULL};
        lw_lens_command(i + 5 < argc ? 7 : 6, lens, out, sizeof out);
        if (strncmp(out, "ok", 2)) return fputs(out, stderr), 1;
        argc = i;
        break;
      }
    int w = 640, h = 400;
    parse_size(argc > 4 ? argv[4] : NULL, &w, &h);
    return lw_cmd_thumbnail(argv[2], argv[3], w, h, argc > 5 ? atof(argv[5]) : -1);
  }
  if (!strcmp(c, "thumbnails")) {
    struct thumbs t = {640, 400, 0, argc > 2 && !strcmp(argv[2], "--force")};
    char dir[LW_PATH];
    lw_dir_cache(dir, "thumbs");
    lw_mkdir_p(dir);
    lw_wallpaper_each(thumb_one, &t);
    printf("%s (%d rendered)\n", dir, t.made);
    return 0;
  }
  if (!strcmp(c, "bench")) {
    int size[2] = {1920, 1200};
    const char *id = NULL;
    float scale = 0;
    for (int i = 2; i < argc; i++) {
      if (strchr(argv[i], 'x') && argv[i][0] >= '0' && argv[i][0] <= '9')
        parse_size(argv[i], &size[0], &size[1]);
      else if ((argv[i][0] >= '0' && argv[i][0] <= '9') || argv[i][0] == '.')
        scale = strtof(argv[i], NULL);
      else
        id = argv[i];
    }
    if (id) return lw_cmd_bench(id, size[0], size[1], 3, scale);
    lw_wallpaper_each(bench_one, size);
    return 0;
  }
  if (!strcmp(c, "panel")) { /* kinetic wall panel [tab]: opens the Kinetic panel */
    char *tab = argc > 2 ? argv[2] : NULL;
    char *toggle[] = {"omarchy-shell", "-q", "shell", tab ? "summon" : "toggle", "kinetic", NULL};
    char *show[] = {"omarchy-shell", "-q", "kinetic.data", "showTab", tab, NULL};
    lw_spawn(toggle);
    if (tab) {
      usleep(150000);
      lw_spawn(show);
    }
    return 0;
  }
  if (!strcmp(c, "stats") && argc > 2 && (!strcmp(argv[2], "-w") || !strcmp(argv[2], "--watch"))) {
    for (;;) {
      fputs("\033[H\033[2J", stdout);
      int rc = lw_ipc_client("stats", false);
      fflush(stdout);
      if (rc == 2) return rc;
      sleep(1);
    }
  }
  if (!strcmp(c, "fps")) { /* kinetic wall fps [corner|off]: shorthand for the fps_monitor option */
    const char *v = argc > 2 ? argv[2] : NULL;
    char now[64] = "";
    if (!v) v = lw_ipc_query("config\x1f" "fps_monitor", now, sizeof now) == 0 && strncmp(now, "off", 3) ? "off" : "bottom-left";
    char cmd[128];
    snprintf(cmd, sizeof cmd, "config\x1f" "fps_monitor\x1f%s", v);
    return lw_ipc_client(cmd, false);
  }
  /* everything else goes to the daemon */
  char cmd[512];
  size_t n = 0;
  for (int i = 1; i < argc && n < sizeof cmd - 1; i++) n += (size_t)snprintf(cmd + n, sizeof cmd - n, "%s%s", i > 1 ? "\x1f" : "", argv[i]);
  return lw_ipc_client(cmd, false);
}

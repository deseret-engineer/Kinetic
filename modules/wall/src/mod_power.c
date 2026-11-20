/* Power module: battery level and AC state from sysfs, refreshed by kernel
 * uevents over netlink. There is no polling timer. */
#include "lw.h"
#include <dirent.h>
#include <linux/netlink.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int nl = -1;

static bool read_attr(const char *dev, const char *attr, char *out, size_t len) {
  char path[LW_PATH];
  snprintf(path, sizeof path, "/sys/class/power_supply/%s/%s", dev, attr);
  FILE *f = fopen(path, "r");
  if (!f) return false;
  bool ok = fgets(out, (int)len, f) != NULL;
  fclose(f);
  if (ok) out[strcspn(out, "\n")] = 0;
  return ok;
}

void lw_power_refresh(void) {
  struct lw_sys *s = &E.sys;
  bool had = s->has_battery, was_on = s->on_battery, was_charging = s->charging;
  float level = s->battery;
  s->has_battery = s->on_battery = s->charging = false;
  DIR *d = opendir("/sys/class/power_supply");
  struct dirent *e;
  while (d && (e = readdir(d))) {
    char type[32], val[32];
    if (e->d_name[0] == '.' || !read_attr(e->d_name, "type", type, sizeof type) || strcmp(type, "Battery")) continue;
    if (read_attr(e->d_name, "scope", val, sizeof val) && !strcmp(val, "Device")) continue; /* mouse, headset */
    float pct;
    char full[32];
    if (read_attr(e->d_name, "capacity", val, sizeof val))
      pct = strtof(val, NULL) / 100.0f;
    else if ((read_attr(e->d_name, "charge_now", val, sizeof val) && read_attr(e->d_name, "charge_full", full, sizeof full)) ||
             (read_attr(e->d_name, "energy_now", val, sizeof val) && read_attr(e->d_name, "energy_full", full, sizeof full)))
      pct = strtof(full, NULL) > 0 ? strtof(val, NULL) / strtof(full, NULL) : 1; /* some drivers expose no capacity */
    else
      continue;
    s->has_battery = true;
    s->battery = lw_clampf(pct, 0, 1);
    if (read_attr(e->d_name, "status", val, sizeof val)) {
      s->on_battery = !strcmp(val, "Discharging");
      s->charging = !strcmp(val, "Charging");
    }
    break;
  }
  if (d) closedir(d);
  if (!s->has_battery) s->battery = 1;
  if (had != s->has_battery || was_on != s->on_battery || was_charging != s->charging || level != s->battery) {
    lw_dbg("power: battery %.0f%% %s", s->battery * 100, s->on_battery ? "discharging" : s->charging ? "charging" : "idle");
    lw_engine_policy();
    lw_engine_invalidate(NULL);
  }
}

static void on_uevent(void *data, uint32_t ev) {
  char buf[4096];
  bool relevant = false;
  ssize_t n;
  while ((n = recv(nl, buf, sizeof buf - 1, MSG_DONTWAIT)) > 0) {
    buf[n] = 0;
    for (char *p = buf; p < buf + n; p += strlen(p) + 1)
      if (!strcmp(p, "SUBSYSTEM=power_supply")) relevant = true;
  }
  if (relevant) lw_power_refresh();
}

static bool start(void) {
  lw_power_refresh();
  struct sockaddr_nl addr = {.nl_family = AF_NETLINK, .nl_groups = 1};
  nl = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);
  if (nl < 0 || bind(nl, (struct sockaddr *)&addr, sizeof addr) < 0) {
    lw_log("power: no uevent socket, battery state will not update live");
    return true;
  }
  /* Only power_supply events should wake us; everything else is dropped in the kernel. */
  lw_loop_add(nl, on_uevent, NULL);
  return true;
}

static void stop(void) {
  if (nl >= 0) close(nl);
}

struct lw_module lw_mod_power = {"power", start, stop, false};

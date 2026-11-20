/* lw_run / lw_run_wait: the engine's way to run omarchy-shell and owe without
 * blocking or leaving zombies (SIGCHLD is ignored), and still hear how it went.
 * Build and run: tests/run.sh */
#include "../modules/wall/src/lw.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

/* the loop is the engine's; this test drives the one fd by hand */
static int loop_fd = -1;
static lw_fd_cb loop_cb;
static void *loop_data;
void lw_loop_add(int fd, lw_fd_cb cb, void *data) { loop_fd = fd, loop_cb = cb, loop_data = data; }
void lw_loop_del(int fd) { loop_fd = -1; }

#include "../modules/wall/src/util.c"
#include <poll.h>

static int got_status = -2;
static char got_out[256];
static void answer(void *data, int status, const char *out) {
  got_status = status;
  snprintf(got_out, sizeof got_out, "%s", out);
}

int main(void) {
  signal(SIGCHLD, SIG_IGN); /* as the daemon runs */
  char out[256];
  char *ok[] = {"sh", "-c", "echo fine", NULL};
  assert(lw_run_wait(ok, 3, out, sizeof out) == 0 && !strcmp(out, "fine"));
  char *fail[] = {"sh", "-c", "echo 'Target not found.' >&2; exit 1", NULL};
  assert(lw_run_wait(fail, 3, out, sizeof out) == 1 && !strcmp(out, "Target not found."));
  char *missing[] = {"kinetic-no-such-command", NULL};
  assert(lw_run_wait(missing, 3, out, sizeof out) == 127);
  char *hang[] = {"sleep", "10", NULL};
  double t0 = lw_now();
  assert(lw_run_wait(hang, 0.3, out, sizeof out) == 124 && lw_now() - t0 < 3);

  /* the asynchronous one: the answer arrives through the loop */
  char *two[] = {"sh", "-c", "printf 'a\\nb\\n'; exit 3", NULL};
  lw_run(two, 3, answer, NULL);
  while (loop_fd >= 0) {
    struct pollfd p = {.fd = loop_fd, .events = POLLIN};
    poll(&p, 1, 3000);
    loop_cb(loop_data, p.revents);
  }
  assert(got_status == 3 && !strcmp(got_out, "a\nb"));
  puts("lw_run: exit status, output, missing commands and timeouts come back right");
  return 0;
}

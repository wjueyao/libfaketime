/* Probe the recording policy without depending on an agent runtime. */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <time.h>

int main(int argc, char **argv)
{
  /* Initialize the library before injecting errors into timestamp SAVE locks.
   * Core library initialization failures are outside the SAVE policy. */
  struct timespec warmup;
  if (clock_gettime(CLOCK_MONOTONIC, &warmup) != 0) return 93;
  if (setenv("TEST_FLOCK_ACTIVE", "1", 1) != 0) return 94;
  if (argc > 1)
  {
    struct rlimit limit = {16, 16};
    if (signal(SIGXFSZ, SIG_IGN) == SIG_ERR || setrlimit(RLIMIT_FSIZE, &limit))
      return 90;
  }
  (void)argv;
  for (int i = 0; i < 4; i++)
  {
    struct timespec now;
    errno = E2BIG;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) return 91;
    if (errno != E2BIG) return 92;
    printf("%lld %ld\n", (long long)now.tv_sec, now.tv_nsec);
  }
  puts("business finished");
  return 7;
}

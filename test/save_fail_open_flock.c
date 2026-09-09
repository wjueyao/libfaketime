/* Fault injection only: do not link this into the installed library. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/syscall.h>
#include <unistd.h>

int flock(int fd, int operation)
{
  const char *fail = getenv("TEST_FLOCK_FAIL");
  if (getenv("TEST_FLOCK_ACTIVE") && fail &&
              ((strcmp(fail, "lock") == 0 && operation == LOCK_EX) ||
               (strcmp(fail, "unlock") == 0 && operation == LOCK_UN)))
  {
    errno = EBADF;
    return -1;
  }
  return syscall(SYS_flock, fd, operation);
}

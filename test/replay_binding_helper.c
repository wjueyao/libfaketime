/* Test-only registration helper: each exec gets its own real LOAD/shared pair. */
#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../src/faketime_common.h"
#include "../src/ft_sem.h"
int main(int argc, char **argv) {
  if(argc!=7)return 1;
  char sem[128],shm[128],clock[4096];
  snprintf(sem,sizeof(sem),"/faketime_sem_%ld",(long)getpid());
  snprintf(shm,sizeof(shm),"/faketime_shm_%ld",(long)getpid());
  snprintf(clock,sizeof(clock),"%s",argv[4]);
  char *slash=strrchr(clock,'/');if(!slash)return 1;
  strcpy(slash+1,"clock");
  ft_sem_t lock;
  if(ft_sem_create(sem,&lock))return 1;
  int fd=shm_open(shm,O_CREAT|O_EXCL|O_RDWR,0600);if(fd<0)return 1;
  struct ft_shared_s state={0};
  state.magic=FT_SHARED_MAGIC;state.version=FT_SHARED_VERSION;state.size=sizeof(state);
  state.start_time_real.nsec=state.start_time_mon.nsec=state.start_time_mon_raw.nsec=-1;
#ifdef CLOCK_BOOTTIME
  state.start_time_boot.nsec=-1;
#endif
  if(write(fd,&state,sizeof(state))!=sizeof(state))return 1;
  close(fd);ft_sem_close(&lock);
  /* Record only test-owned names for cleanup after the consuming process exits. */
  char ledger[4096];snprintf(ledger,sizeof(ledger),"%s",argv[4]);
  slash=strrchr(ledger,'/');strcpy(slash+1,"objects");
  FILE *owned=fopen(ledger,"a");if(!owned)return 1;
  fprintf(owned,"%ld\n",(long)getpid());fclose(owned);
  printf("FAKETIME_SHARED=%s %s%cFAKETIME_LOAD_FILE=%s%cTZ=UTC%cFAKETIME_DONT_FAKE_MONOTONIC=1%cNO_FAKE_STAT=1%c",sem,shm,0,clock,0,0,0,0);
  return 0;
}

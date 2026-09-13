/* Program registration: the trusted static helper owns identity and files.
 * Clock reads never invoke the helper. Online setup errors fail open. */
#include <poll.h>
#include <sys/wait.h>
#include <signal.h>

static long long program_millis(void) {
  struct timespec t;
  if (!real_clock_gettime || real_clock_gettime(CLOCK_MONOTONIC,&t)) return -1;
  return (long long)t.tv_sec*1000+t.tv_nsec/1000000;
}
static void program_unavailable(const char *config) {
  char path[4096];const char *slash=strrchr(config,'/');
  if(!slash || slash-config+13 >= (int)sizeof(path))return;
  size_t n=(size_t)(slash-config+1);memcpy(path,config,n);
  memcpy(path+n,"unavailable",12);
  int fd=open(path,O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC|O_NONBLOCK|O_NOFOLLOW,0600);
  if(fd>=0){const char message[]="program registration failed\n";(void)write(fd,message,sizeof(message)-1);close(fd);}
}
static void program_bind(void) {
  const char *helper=getenv("FAKETIME_PROGRAM_HELPER");
  const char *config=getenv("FAKETIME_PROGRAM_CONFIG");
  const char *session=getenv("FAKETIME_PROGRAM_SESSION");
  if(!helper || !config || !session)return;
  /* An exec is a new program, even when the kernel reuses the parent PID. */
  unsetenv("FAKETIME_SAVE_FILE");unsetenv("FAKETIME_LOAD_FILE");unsetenv("FAKETIME_SHARED");
  setenv("FAKETIME","+0",1);
  char pid[32];snprintf(pid,sizeof(pid),"%ld",(long)getpid());
  int pipes[2];if(pipe2(pipes,O_CLOEXEC)){program_unavailable(config);return;}
  long long start=program_millis();
  if(start<0){close(pipes[0]);close(pipes[1]);program_unavailable(config);return;}
  pid_t child=fork();
  if(child==0){
    close(pipes[0]);dup2(pipes[1],STDOUT_FILENO);close(pipes[1]);
    int nullfd=open("/dev/null",O_WRONLY);if(nullfd>=0){dup2(nullfd,STDERR_FILENO);close(nullfd);}
    char *const args[]={(char *)helper,"__faketime","env-init",(char *)config,pid,(char *)session,NULL};
    execv(helper,args);_exit(127);
  }
  close(pipes[1]);
  if(child<0){close(pipes[0]);program_unavailable(config);return;}
  char output[16384];size_t used=0;int ok=0,status;
  while(used<sizeof(output)){
    long long now=program_millis();int left=(int)(2000-(now-start));if(now<0 || left<=0)break;
    struct pollfd p={pipes[0],POLLIN|POLLHUP,0};
    int r=poll(&p,1,left);if(r<0 && errno==EINTR)continue;if(r<=0)break;
    ssize_t n=read(pipes[0],output+used,sizeof(output)-used);
    if(n<0 && errno==EINTR)continue;
    if(n==0){ok=1;break;}if(n<0)break;used+=(size_t)n;
  }
  close(pipes[0]);
  int reaped=0;
  while(ok && !reaped){
    pid_t result=waitpid(child,&status,WNOHANG);
    if(result==child){reaped=1;break;}
    if(result<0 && errno!=EINTR){ok=0;break;}
    long long now=program_millis();if(now<0 || now-start>=2000){ok=0;break;}
    poll(NULL,0,1);
  }
  if(!reaped){
    kill(child,SIGKILL);
    while(waitpid(child,&status,0)<0){if(errno!=EINTR)break;}
  }
  if(!ok || !WIFEXITED(status) || WEXITSTATUS(status) || (used && output[used-1])){
    program_unavailable(config);return;
  }
  /* Only helper-owned environment records; no shell evaluation. */
  for(size_t at=0;at<used;){
    char *line=output+at;size_t len=strlen(line);char *eq=strchr(line,'=');
    if(!eq){program_unavailable(config);return;}*eq=0;
    if(strcmp(line,"LD_PRELOAD") && strcmp(line,"TZ") && strcmp(line,"NO_FAKE_STAT") &&
       strcmp(line,"FAKETIME") && strncmp(line,"FAKETIME_",9)) {program_unavailable(config);return;}
    if(setenv(line,eq+1,1)){program_unavailable(config);return;}at+=len+1;
  }
}

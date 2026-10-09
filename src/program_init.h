/* Program registration: the trusted static helper owns identity and files.
 * Clock reads never invoke the helper. Online setup errors fail open. */
#include <poll.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/file.h>
#ifdef __APPLE__
#include <crt_externs.h>
#include <libproc.h>
#endif

static int program_active_fd = -1;

static long long program_millis(void) {
  struct timespec t;
  int (*clock_now)(clockid_t,struct timespec *)=dlsym(RTLD_NEXT,"clock_gettime");
  if (!clock_now || clock_now(CLOCK_MONOTONIC,&t)) return -1;
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
  unsetenv("FAKETIME_SAVE_FILE");unsetenv("FAKETIME_LOAD_FILE");unsetenv("FAKETIME_SHARED");unsetenv("FAKETIME_SHARED_FILE");
  setenv("FAKETIME","+0",1);
  /* Optional file protocol: hold an activity lease across the entire program,
   * including forked children. Close (never LOCK_UN) releases the last holder
   * on normal exit or SIGKILL. A completed attempt cannot register new work. */
  const char *active=getenv("FAKETIME_PROGRAM_ACTIVE");
  if(active){
    char closed;
    program_active_fd=open(active,O_RDONLY|O_NOFOLLOW);
    if(program_active_fd<0 || flock(program_active_fd,LOCK_SH|LOCK_NB) ||
       pread(program_active_fd,&closed,1,0)!=0){
      if(program_active_fd>=0)close(program_active_fd);
      program_active_fd=-1;program_unavailable(config);return;
    }
    /* Keep the lease across exec, including protected/non-instrumented images.
     * A new constructor replaces a matching inherited descriptor only after
     * taking its own lock, so there is no unprotected handoff window. */
    const char *inherited=getenv("FAKETIME_PROGRAM_ACTIVE_FD");
    if(inherited){
      char *end;errno=0;long fd=strtol(inherited,&end,10);
      struct stat old_state,new_state;
      if(!errno && *inherited && !*end && fd>2 && fd<=INT_MAX && fd!=program_active_fd &&
         !ft_real_fstat((int)fd,&old_state) && !ft_real_fstat(program_active_fd,&new_state) &&
         old_state.st_dev==new_state.st_dev && old_state.st_ino==new_state.st_ino)close((int)fd);
    }
    char descriptor[32];snprintf(descriptor,sizeof(descriptor),"%d",program_active_fd);
    if(setenv("FAKETIME_PROGRAM_ACTIVE_FD",descriptor,1)){program_unavailable(config);return;}
  }
  char pid[32];snprintf(pid,sizeof(pid),"%ld",(long)getpid());
  char *legacy_args[]={(char *)helper,"__faketime","env-init","--",(char *)config,pid,(char *)session,NULL};
  char **helper_args=legacy_args;
#ifdef __APPLE__
  char executable[4096],cwd[4096];
  if(active){
    int argc=*_NSGetArgc();char **argv=*_NSGetArgv();
    if(argc<1 || argc>65536 || proc_pidpath(getpid(),executable,sizeof(executable))<=0 ||
       !getcwd(cwd,sizeof(cwd))){program_unavailable(config);return;}
    size_t bytes=strlen(executable)+strlen(cwd);
    for(int i=0;i<argc;i++)bytes+=strlen(argv[i])+1;
    if(bytes>128*1024){program_unavailable(config);return;}
    helper_args=calloc((size_t)argc+10,sizeof(char *));
    if(!helper_args){program_unavailable(config);return;}
    char *prefix[]={(char *)helper,"__faketime","env-init-v2","--",(char *)config,pid,(char *)session,executable,cwd};
    memcpy(helper_args,prefix,sizeof(prefix));
    for(int i=0;i<argc;i++)helper_args[i+9]=argv[i];
  }
#endif

  int pipes[2];
  if(pipe(pipes)){if(helper_args!=legacy_args)free(helper_args);program_unavailable(config);return;}
  if(fcntl(pipes[0],F_SETFD,FD_CLOEXEC)==-1 ||
     fcntl(pipes[1],F_SETFD,FD_CLOEXEC)==-1){
    int saved_errno=errno;
    close(pipes[0]);close(pipes[1]);
    errno=saved_errno;if(helper_args!=legacy_args)free(helper_args);program_unavailable(config);return;
  }
  long long start=program_millis();
  if(start<0){close(pipes[0]);close(pipes[1]);if(helper_args!=legacy_args)free(helper_args);program_unavailable(config);return;}
  /* The registration helper is control-plane code, not a recorded program.
   * Prepare its environment before fork; do not re-inject this library. */
  extern char **environ;
  size_t env_count=0,helper_count=0;
  while(environ[env_count])env_count++;
  char **helper_env=malloc((env_count+1)*sizeof(*helper_env));
  if(!helper_env){close(pipes[0]);close(pipes[1]);if(helper_args!=legacy_args)free(helper_args);program_unavailable(config);return;}
  for(size_t i=0;i<env_count;i++)
    if(strncmp(environ[i],"LD_PRELOAD=",11) && strncmp(environ[i],"DYLD_INSERT_LIBRARIES=",22))
      helper_env[helper_count++]=environ[i];
  helper_env[helper_count]=NULL;
  pid_t child=fork();
  if(child==0){
    close(pipes[0]);dup2(pipes[1],STDOUT_FILENO);close(pipes[1]);
    int nullfd=open("/dev/null",O_WRONLY);if(nullfd>=0){dup2(nullfd,STDERR_FILENO);close(nullfd);}
    execve(helper,helper_args,helper_env);_exit(127);
  }
  free(helper_env);
  if(helper_args!=legacy_args)free(helper_args);
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
    if(strcmp(line,"LD_PRELOAD") && strcmp(line,"DYLD_INSERT_LIBRARIES") && strcmp(line,"TZ") && strcmp(line,"NO_FAKE_STAT") &&
       strcmp(line,"FAKETIME") && strncmp(line,"FAKETIME_",9)) {program_unavailable(config);return;}
    if(setenv(line,eq+1,1)){program_unavailable(config);return;}at+=len+1;
  }
}

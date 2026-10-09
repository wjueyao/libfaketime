#include <stdio.h>
#include <time.h>
int main(void) { struct timespec t; if(clock_gettime(CLOCK_REALTIME,&t))return 1; printf("%lld\n",(long long)t.tv_sec); return 0; }

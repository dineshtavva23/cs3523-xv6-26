#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main(int argc,char *argv[]){
    getpid();
    getpid();
    getpid();

    // 3 system calls + 2 systems calls

    //stage:1
    int syscount1 = getsyscount();

    //stage:2
    for(int i=0;i<50;i++){
        uptime();
    }

    int syscount2 = getsyscount();
    printf("Number of system calls process has invoked by completion of stage-1 are :%d, and that after compelting stage-2 are:%d\n",syscount1,syscount2);

    int syscount3 = getsyscount();
    printf("Final sycalls process has invoked are(includes write() calls in above print statement): %d\n",syscount3);

    exit(0);
}
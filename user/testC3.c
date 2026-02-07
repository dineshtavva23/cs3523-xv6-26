#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main(int argc,char *argv[]){
    int pid = fork();
    if(pid==0){
        getpid();
        getpid();
        exit(0);
    }

    pause(5);
    int cnt = getchildsyscount(pid);
    printf("child sycalls count:%d\n",cnt);
    if(getchildsyscount(getppid())==-1){
        printf("Success! check passed\n");
    }

    wait(0);
    exit(0);
    
}
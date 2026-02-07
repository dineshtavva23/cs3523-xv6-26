#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main(int argc,char *argv[]){
    int id = getpid2();
    int actualid = getpid();
    if(id==actualid){
        printf("Succesfull!Pid of parent process is: %d, and it is matching with actual pid of parent\n",id);
    }
    else{
        printf("Failed! Pid of parent process is: %d(getpid2()), and it is not matching with actual pid of parent which is %d\n",id,actualid);
    }
    int pid = fork();
    if(pid==0){
        id = getpid2();
        actualid = getpid();
        if(id==actualid){
            printf("Succesfull!Pid of child process is: %d, and it is matching with actual pid of child\n",id);
        }
        else{
            printf("Failed! Pid of child process is: %d(getpid2()), and it is not matching with actual pid of child which is %d\n",id,actualid);
        }
    }
    else if(pid>0){
        wait(0);
        printf("End of test case!!\n");
    }
    else{
        printf("Error, fork failed\n");
    }
    exit(0);
}
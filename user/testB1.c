#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main(int argc,char *argv[]){
    int id = getpid();
    int ppid = getppid();
    int pid = fork();
    printf("Pid of parent process is: %d and pid of its parent process is: %d\n",id,ppid);

    if(pid==0){
        int id2 = getpid2();
        int ppid2 = getppid();
        printf("pid of child process is %d and that of its parent's is : %d\n",id2,ppid2);
        if(ppid2==id){
            printf("Successful! ppid() and parent id are matching\n");
        }
        else{
            printf("Erro! ppid() is not returning correct parent pid\n");
        }
    }
    else if(pid>0){
        wait(0);
        printf("Fork successful, pid of child is(from parent): %d\n",pid);
    }
    else{
        printf("Error, fork failed\n");
    }
    exit(0);
    
}
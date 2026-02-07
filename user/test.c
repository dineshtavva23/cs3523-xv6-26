#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc,char *argv[]){
    int n= 0;
    if(argc>=2){
        n = atoi(argv[1]);
    }

    printf("Say hello to kernel %d\n",n);
    hello(n);
    
    int id = getpid2();
    printf("Pid of current process is: %d\n",id);

    int pid = fork();
    id = getpid2();
    int nchilds=getnumchild();
    printf("Number of children of pid : %d are %d\n",id,nchilds);
    if(pid==0){
        int id = getpid2();
        int ppid = getppid();
        int nchild = getnumchild();
        printf("pid of child process is %d and that of its parent's is : %d and the number of children are: %d\n",id,ppid,nchild);
    }
    else if(pid>0){
        wait(0);
        int nchild = getnumchild();
        printf("pid of parent process is %d\n",id);
        printf("number of children of parent process is %d\n",nchild);
    }
    else{
        printf("Error, fork failed\n");
    }
    exit(0);
}


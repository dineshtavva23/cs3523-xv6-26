#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main(int argc,char *argv[]){
    printf("Initial children (should be 0): %d\n",getnumchild());

    if(fork()==0){
        pause(10);
        exit(0);
    }

    if(fork()==0){
        exit(0);
    }

    pause(3);
    
    printf("After 1 alive and 1 zombie: %d(Expected: 1)\n",getnumchild());

    if(getnumchild()==1){
        printf("Success! Zombie ignored.\n");
    }

    else{
        printf("Failure! Wrong count.\n");
    }

    wait(0);
    wait(0);
    exit(0);


    
} 
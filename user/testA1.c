#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"



int main(int argc,char *argv[]){
    int n = 0;

    int id1 = hello(n);
    int id2 = hello(3);

    if(id1==0&&id2==0){
        printf("hello() returned 0.\n");
    }
    else{
        printf("Something went wront with syscall hello(), did not return 0\n");
    }

    exit(0);

}
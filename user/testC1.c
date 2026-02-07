#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main(int argc,char *argv[]){
    int first = getsyscount();

    for(int i=0;i<25;i++){
        uptime();
    }
    int second = getsyscount();
    printf("Start: %d, End: %d\n",first,second);

    if(second-first==26){
        printf("Success!. counted corrected syscounts\n");
    }
    else{
        printf("Failure! Difference was %d\n",second-first);
    }

    exit(0);



}
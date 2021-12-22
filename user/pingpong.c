#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int p[2];
    pipe(p);

    char msg = 'p';
 
    if (fork() == 0) {
        if (read(p[0], &msg, 1) == 1) {
            fprintf(1, "%d: received ping\n", getpid());
            write(p[1], &msg, 1);

            close(p[0]);
            close(p[1]);
        } else {
            fprintf(2, "child failed to get p\n");
            exit(1);
        }
    } else {
        write(p[1], &msg, 1);
        
        wait(0);  // wait for child to catch ping

        if (read(p[0], &msg, 1) == 1) {
            fprintf(1, "%d: received pong\n", getpid());

            close(p[0]);
            close(p[1]);
        } else {
            fprintf(2, "parent failed to get p back\n");
            exit(1);
        }
    }

    exit(0);
}

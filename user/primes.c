#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// takes out numbers from the reading pipe
// if more than one number, create another pipe
// and do the seiving.
//
// Remark:
// The main routine (the first process of primes.c)
// prints all primes since it's the one who keeps
// calling prime().
//
// In this design we don't need wait()  (which is great !)
// since the main routine will not end until reaching
// the final seive.
void prime(int rp) {
    int base;
    int next;
    
    // base case: nothing from reading pipe
    if (read(rp, &base, sizeof base) != sizeof base) {
        close(rp);
        return;
    }

    printf("%d\n", base);
    
    // more than one number from the rp
    if (read(rp, &next, sizeof next)) {
        int pp[2];
        pipe(pp);

        if (fork()) {
            close(pp[1]);
            prime(pp[0]);  // go to the next seive
        } else {
            close(pp[0]);
            while (1) {
                if (next%base != 0)
                    write(pp[1], &next, sizeof next);
                
                if (read(rp, &next, sizeof next) != sizeof next)
                    break;
            }
            
            close(pp[1]);
        }
    }

    close(rp);
}

int main(int argc, char *argv[]) {
    int p[2];
    pipe(p);
    
    if (fork()) {
        close(p[1]);
        prime(p[0]);
    } else {
        close(p[0]);
        for (int i = 2; i < 36; i++)
            write(p[1], &i, sizeof i);
        
        // done sending, wait for children to do the seiving
        close(p[0]);
    }

    exit(0);
}

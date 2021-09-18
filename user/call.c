#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int g(int x) {
  return x+3;
}

int f(int x) {
  x += 1;
  x = 2*x;
  x -= 1;
  return x;
}

void main(void) {
  printf("%d %d\n", f(8)+1, 13);
  exit(0);
}

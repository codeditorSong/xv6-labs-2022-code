#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void
prime(int read_fd)
{
  int prime_num;
  int n;
  int p[2];
  void (*next_prime)(int);

  if(read(read_fd, &prime_num, sizeof(int)) <= 0){
    close(read_fd);
    exit(0);
  }

  printf("prime %d\n", prime_num);

  pipe(p);

  next_prime = prime;  // function pointer to break -Winfinite-recursion

  if(fork() == 0){
    // child
    close(p[1]);
    close(read_fd);
    next_prime(p[0]);
    close(p[0]);
    exit(0);
  } else {
    // parent
    close(p[0]);
    while(read(read_fd, &n, sizeof(int)) > 0){
      if(n % prime_num != 0){
        write(p[1], &n, sizeof(int));
      }
    }
    close(read_fd);
    close(p[1]);
    wait(0);
    exit(0);
  }
}

int
main(int argc, char *argv[])
{
  int p[2];
  int i;

  pipe(p);

  if(fork() == 0){
    // child - the prime pipeline
    close(p[1]);
    prime(p[0]);
    close(p[0]);
    exit(0);
  } else {
    // parent - feed numbers
    close(p[0]);
    for(i = 2; i <= 35; i++){
      write(p[1], &i, sizeof(int));
    }
    close(p[1]);
    wait(0);
    exit(0);
  }
}
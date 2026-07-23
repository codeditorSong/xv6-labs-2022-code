#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

int
main(int argc, char *argv[])
{
  char buf[1024];
  char *new_argv[MAXARG];
  int i, n;
  char c;

  if(argc < 2){
    fprintf(2, "Usage: xargs command [args...]\n");
    exit(1);
  }

  // Build the base argv from the command-line arguments
  for(i = 1; i < argc; i++){
    new_argv[i - 1] = argv[i];
  }

  n = 0;
  while(read(0, &c, 1) > 0){
    if(c == '\n'){
      buf[n] = '\0';
      new_argv[argc - 1] = buf;
      new_argv[argc] = 0;

      if(fork() == 0){
        exec(new_argv[0], new_argv);
        fprintf(2, "xargs: exec %s failed\n", new_argv[0]);
        exit(1);
      } else {
        wait(0);
      }
      n = 0;
    } else {
      buf[n++] = c;
      if(n >= sizeof(buf) - 1){
        fprintf(2, "xargs: input line too long\n");
        // Skip the rest of the line
        while(read(0, &c, 1) > 0 && c != '\n')
          ;
        n = 0;
      }
    }
  }

  exit(0);
}
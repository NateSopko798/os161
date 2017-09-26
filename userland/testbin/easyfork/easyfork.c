#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>

int
main(int argc, char *argv[])
{
	(void) argc;
	(void) argv;
	pid_t pid;
	pid = fork();
	if(pid == -1){
		printf("fork failed\n");
	}
	else if(pid == 0) {
		printf("Hello from child process\n");
	} else {
		int status;
		(void)waitpid(pid, &status, 0);
	}
	return 0;
}

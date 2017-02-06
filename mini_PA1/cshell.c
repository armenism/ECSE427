#include <stdio.h> 
#include <unistd.h> 
#include <string.h> 
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>

extern int errno;

int job_list[10], job_list_counter = 0;
pid_t pid_c, ppid;

// signal handling function
static void signalHandling(int signal) {
	printf("\nsignal %d was caught\n", signal);
	// kill the calling process
	if (getpid() == pid_c) kill(pid_c, SIGKILL);
}

// print given argmunets - for debugging purposes
void print_args(char *prompt, char *args[]) {
	int index = 0;

	while (args[index] != '\0') {
		printf("%s[%d] is %s\n", prompt, index, args[index]);
		index++;
	}
}

// in case of output redirection, get the filename given
void get_filename(char *args[], char **filename) {
	int index = 0;
	while (args[index] != '\0')
		if (strcmp(args[index++], ">") == 0) { // after the ">" will be our filename
			if (args[index] != NULL) {
			*filename = args[index];
			printf("%s\n", *filename);
			args[index] = NULL;
			args[index - 1] = NULL;
			} else {	// if no filename is provided
				perror("please specify a proper file name");
				exit(1);
			}
		}
}

// clean both argument arrays, ready to use for next iteration
void clean_args(char *args_1[], char *args_2[]) {
	int index = 0;

	while (args_1[index] != '\0') args_1[index++] = NULL;
	index = 0;
	while (args_2[index] != '\0') args_2[index++] = NULL;
}

// update list of jobs running in the background
int update_background_jobs(int job_list[], int length) {
	int temp[length], temp_index = 0, list_index = 0;

	for (; list_index <= length; list_index++)
		if (job_list[list_index] != '\0') {
			temp[temp_index++] = job_list[list_index];
			job_list[list_index] = '\0';
		}

	list_index = 0;

	while (list_index < temp_index) {
		job_list[list_index] = temp[list_index];
		list_index++;
	}

	return temp_index;
}

// check the condition of the jobs running in the background, then update the array
void maintain_jobs(int job_list[], int *job_list_counter) {
	int counter = 0, length;
	pid_t pid;

	while (counter < 8) {
		if (job_list[counter] != '\0'){
			pid = waitpid(job_list[counter], NULL, WNOHANG);
			if (pid == job_list[counter]) {
				kill(pid, SIGKILL);
				job_list[counter] = '\0';
			}
		}
		counter++;
	}
	length = counter;

	*job_list_counter = update_background_jobs(job_list, length);
}

// print the list of jobs running in the background
void static print_jobs(int job_list[]) {

	// update jobs before trying to print
	maintain_jobs(job_list, &job_list_counter);
	int job_number = 0;

	// print jobs
	while (job_list[job_number] != '\0') {
		printf("[%d]+ Running\t\t\t%d\n", job_number + 1, job_list[job_number]);
		job_number++;
	}
}

// bring a background process to the foreground
void foreground_job(char *args[], int job_list[], int *job_list_counter) {
	// check for proper format
	if (args[1] == '\0') return;

	// check if the job exists
	if ((atoi(args[1]) -1) >= 0 && (atoi(args[1]) -1) < *job_list_counter) {
		// store pid of this process, then wait for process to finish
		pid_c = job_list[atoi(args[1]) - 1];
		waitpid(job_list[atoi(args[1]) - 1], NULL, WUNTRACED);
		// update jobs once done
		maintain_jobs(job_list, job_list_counter);
	} else perror("given job does not exist");
}

// separate given arguments into two argument arrays - to be used for piping
void static separate_args(char *args_1[], char *args_2[], char *separator) {
	int counter_1 = 0, counter_2 = 0;
	int separator_reached = 0;	

	// parse until reached separator flag, then put everything after into args_2
	// and every index afterwards of args_1 are set to NULL
	for (; args_1[counter_1] != NULL ; counter_1++){
		if (strcmp(args_1[counter_1], separator) == 0) {
			separator_reached = 1;
			args_1[counter_1] = NULL;
		}
		else if (separator_reached == 1) {
			args_2[counter_2++] = args_1[counter_1];
			args_1[counter_1] = NULL;
		}
	} 
	// null terminated
	args_2[counter_2] = NULL;
}

// check to see if fork is needed for the given set of arguments or not
int execute_in_parent(char *cmd) {
	if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "cd") == 0 || 
		strcmp(cmd, "jobs") == 0 || strcmp(cmd, "fg") == 0)
		return 1;
	return 0;
}

// get the argmunets from the command line
int getcmd(char *prompt, char *args[], int *background, char *separator) {

	int length, i = 0, j = 0; 
	char *token, *loc, *line = NULL; 
	size_t linecap = 0;

	printf("%s", prompt);
	length = getline(&line, &linecap, stdin);

	if (length <= 0) exit(-1);
	// Check if background is specified
	if ((loc = index(line, '&')) != NULL) {
		*background = 1;
		*loc = ' ';
	} else *background = 0;

	// Check if piping/redirection is specified
	if (index(line, '|') != NULL && index(line, '>') != NULL) return 1;
	else if (index(line, '|') != NULL) *separator = '|';
	else if (index(line, '>') != NULL) *separator = '>';

	while ((token = strsep(&line, " \t\n")) != NULL) { // provided code
		for (; j < strlen(token); j++)
			if (token[j] <= 32) token[j] = '\0';
		if (strlen(token) > 0 && token) args[i++] = token;
	} args[i] = NULL; // all arguments have to be NULL terminated

	return 0;
}

void redirect_output(char *args[], char *filename) {
	int fd = open(filename, O_CREAT | O_WRONLY | O_CLOEXEC, 0644);

	// close stdout and replace it with new file
	close(1);
	dup(fd);
	close(fd);

	// execute, instead of being printed, the result will be reflected in the file
	execvp(args[0], args);

	perror("Output redirection failed!");
	exit(1);
}

void pipe_cmd(char *args_1[], char *args_2[]) {
	int fd[2];
	pid_t pid;

	// create a unidirectional pipe
	if (pipe(fd) != 0) {
		printf("Error while trying to pipe! exiting\n");
		exit(1);
	}
	// fork the process containing the pipe
	pid = fork();

	if (pid == -1) {
		printf("Error while trying to fork! exiting\n");
		exit(1);
	}

	if (pid == (pid_t) 0) { // child process - will write to pipe[1]
		// close stdout and replace with input of pipe
		close(1);
		dup(fd[1]);

		// close pipe output in child and duplicate of pipe input
		close(fd[0]);
		close(fd[1]);

		// exec - the result will be directed to the output of pipe located in parent
		execvp(args_1[0], args_1);
		perror("Piping failed!\n");
		exit(1);

	} else if (pid != (pid_t) 0) { // parent process - will read from pipe[0]
		// close stdin and replace with output of pipe
		close(0);
		dup(fd[0]);

		// close duplicate pipe input and pipe output
		close(fd[0]);
		close(fd[1]);

		// execute with regard to piped output from child process
		execvp(args_2[0], args_2);
		perror("Piping failed!\n");
		exit(1);
	}
}


int main(void) {

	ppid = getpid();
	// set signal handlers
	if (signal(SIGINT, signalHandling) == SIG_ERR) exit(1);
	if (signal(SIGTSTP, SIG_IGN) == SIG_ERR) exit(1);

	// define variables
	char *args_1[20], *args_2[20], *filename = NULL, separator, *pwd = "[cshell]$: ";
	int bg, status, cnt;
	pid_t pid;

	while(1) {
		// reset the following values every iteration
        bg = 0;
        filename = NULL;
        separator = '\0';

   		// check on and refresh background jobs
		maintain_jobs(job_list, &job_list_counter);
        clean_args(args_1, args_2);

		cnt = getcmd(pwd, args_1, &bg, &separator);

		// if proper input
		if (args_1[0] != NULL && cnt == 0) {

			// check for piping, if so then populate args_2
			if (separator == '|') separate_args(args_1, args_2, &separator);

			// check to see if command shall be executed in parent, otherwise will fork and execute in child
			if (execute_in_parent(args_1[0]) == 1) {
				// the following four commands are to be executed in the parent only
				if (strcmp(args_1[0], "exit") == 0) exit(0);
				else if (strcmp(args_1[0], "cd") == 0) {if(chdir(args_1[1]) == -1) printf("%s\n", strerror(errno));}
				else if (strcmp(args_1[0], "jobs") == 0) print_jobs(job_list);
				else if (strcmp(args_1[0], "fg") == 0) foreground_job(args_1, job_list, &job_list_counter);

			} else {
				// create a child
				pid = fork();

				if (pid == (pid_t) -1) {
			        printf("error encountered during fork\n");
			    } else if (bg == 1 && pid != (pid_t) 0 && job_list_counter < 8) { // check if background flag specified
			    	job_list[job_list_counter] = (int) pid;						  // if so then increment jobs in background
			    	job_list_counter++;
			    } else if (bg == 1 && pid == (pid_t) 0 && job_list_counter == 8) { // if no more jobs allowed in bg then exit
			    	printf("Jobs limit reached\n");
			    	exit(0);
			    } else if (pid != (pid_t) 0){ // if background flag not specified, then parent will wait for child to finish
			    	/*
			    	WUNTRACED: wait for child to finish
			    	WNOHANG: collects status of dead child if any 
			    	*/
			        waitpid(-1, &status, WUNTRACED);
			    } else if (pid == (pid_t) 0) {	// THE PIPING/REDIRECTION SHOULD BE DONE HERE (CHILD - GRANDCHILD)

			    	// get the pid of the child process for future reference
			    	pid_c = getpid();

			    	// piping
			        if (separator == '|') {
			        	//print_args(args_2);
			        	pipe_cmd(args_1, args_2);
			        	waitpid(-1, NULL, WUNTRACED);
			        }
			        // output redirection
			        else if (separator == '>') {
			        	get_filename(args_1, &filename);
			        	redirect_output(args_1, filename);
			        }
			        // normal command to be executed
			        else {
			        	execvp(args_1[0], args_1);
			        	perror("error occured");
			        	exit(1);
			        }

			    } 
			}
		} else if (cnt == 1) printf("Invalid input\n");
	}
	return 0;
}

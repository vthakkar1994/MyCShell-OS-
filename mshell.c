/*
* mshell - My tiny shell program with job control
* Vaishali Thakkar
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <readline/readline.h>
#include <readline/history.h>

/* Misc manifest constants */
#define MAXLINE 1024 /* max line size */
#define MAXARGS 128 /* max args on a command line */
#define MAXJOBS 16 /* max jobs at any point in time */
#define MAXJID 1<<16 /* max job ID */

/* Job states */#define UNDEF 0 /* undefined */
#define FG 1 /* running in foreground */
#define BG 2 /* running in background */
#define ST 3 /* stopped */

/*
* Jobs states: FG (foreground), BG (background), ST (stopped)
* Job state transitions and enabling actions:
* FG -> ST : ctrl-z
* ST -> FG : fg command
* ST -> BG : bg command* ST -> BG : bg command
* BG -> FG : fg command
* At most 1 job can be in the FG state.
*/

/* Global variables */
extern char **environ;   /* defined in libc */
char prompt[] = "msh> "; /* command line prompt (DO NOT CHANGE) */
int verbose = 0;         /* if true, print additional output */
int nextjid = 1;         /* next job ID to allocate */
char sbuf[MAXLINE];      /* for composing sprintf messages */

struct job_t {           /* The job struct */
pid_t pid;               /* job PID */
int jid;                 /* job ID [1, 2, ...] */
int state;              /* UNDEF, BG, FG, or ST */
char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */

/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv);
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs);
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);handler_t *Signal(int signum, handler_t *handler);

#define MAX_WORDS 152
char* cmd [] ={"zmore","zgrep","zfgrep","zdiff","zcat","whiptail","vmmouse_detect","unicode_start","uname","ulockmgr_server","touch","tar","sync","stty","ss","setupcon","setfacl","run-parts","rnano","rm","rbash","ps","plymouth","ping","openvt","ntfswipe","ntfsmove","ntfsls","ntfsfix","ntfsdecrypt","ntfscluster","ntfscat","netstat","nc.openbsd","nano","mt-gnu","mountpoint","more","mknod","lsmod","ls","login","ln","lesskey","lessecho","kill","ip","init-checkconf","gzip","gunzip","getfacl","fuser","fgrep","false","ed","dumpkeys","dnsdomainname","dir","dd","dbus-daemon","date","cpio","chvt","chmod","chacl","bzmore","bzip2recover","bzgrep","bzexe","bzdiff","bzcat","bunzip2","bash","znew","zless","zforce","zegrep","zcmp","ypdomainname","which","vdir","uncompress","umount","true","tempfile","tailf","su","static-sh","sleep","sh","setfont","sed","running-in-container","rmdir","readlink","pwd","plymouth-upstart-bridge","ping6","pidof","open","ntfstruncate","ntfsmftalloc","ntfsinfo","ntfsdump_logfile","ntfscmp","ntfsck","nisdomainname","netcat","nc","mv","mt","mount","mktemp","mkdir","lsblk","loadkeys","lesspipe","lessfile","less","kbd_mode","initctl2dot","hostname","gzexe","grep","fusermount","findmnt","fgconsole","egrep","echo","domainname","dmesg","df","dbus-uuidgen","dbus-cleanup-sockets","dash","cp","chown","chgrp","cat","bzless","bzip2","bzfgrep","bzegrep","bzcmp","busybox"};
/*
* main - The shell's main routine
*/
void * xmalloc (int size)
{
    void *buf;
    buf = malloc (size);
    if (!buf) {
        fprintf (stderr, "Error: Out of memory. Exiting.'n");
        exit (1);
    }
    return buf;
}

char * dupstr (char* s) {
    char *r;
    r = (char*) xmalloc ((strlen (s) + 1));
    strcpy (r, s);
    return (r);
}

char* my_generator(const char* text, int state)
{
    static int list_index, len;
    char *name;
    if (!state) {
        list_index = 0;
        len = strlen (text);
    }
    while (name = cmd[list_index]) {
        list_index++;
        if (strncmp (name, text, len) == 0) {
            return (dupstr(name));
        }
    }
    /* If no names matched, then return NULL. */
    return ((char *)NULL);
}

static char** my_completion( const char * text , int start,  int end)
{
    char **matches;

    matches = (char **)NULL;
    if (start == 0)
        matches = rl_completion_matches ((char*)text, &my_generator);
    return (matches);
}


int main(int argc, char **argv)
{
	char c;
	char *cmdline;
	int emit_prompt = 1; /* emit prompt (default) */

	/* Redirect stderr to stdout (so that driver will get all output
	* on the pipe connected to stdout) */
	dup2(1, 2);

	/* Parse the command line */
	while ((c = getopt(argc, argv, "hvp")) != EOF) {
		switch (c) {
			case 'h': /* print help message */
				usage();
				break;
			case 'v': /* emit additional diagnostic info */
				verbose = 1;
				break;
			case 'p': /* don't print a prompt */
				emit_prompt = 0; /* handy for automatic testing */
				break;
			default:
				usage();
		}
	}

	/* Install the signal handlers */

	/* These are the ones you will need to implement */
	Signal(SIGINT, sigint_handler); /* ctrl-c */
	Signal(SIGTSTP, sigtstp_handler); /* ctrl-z */
	Signal(SIGCHLD, sigchld_handler); /* Terminated or stopped child */

	/* This one provides a clean way to kill the shell */
	Signal(SIGQUIT, sigquit_handler);

	/* Initialize the job list */
	initjobs(jobs);

    rl_attempted_completion_function = my_completion;
	/*Execute the shell's read/eval loop */
	while (1) {
        cmdline = readline("msh>");
            //enable auto-complete
		/* Evaluate the command line */
        /* CTRL+D, i/p = null string, say good bye */
        if (!cmdline) {
            fflush(stdout);
            exit(0);
        }
        if  (cmdline) {
            eval(cmdline);
        }
        fflush(stdout);
		fflush(stdout);
        if (cmdline) {
            free(cmdline);
        }

	}
	exit(0); /* control never reaches here */
}

/*
* eval - Evaluate the command line that the user has just typed in
*
* If the user has requested a built-in command (quit, jobs, bg or fg)
* then execute it immediately. Otherwise, fork a child process and
* run the job in the context of the child. If the job is running in
* the foreground, wait for it to terminate and then return. Note:
* each child process must have a unique process group ID so that our
* background children don't receive SIGINT (SIGTSTP) from the kernel
* when we type ctrl-c (ctrl-z) at the keyboard.
*/

void eval(char *cmdline)
{
	char *argv[MAXARGS]; /* argv for execve() */
	int bg;
	pid_t pid;
	sigset_t mask;

	bg = parseline(cmdline, argv);
	/* If there is no arguemnet, simply return.
         * passing null argument to strcmp
         * can lead to undefined behaviour */
    	if (!argv[0]) {
        	return;
    	}

	if(!builtin_cmd(argv)) {

		// Blocking SIGCHILD signals to avoid a race
		if(sigemptyset(&mask) != 0){
			unix_error("sigemptyset error");
		}
		if(sigaddset(&mask, SIGCHLD) != 0){
			unix_error("sigaddset error");
		}
		if(sigprocmask(SIG_BLOCK, &mask, NULL) != 0){
			unix_error("sigprocmask error");
		}

		// Forking
		if((pid = fork()) < 0){
			unix_error("forking error");
		}

		// Child- unblock mask, set new process group, run command
		else if(pid == 0) {
			if (sigprocmask(SIG_UNBLOCK, &mask, NULL) != 0){
				unix_error("sigprocmask error");
			}
			if(setpgid(0, 0) < 0) {
				unix_error("setpgid error");
			}
			if(execvp(argv[0], argv) < 0) {
				printf("%s: something went wrong : <%s>\n", argv[0], strerror(errno));
				exit(1);
			}
		}
		// Parent- add job to list, unblock signal, then do job
		else {
			if(!bg){
			addjob(jobs, pid, FG, cmdline);
			}
			else {
				addjob(jobs, pid, BG, cmdline);
			}
			if (sigprocmask(SIG_UNBLOCK, &mask, NULL) != 0){
				unix_error("sigprocmask error");
			}

			// Testing for a fg job
			if (!bg){
				waitfg(pid);
			}
			else {
				printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
			}
		}
	}
}

/*
* parseline - Parse the command line and build the argv array.
** Characters enclosed in single quotes are treated as a single
* argument. Return true if the user has requested a BG job, false if
* the user has requested a FG job.
*/

int parseline(const char *cmdline, char **argv)
{
	static char array[MAXLINE]; /* holds local copy of command line */
	char *buf = array; /* ptr that traverses command line */
	char *delim; /* points to first space delimiter */
	int argc; /* number of args */
	int bg; /* background job? */

	strcpy(buf, cmdline);
    if (buf[strlen(buf)-1] == '\n') {
        buf[strlen(buf)-1] = ' '; /* replace trailing '\n' with space */
    } else {
        buf[strlen(buf)] = ' ';
        buf[strlen(buf)] = 0;
    }
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

	/* Build the argv list */
	argc = 0;
	if (*buf == '\'') {
		buf++;
		delim = strchr(buf, '\'');
	}
	else {
		delim = strchr(buf, ' ');
	}
	while (delim) {
		argv[argc++] = buf;
		*delim = '\0';
		buf = delim + 1;
		while (*buf && (*buf == ' ')) /* ignore spaces */
			buf++;

		if (*buf == '\'') {
			buf++;
			delim = strchr(buf, '\'');
		}
		else {
			delim = strchr(buf, ' ');
		}
	}
	argv[argc] = NULL;

	if (argc == 0) /* ignore blank line */
		return 1;
	/* should the job run in the background? */

	if ((bg = (*argv[argc-1] == '&')) != 0) {
		argv[--argc] = NULL;
	}
	return bg;
}

/*
* builtin_cmd - If the user has typed a built-in command then execute
* it immediately.
*/

int builtin_cmd(char **argv)
{
	if(!strcmp(argv[0],"jobs")) /* list jobs */
	{
		listjobs(jobs);
		return 1;
	}
	if(!strcmp(argv[0],"bg") || !strcmp(argv[0],"fg"))
	{
		do_bgfg(argv);
		return 1;
	}
    /* quit/exit command */
	if((!strcmp(argv[0],"quit")) || (!strcmp(argv[0],"exit"))) exit(0);

	if(!strcmp(argv[0],"&")) return 1; /* ignore singleton & */

	return 0; /* not a builtin command */
}

/*
* do_bgfg - Execute the builtin bg and fg commands
*/
void do_bgfg(char **argv)
{
	struct job_t *j=NULL;

	/* Did the user provide bg or fg with an arguement? */
	if(argv[1] == NULL)
	{
		printf("%s command requires PID or %%jobid argument\n",argv[0]);
		return;
	}
	/* Is the argument a PID? */
	if(isdigit(argv[1][0]))
	{
		pid_t pid = atoi(argv[1]);
		if(!(j = getjobpid(jobs,pid)))
		{
			printf("(%d): No such process\n", pid);
			return;
		}
	}
	else if(argv[1][0] == '%') /* Is the argument a JID? */
	{
		int jid = atoi(&argv[1][1]); /* argv[1][1] points past the '%' */
		if(!(j = getjobjid(jobs,jid)))
		{
			printf("%s: No such job\n",argv[1]);
			return;
 		}
	}
	else
	{
		printf("%s: argument must be a PID or %%jobid\n",argv[0]);
		return;
	}

	if(!strcmp(argv[0],"bg"))
	{
		if(kill(-(j->pid),SIGCONT) < 0) unix_error("do_bgfg(): kill error");
		j->state =BG;
		printf("[%d] (%d) %s",j->jid,j->pid,j->cmdline);
	}
	else if(!strcmp(argv[0],"fg"))
	{
		if(kill(-(j->pid),SIGCONT) < 0) unix_error("do_bgfg(): kill error");
		j->state = FG;
		waitfg(j->pid);
	}
	else
	{
	printf("do_bgfg(): internal error\n");
	exit(0);
	}
	return;
	}

/*
* waitfg - Block until process pid is no longer the foreground process
*/

void waitfg(pid_t pid)
{
	struct job_t *fg_job = getjobpid(jobs,pid);
	if(!fg_job) return; /* Foreground job has already completed */
	while(fg_job->pid == pid && fg_job->state == FG)
		sleep(1);
	return;
}

/*****************
* Signal handlers
*****************/
/*
* sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
* a child job terminates (becomes a zombie), or stops because it
* received a SIGSTOP or SIGTSTP signal. The handler reaps all
* available zombie children, but doesn't wait for any other
* currently running children to terminate.
*/

void sigchld_handler(int sig)
{
	pid_t child_pid;
	int child_jid;
	int status;

	while((child_pid = waitpid(-1,&status,WNOHANG|WUNTRACED)) > 0)
	{
		if(WIFSTOPPED(status))
		{
			struct job_t *j = getjobpid(jobs,child_pid);

			if(!j)
			{
				printf("Lost track of (%d)\n", child_pid);
				return;
			}
			j->state = ST;
			fprintf(stdout,"Job [%d] (%d) stopped by signal %d\n",pid2jid(child_pid),child_pid,WSTOPSIG(status));
		}
		else if(WIFSIGNALED(status))
		{
			child_jid = pid2jid(child_pid);
			deletejob(jobs,child_pid);
			fprintf(stdout,"Job [%d] (%d) terminated by signal %d\n",child_jid,child_pid,WTERMSIG(status));
		}

		else if(WIFEXITED(status))
		{
			child_jid = pid2jid(child_pid);
			deletejob(jobs,child_pid);
		}
		else unix_error("waitpid error");
	}
	return;
}

/*
* sigint_handler - The kernel sends a SIGINT to the shell whenver the
* user types ctrl-c at the keyboard. Catch it and send it along
* to the foreground job.
*/

void sigint_handler(int sig)
{
	pid_t pid;

	if((pid = fgpid(jobs)) > 0) /* fgpid() returns 0 if there's no foreground job */
	{
		/* "-pid" means signal all processes in process group pid */
		if(kill(-pid,SIGINT) < 0) unix_error("sigint_handler(): kill() error");
		printf("Job [%d] (%d) terminatd by signal 2\n", pid2jid(pid), pid);
	}
	return;
}

/*
* sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
* the user types ctrl-z at the keyboard. Catch it and suspend the
* foreground job by sending it a SIGTSTP.
*/
void sigtstp_handler(int sig)
{
	pid_t pid;

	if((pid = fgpid(jobs)) > 0) /* fgpid() returns 0 if there's no foreground job */
	{
		if(kill(-pid,SIGTSTP) < 0) unix_error("sigtstp_handler(): kill() error");
		printf("Job [%d] (%d) stopped by signal 20\n", pid2jid(pid), pid);
	}
	return;
}

/*********************
* End signal handlers
*********************/
/***********************************************
* Helper routines that manipulate the job list
**********************************************/
/* clearjob - Clear the entries in a job struct */

void clearjob(struct job_t *job) {
	job->pid = 0;
	job->jid = 0;
	job->state = UNDEF;
	job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
	int i;

	for (i = 0; i < MAXJOBS; i++)
		clearjob(&jobs[i]);
	}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs)
{
	int i, max=0;

	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].jid > max)
			max = jobs[i].jid;
	return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline)
{
	int i;

	if (pid < 1)
		return 0;

	for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid == 0) {
			jobs[i].pid = pid;
			jobs[i].state = state;
			jobs[i].jid = nextjid++;
			if (nextjid > MAXJOBS)
			nextjid = 1;
			strcpy(jobs[i].cmdline, cmdline);
			if(verbose){
			printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
			}
			return 1;
		}
	}
	printf("Tried to create too many jobs\n");
	return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid)
{
	int i;
	if (pid< 1)
		return 0;
	for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid == pid) {
			clearjob(&jobs[i]);
			nextjid = maxjid(jobs)+1;
			return 1;
		}
	}
	return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
	int i;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].state == FG)
			return jobs[i].pid;
	return 0;
}

/* getjobpid - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
	int i;
	if (pid < 1)
		return NULL;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].pid == pid)
			return &jobs[i];
		return NULL;
	}

/* getjobjid - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid)
{
	int i;
	if (jid < 1)
		return NULL;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].jid == jid)
			return &jobs[i];
	return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid)
{

	int i;
	if (pid < 1)
		return 0;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].pid == pid) {
			return jobs[i].jid;
		}
	return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs)
{
	int i;

	for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid != 0) {
			printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
			switch (jobs[i].state) {
				case BG:
					printf("Running ");
					break;
				case FG:
					printf("Foreground ");
					break;
				case ST:
					printf("Stopped ");
					break;
				default:
					printf("listjobs: Internal error: job[%d].state=%d ",
					i, jobs[i].state);
			}
		printf("%s", jobs[i].cmdline);
		}
	}
}

/******************************
* end job list helper routines
******************************/
/***********************
* Other helper routines
***********************/
/*
* usage - print a help message
*/
void usage(void)
{
	printf("Usage: shell [-hvp]\n");
	printf(" -h print this message\n");
	printf(" -v print additional diagnostic information\n");
	printf(" -p do not emit a command prompt\n");
	exit(1);
}

/*
* unix_error - unix-style error routine
*/
void unix_error(char *msg)
{
	fprintf(stdout, "%s: %s\n", msg, strerror(errno));
	exit(1);
}

/*
* app_error - application-style error routine
*/
void app_error(char *msg)
{
	fprintf(stdout, "%s\n", msg);
	exit(1);
}

/*
* Signal - wrapper for the sigaction function
*/
handler_t *Signal(int signum, handler_t *handler)
{
	struct sigaction action, old_action;
	action.sa_handler = handler;
	sigemptyset(&action.sa_mask); /* block sigs of type being handled */
	action.sa_flags = SA_RESTART; /* restart syscalls if possible */

	if (sigaction(signum, &action, &old_action) < 0)
		unix_error("Signal error");
	return (old_action.sa_handler);
}

/*
* sigquit_handler - The driver program can gracefully terminate the
* child shell by sending it a SIGQUIT signal.
*/
void sigquit_handler(int sig)
{
	printf("Terminating after receipt of SIGQUIT signal\n");
	exit(1);
}



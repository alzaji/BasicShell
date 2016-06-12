/**
UNIX Shell Project

Sistemas Operativos
Grados I. Informatica, Computadores & Software
Dept. Arquitectura de Computadores - UMA

Some code adapted from "Fundamentos de Sistemas Operativos", Silberschatz et al.

To compile and run the program:
   $ gcc Shell_project.c job_control.c -o Shell
   $ ./Shell
	(then type ^D to exit program)

**/

#include "job_control.h"   // remember to compile with module job_control.c
#include "stdio.h"
#include "time.h"
#include "string.h"

//Defining some colors here
#define ANSI_COLOR_RED 		 "\x1b[31;1;1m"
#define ANSI_COLOR_GREEN	 "\x1b[32;1;1m"
#define ANSI_COLOR_BLUE		 "\x1b[34;1;1m"
#define ANSI_COLOR_CYAN    "\x1b[36;1;1m"
#define ANSI_COLOR_BROWN   "\x1b[33;1;1m"
#define ANSI_COLOR_PURPLE  "\x1b[35;1;1m"
#define ANSI_COLOR_BLACK   "\x1b[0m"

#define MAX_LINE 256 // 256 chars per line, per command, should be enough.


// -----------------------------------------------------------------------
//                            MAIN
// -----------------------------------------------------------------------

job * backjobs; // global list of jobs
struct termios termconf; /* will contain shell's config in order to restore later */
struct termios termpidconf; /* process terminal config */
int shell_terminal; /* terminal file descriptor */

void sigchldhandler(){
	int i;
	int info;
	int status;
	enum status status_analyzed;
	pid_t pid_aux, wpid;
	job * item = backjobs->next; // points to the next item in the list

  printf("\nSIGHCLD arrived...\n");
  block_SIGCHLD();

	if(empty_list(backjobs)!=1){
		while(item != NULL){
			pid_aux = item->pgid; //points to pgid
			wpid = waitpid(pid_aux, &status, WNOHANG | WUNTRACED); // let's check if the process is alive or if it has stopped
			if(wpid==pid_aux){
				status_analyzed=analyze_status(status, &info);
				printf("\nJob pid: %d, Command: %s, %s, Info: %d\n",pid_aux,item->command,status_strings[status_analyzed],info);
				if(status_analyzed==SUSPENDED){ // handle SUSPENDED here
					item->state=STOPPED; //Stop the process
					item=item->next; // point to the next one
				}else{ //handle SIGNALED and EXITED here
					job * aux = item;  // define a new job so we don't loose the reference
					item=item->next; // item points to the next one in list
					delete_job(backjobs, aux); // get the job out the list
				}
			}else{
				item=item->next;
			}
		}
	}
  unblock_SIGCHLD();
  fflush(stdout);
}



int main(void)
{
	char inputBuffer[MAX_LINE]; /* buffer to hold the command entered */
	int background;             /* equals 1 if a command is followed by '&' */
	char *args[MAX_LINE/2];     /* command line (of 256) has max of 128 arguments */

	// probably useful variables:
	int pid_fork, pid_wait; /* pid for created and waited process */
	int status;             /* status returned by wait */
	enum status status_res; /* status processed by analyze_status() */
	int info;				/* info processed by analyze_status() */
	char time_date[128];	/* time and date for prompt*/
	char path[100];			/* path for prompt*/

	backjobs = new_list("Background_jobs");
	signal(SIGCHLD,sigchldhandler);
	ignore_terminal_signals();
	shell_terminal = STDIN_FILENO;

  tcgetattr(shell_terminal,&termconf); /* Store the current shell's config */
	termpidconf = termconf;

	//Algo de info por aquí
	printf(ANSI_COLOR_RED "Shell básica V1.0\n");
	printf(ANSI_COLOR_GREEN "Alberto Zamora Jiménez - 2º Grado en Ingeniería Informatica A\n");
	printf(ANSI_COLOR_GREEN "Escriba help para una lista de comandos internos de esta shell.\n" );

	while (1)   /* Program terminates normally inside get_command() after ^D is typed */
	{
    /* The next block sets some config for the prompt, as we don't have any external config
    file (i.e .bashrc) around nor functions to set it in place.*/

		realpath("./", path); //resolves our current path pointer and returns a string in path
		time_t tiempo = time(0);
		struct tm *tlocal = localtime(&tiempo); //converts tiempo to a tm struct so we can manipulate it later
		strftime(time_date,128,"[%d/%m/%y %H:%M:%S]", tlocal); // format tlocal as we desire and retun a string to time_date
		printf(ANSI_COLOR_CYAN "%s :%s$ %s", time_date, path, ANSI_COLOR_BLACK); // set our prompt to cyan with the current date, time and path
		fflush(stdout);
		get_command(inputBuffer, MAX_LINE, args, &background);  /* get next command */

		if(args[0]==NULL) continue;   // if empty command

		if(strcmp(args[0], "help")==0){ // prints a detailed list of built-in commands
			printf(ANSI_COLOR_RED "Comandos disponibles: \n");
			printf(ANSI_COLOR_GREEN "fg ->" ANSI_COLOR_BLACK " Pone un job en primer plano. Admite como argumento el número de job que desea poner en primer plano. Si se ejecuta sin argumentos cogerá el último job introducido en la lista de jobs.\n");
			printf(ANSI_COLOR_GREEN "bg ->" ANSI_COLOR_BLACK " Pone un job en segundo plano. Admite como argumento el numero de job que desea poner en segundo plano. Si se ejecuta sin argumentos cogerá el último job introducido en la lista de jobs.\n");
			printf(ANSI_COLOR_GREEN "jobs ->" ANSI_COLOR_BLACK " Imprime una lista detallada de los jobs pertenecientes a esta shell.\n");
			continue;
		}
		if(strcmp(args[0], "jobs")==0){ // jobs implementation here, only prints our background jobs list.
			print_job_list(backjobs);
			continue;
		}
		else if(strcmp(args[0], "fg")==0){ // fg built-in command, gets a process from background to foreground, behaviour is similar as bash's fg
			int position;
			enum status status_a;
			char * comando;
			int listasize = backjobs->pgid;

			if(args[1]==NULL){ // if no arguments, get the first job from the list
				position=1;
			}else position = atoi(args[1]); // atoi() gets the argument and transforms it into an integer
				if(position<0 || position>listasize){ // if position is invalid, keep going.
					continue;
				}
				job * fgjob = get_item_bypos(backjobs, position); // get the item from the list
				comando = fgjob->command; // get the command issued in this job
				int fgpid = fgjob->pgid; // get pgid for the job
				set_terminal(fgpid); // give the terminal to the job
				tcsetattr(shell_terminal, TCSANOW, &termpidconf);
				if(fgjob->state==STOPPED){  // handle suspended jobs
					killpg(fgpid, SIGCONT); // send sigcont to the group so it awakes.
				}
				fgjob->state = FOREGROUND; // set job state to FOREGROUND
				waitpid(fgpid, &status, WUNTRACED); // return if child has stopped and tell me what happened
				status_a=analyze_status(status, &info); // get some info
				printf("Foreground pid: %d, Command: %s, %s, Info: %d\n",fgpid,comando,status_strings[status_a],info); // print it
				set_terminal(getpid()); //give the terminal to the shell
				tcsetattr(shell_terminal, TCSANOW, &termconf);
				if(status_a==SUSPENDED){ // if the job was suspended, set state to STOPPED
					fgjob->state=STOPPED;
				}else delete_job(backjobs, fgjob); //else it was signaled or it exited.
				continue;
		}
		else if(strcmp(args[0], "bg")==0){ //bg built-in command, gets a suspended process from the jobs list and puts it to background, similar behaviour as bash's bg
			int position;
			if(args[1]==NULL){ //if no arguments, get the first gob from the list
				position=1;
			}else position = atoi(args[1]); // atoi() gets the argument and transforms it to an integer
				if(position<0 || position>list_size(backjobs)){ //if position is invalid, keep going.
					continue;
				}
				job * fgjob = get_item_bypos(backjobs, position); // get the item from the list
				if(fgjob->state==STOPPED){ //if it is suspended, send a signal to awake it.
					int fgpid = fgjob->pgid; // get the pgid of the job
					fgjob->state=BACKGROUND; // set state to BACKGROUND
					killpg(fgpid, SIGCONT); // tell it to continue
					printf("Background job runing.. pid: %d, Command: %s\n",fgpid,fgjob->command); // print info about what happened

			}
				continue;
		}
		else if(strcmp(args[0], "cd")==0){ //cd built-in command, changes directory to the one passed as argument
					chdir(args[1]);
					continue;
				}
		else{
			pid_fork=fork(); //creates a new process
			if(pid_fork<0){	//ERROR
				printf("ERROR: Forking child error\n");
				exit(1);

			}else if(pid_fork==0){	//Child process
				new_process_group(getpid());	//Create new process group for pid child
				restore_terminal_signals(); // let me use ^C, ^Z...
				if(background==0){ //Foreground
          set_terminal(getpid());
				}

				if(execvp(*args, args)<0){
					printf("ERROR: Child process error\n");
					exit(1);
				}

				}else{
					if(background==0){ // no '&' was found, the process has the terminal
            set_terminal(pid_fork); // give the new process the terminal
						tcgetattr(shell_terminal, &termpidconf);
						waitpid(pid_fork, &status, WUNTRACED); // return if child has stopped and tell me what happened
						status_res=analyze_status(status, &info);
						printf("Foreground pid: %d, Command: %s, %s, Info: %d\n",pid_fork,*args,status_strings[status_res],info); // tell the user some info about the job
						set_terminal(getpid()); // get the terminal back
						tcsetattr(shell_terminal, TCSANOW, &termconf); // restores original terminal configuration
						if(status_res==SUSPENDED){
							job * this_job = new_job(pid_fork, args[0], STOPPED); // set this job as STOPPED
              block_SIGCHLD();
							add_job(backjobs, this_job); // add this job to the list
              unblock_SIGCHLD();
						}
					}else{ // backgroud == 1,
						printf("Background job running.. pid: %d, Command: %s\n",pid_fork,*args); // tell the user some info about the job
						job * this_job = new_job(pid_fork, args[0], BACKGROUND); // Set this job as BACKGROUND
						add_job(backjobs, this_job); // add this job to the list
					}

			}
		} // end else for pid_fork
		/* the steps are:
			 (1) fork a child process using fork()
			 (2) the child process will invoke execvp()
			 (3) if background == 0, the parent will wait, otherwise continue
			 (4) Shell shows a status message for processed command
			 (5) loop returns to get_commnad() function
		*/

	} // end while
}

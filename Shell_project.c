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

Autor: Alberto Zamora Jiménez
			2º Grado en Ingeniería Informática "A"

**/

#include "job_control.h"   // remember to compile with module job_control.c
#include "stdio.h"
#include "time.h"
#include "string.h"
#include "errno.h"

//Definimos algunos colores aquí
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
struct termios termconf; /* Estructura termios que guardara nuestra config original */
struct termios termpidconf; /* Estructura termios que guardara la config del proceso */
int shell_terminal; /* Variable para almacenar el descriptor de fichero de la terminal */

char *rsargs[MAX_LINE/2];     /* command line (of 256) has max of 128 arguments */

//------------------------------------------------------------------------
//                     Manejador de SIGCHILD
//------------------------------------------------------------------------

void sigchldhandler(){
	int info;
	int status;
	enum status status_analyzed;
	pid_t pid_aux, wpid, rpid; // Variables para gestionar pids
	job * item = backjobs->next; // Apunta al siguiente item en la lista

  printf("\nSIGCHILD invocado...\n"); // Informamos de la llegada de la señal
  block_SIGCHLD();

	if(empty_list(backjobs)!=1){ //Mientras la lista no este vacia la recorremos
		while(item != NULL){
			pid_aux = item->pgid; //Dame el pgid del item
			wpid = waitpid(pid_aux, &status, WNOHANG | WUNTRACED); // Informame de como esta el proceso
			if(wpid==pid_aux){ //Si termino o fue parado, analizamos el estado
				status_analyzed=analyze_status(status, &info);
				printf("\nJob pid: %d, Command: %s, %s, Info: %d\n",pid_aux,item->command,status_strings[status_analyzed],info); // Informa de su estado
				if(status_analyzed==SUSPENDED){ // Aquí manejamos SUSPENDED
					item->state=STOPPED; //Cambia el estado del proceso a STOPPED
					item=item->next; // Apunta item al siguiente elemento
				}
			else{ // Aquí manejamos SIGNALED y EXITED
				if (item->state == RESPAWNABLE){ // Si el proceso es de tipo RESPANWABLE lo manejamos aquí

					if (status_analyzed == EXITED){
					/*Si el respawnable salió, entonces se informará de que salió y se indicará que es un respawnable.
					 *Quitamos el proceso de la lista, volveremos a lanzar el proceso que salió e informamos de su ejecución
					 */
					printf("Es respawnable\n");
					job * aux = item;  // Definimos un item auxiliar para no perder la referencia del actual
					delete_job(backjobs, aux); // Lo sacamos de la lista

					rpid=fork(); // Creamos el proceso nuevo
					if(rpid<0){	// Si falla el fork, informamos por aquí y mandamos un exit(1) hacia arriba
						printf("ERROR: Forking child error\n");
						exit(1);

					}
					else if(rpid==0){	// Si tuvo exito lanzamos el proceso
						new_process_group(getpid());	// Creamos el nuevo grupo de procesos para el hijo
						restore_terminal_signals(); // Restauramos señales para poder usar ^C ^Z...

						if(execvp(*rsargs, rsargs)<0){ // Ejecutamos el proceso con sus argumentos, si falla, informamos y lanzamos exit(1) hacia arriba
							printf("ERROR: Child process error\n");
							exit(1);
						}
					}
					else{ //padre

						printf("Respawnable job running.. pid: %d, Command: %s \n",rpid,*rsargs); // Informa del estado del proceso
						job * this_job = new_job(rpid, rsargs[0], RESPAWNABLE); // Pon el estado de este proceso a RESPAWNABLE
						add_job(backjobs, this_job); // Lo añadimos a la lista
						item = item -> next; // Pasamos al siguiente
					}
				}

			}

				else{ //Si el proceso recibe una señal, o no es un respawnable lo manejamos aquí

				job * aux = item;  // Define un job nuevo para no perder la referencia
				item=item->next; // Avanzamos item al siguiente
				delete_job(backjobs, aux); // Eliminamos este item de la lista

				}
			}
		}
		else{ // Si no termino o no fue parado, pasamos al siguiente
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
	int redirectpos, redirecttype;

	// probably useful variables:
	int pid_fork; /* pid for created */
	int status;             /* status returned by wait */
	enum status status_res; /* status processed by analyze_status() */
	int info;				/* info processed by analyze_status() */
	char time_date[128];	/* Fecha y hora para nuestro prompt*/
	char path[100];			/* ruta para nuestro prompt */

	FILE *infile, *outfile; /* Variables para ficheros*/
	int fnum1, fnum2; /* Variables para descriptores de ficheros que se usaran en redirecciones*/

	backjobs = new_list("Background_jobs"); // Creamos nuestra lista de jobs;
	signal(SIGCHLD,sigchldhandler); // Instalamos el manegador de SIGCHILD
	ignore_terminal_signals(); // Prevenimos el lanzamiento de ^C ^Z...
	shell_terminal = STDIN_FILENO; //Descriptor de fichero de entrada de la terminal usado para tcgetattr

  tcgetattr(shell_terminal,&termconf); /* Guardamos la configuración actual de la terminal */
	termpidconf = termconf; // Inicializamos termpidconf con la misma config, sera modificada posteriormente

	/*Mostramos algo de información sobre la versión, autoría e indicamos el comando de ayuda */
	printf(ANSI_COLOR_RED "Shell ampliada V1.0\n");
	printf(ANSI_COLOR_GREEN "Alberto Zamora Jiménez - 2º Grado en Ingeniería Informatica A\n");
	printf(ANSI_COLOR_GREEN "Escriba help para una lista de comandos internos de esta shell.\n" );

	while (1)   /* Program terminates normally inside get_command() after ^D is typed */
	{
    /* El siguiente bloque configura el prompt al gusto del autor. Puesto que no disponemos
		 * de un fichero tipo .bashrc, lo hacemos a mano.
		 * El resultado sería algo como [13/06/16 03:27:36] :/home/alberto/D_DRIVE/Universidad/2015/Sistemas Operativos/Shell/Ampliada$
		 */

		realpath("./", path); //Resolvemos nuestro puntero de ruta actual y devolvemos el resultado en forma de cadena a path
		time_t tiempo = time(0);
		struct tm *tlocal = localtime(&tiempo); //Convierte tiempo a una estructura tm para manipularla posteriormente
		strftime(time_date,128,"[%d/%m/%y %H:%M:%S]", tlocal); // Le damos el formato deseado y lo guardamos en time_date
		printf(ANSI_COLOR_CYAN "%s :%s$ %s", time_date, path, ANSI_COLOR_BLACK); // Ponemos nuestro prompt con color cian y mostramos fecha, hora y ruta actual
		fflush(stdout);
		get_command(inputBuffer, MAX_LINE, args, &background, &redirectpos, &redirecttype);  /* get next command */

		if(args[0]==NULL) continue;   // Si el commando es vacio, continuamos

		//------------------------------------------------------------------------
		//                     		Comandos internos
		//------------------------------------------------------------------------

		/* Comando interno "help"
		 *-----------------------
		 * Cuando el usuario llame a help, mostraremos por pantalla información detallada sobre
		 * comandos disponibles y comportamiento de ciertos aspectos de la shell.
		 */
		if(strcmp(args[0], "help")==0){
			printf(ANSI_COLOR_RED "Comandos disponibles: \n");
			printf(ANSI_COLOR_GREEN "fg ->" ANSI_COLOR_BLACK " Pone un job en primer plano. Admite como argumento el número de job que desea poner en primer plano. Si se ejecuta sin argumentos cogerá el último job introducido en la lista de jobs.\n");
			printf(ANSI_COLOR_GREEN "bg ->" ANSI_COLOR_BLACK " Pone un job en segundo plano. Admite como argumento el numero de job que desea poner en segundo plano. Si se ejecuta sin argumentos cogerá el último job introducido en la lista de jobs.\n");
			printf(ANSI_COLOR_GREEN "jobs ->" ANSI_COLOR_BLACK " Imprime una lista detallada de los jobs pertenecientes a esta shell.\n");
			printf(ANSI_COLOR_RED "\nInformación adicional: \n");
			printf(ANSI_COLOR_GREEN "Jobs en background -> " ANSI_COLOR_BLACK "El lanzamiento de procesos en segundo plano se especificará añadiendo el flag '&' al final del proceso que se desea ejecutar, de la misma forma que en una shell bash.\n");
			printf(ANSI_COLOR_GREEN "Jobs respawnables -> " ANSI_COLOR_BLACK "Esta shell permite un modo de ejecución especial llamado respawnable. Se activa añadiendo el flag '#' al final del proceso que se desea ejecutar.\n");
			printf(ANSI_COLOR_BLACK "\n\t\t\t\tEl comportamiento de un job respawnable se describe a continuación:\n\n\t\t\t\tCuando el proceso termina, se vuelve a lanzar tal y como se especificó en la ejecución original, siendo inmortal mientras no se cambie su estado con un fg o bg.\n");
			printf(ANSI_COLOR_GREEN "Redirección de stdin/stdout -> " ANSI_COLOR_BLACK "Esta shell soporta la redirección tradicional de streams stdout/stdin. La invocación se realiza igual que en una shell bash.\n");
			printf(ANSI_COLOR_BLACK "\n\t\t\t\tModo de empleo: programa args > fichero de salida || programa args < fichero de entrada\n");
			continue;
		}

		/* Comando interno "jobs"
		 * ---------------------
		 * Cuando el usuario llame a jobs, mostraremos por pantalla la lista de trabajos
		 */

		if(strcmp(args[0], "jobs")==0){
			print_job_list(backjobs);
			continue;
		}

		/*Comando interno "fg"
		 *-------------------
		 * Trae un proceso a primer plano.
		 * El modo de empleo es similar a bash, si se lanza sin argumentos cogerá el primer elemento de la lista,
		 * Como argumento acepta el numero de job que se desea poner en primer plano.
		 */

		else if(strcmp(args[0], "fg")==0){
			int position;
			enum status status_a;
			char * comando;

			if(args[1]==NULL){ // Si no hay argumentos, coge el primero de la lista
				position=1;
			}else position = atoi(args[1]); // Sino usamos atoi() para coger el argumento y transformarlo en un entero manipulable
				if(position<0 || position>list_size(backjobs)){ // Si la posicion es erronea no hacemos nada y continuamos en el bucle
					continue;
				}
				job * fgjob = get_item_bypos(backjobs, position); // Coge el elemento de la lista
				comando = fgjob->command; // Coge el comando lanzado en este proceso
				int fgpid = fgjob->pgid; // Cogemos el pgid
				set_terminal(fgpid); // Le damos el terminal al job
				tcsetattr(shell_terminal, TCSANOW, &termpidconf); // Restauramos la configuracion del proceso
				if(fgjob->state==STOPPED){  // Manejo de trabajos suspendidos
					killpg(fgpid, SIGCONT); // Mandamos SIGCONT para que el proceso continue con su ejecucion
				}
				fgjob->state = FOREGROUND; // Ponemos el estado del proceso a FOREGROUND
				waitpid(fgpid, &status, WUNTRACED); // Vuelve si el hijo ha parado y dime qué ha pasado
				status_a=analyze_status(status, &info); // analizamos la información
				printf("Foreground pid: %d, Command: %s, %s, Info: %d\n",fgpid,comando,status_strings[status_a],info); // Informamos al usuario
				set_terminal(getpid()); //Devolvemos la terminal al shell
				tcsetattr(shell_terminal, TCSANOW, &termconf); //Restauramos la configuracion del shell
				if(status_a==SUSPENDED){ // Si se suspendio el proceso cambiamos el estado a STOPPED
					fgjob->state=STOPPED;
				}else delete_job(backjobs, fgjob); //Si no fue así, lo sacamos de la lista
				continue;
		}
		/*Comando interno "bg"
		 *-------------------
		 * Indica un proceso en segundo plano que continue su ejecución.
		 * Si el proceso que se encuentra en nuesta lista fue suspendido, le indicamos que continue su ejecución,
		 * tambien modificamos los procesos marcados como respawnables que pasen a ser procesos de tipo background.
		 * El modo de empleo es similar a bash, si se lanza sin argumentos cogerá el primer elemento de la lista,
		 * Como argumento acepta el numero de job que se desea poner en primer plano.
		 */
		else if(strcmp(args[0], "bg")==0){
			int position;
			if(args[1]==NULL){ //Si no hay argumentos, coge el primero de la lista
				position=1;
			}else position = atoi(args[1]); // Sino usamos atoi() para coger el argumento y transformarlo en un entero manipulable
				if(position<0 || position>list_size(backjobs)){ //Si la posicion es erronea no hacemos nada y continuamos en el bucle
					continue;
				}
				job * fgjob = get_item_bypos(backjobs, position); // Coge el elemento de la lista
				int fgpid = fgjob->pgid; // Cogemos el pgid
				if(fgjob->state==STOPPED){ // Si esta suspendido cambiamos el estado a BACKGROUND Y le mandamos SIGCONT para que continue
					fgjob->state=BACKGROUND;
					killpg(fgpid, SIGCONT);
					printf("Background job running.. pid: %d, Command: %s\n",fgpid,fgjob->command); // Informamos de lo que ha sucedido
			}else if (fgjob -> state == RESPAWNABLE){ // Si nuestro proceso es un respawnable, le quitamos la inmortalidad cambiando su estado a BACKGROUND
				fgjob->state=BACKGROUND;
				printf("Respawnable job pid: %d, Command: %s state changed to background job\n",fgpid,fgjob->command); // Informamos de lo sucedido
			}
				continue;
		}
		/*Comando interno "cd"
		 *--------------------
		 *cd es un comando interno típico, se usa para cambiar al directorio indicado pasado por argumento
		 */

		else if(strcmp(args[0], "cd")==0){
			
				if( -1 == chdir(args[1])){
					
					int errorsv = errno;
					
					if(errorsv == ENOENT){
						
						perror(args[1]);
					}
				}
					continue;
				}

	//------------------------------------------------------------------------
	//                     Lanzamiento de comandos externos
	//------------------------------------------------------------------------
	/* En este bloque se incluye el tratamiento normal de procesos de la shell básica, con la ampliación de
	 * las redirecciones.
	 * La trazabilidad se resume en lo siguiente:
	 *
	 * - Hacemos el fork y comprobamos si se creó.
	 * - Si no lo hizo, lanzamos un error por pantalla.
	 * - Si el proceso hijo se creo, creamos el nuevo grupo de procesos con el pid obtenido por getpid() y restauramos señales ^C ^Z...
	 * - Si se detecto algún tipo de redirección lo gestionamos
	 * - Si el proceso fue lanzado en Foreground (background == 0), le damos la terminal
	 * - Lanzamos execvp con el nombre de proceso y sus argumentos
	 * - Si se lanzo en foreground, le damos la terminal y realizamos un waitpid bloqueante hasta que salga, posteriormente le devolvemos la terminal
	 *   al shell, restauramos su configuracion e informamos del estado del proceso. Si fue suspendido, Lo añadimos a la lista de jobs indicando
	 *   dicho estado.
	 * - Si se lanzo en background (background == 1), simplemente le indicamos al usuario que fue lanzado y lo añadimos a la lista de jobs poniendo su estado a BACKGROUND
	 * - Para el proceso respawnable se optó por un nuevo valor para la variable background (background == 2) para manejar esta situación. Puesto que el lanzamiento se realiza en background, lo realizamos
	 *   y añadimos el job a la lista indicando que es de tipo RESPAWNABLE.
	 */


		else{

			pid_fork=fork(); //Creamos el proceso nuevo
			if(pid_fork<0){		// Si falla el fork, informamos por aquí y mandamos un exit(1) hacia arriba
				printf("ERROR: Forking child error\n");
				exit(1);

			}else if(pid_fork==0){	//Child process
				new_process_group(getpid());	// Si tuvo exito lanzamos el proceso
				restore_terminal_signals(); // Restauramos señales para poder usar ^C ^Z...

				//------------------------------------------------------------------------
				//                     Redirecciones
				//------------------------------------------------------------------------
				/*
				 * Para las redirecciones se definieron 2 variables de tipo int que nos indican su tipo y su posición dentro de la cadena de argumentos:
				 * int redirecttype = 0|1 // para valor 0 tratamos la redireccion de stdout, esto es asi porque se encontro el caracter '>'
				 *											 // para valor 1 tratamos la redireccion de stdin, esto es así porque se encontro el caracter '<'
				 *
				 * int redirectpos = posición del caracter '>' o '<' dentro de args[];
				 *
				 * Para la obtención de estos valores se modifico get_command() dentro de job_control.c y job_control.h definiendo estas 2 nuevas variables,
				 * como entrada. Se opto por obtener el tipo de redirección y su posicion a la hora de generar un nuevo argumento en los delimitadores ' ' y '\t'
 				*/
				if(redirecttype == 0){ // Redireccion de stdout

					if(NULL==(outfile = fopen(args[redirectpos+1],"w"))){ //Si no es posible abrir el fichero lanzamos un error

						printf("Error abriendo : %s", args[redirectpos+1]);
						exit(1);
					}
					args[redirectpos] = NULL; // eliminamos '>'
					args[redirectpos+1] = NULL; // eliminamos nombre de fichero
					fnum1=fileno(stdout); //descriptor de fichero de stdout
					fnum2=fileno(outfile); //descriptor de fichero del fichero
					dup2(fnum2,fnum1); //creamos la copia de los descriptores

				}
				else if(redirecttype == 1){ // Redireccion de stdin

					if(NULL==( infile = fopen(args[redirectpos+1],"r"))){ //Si no es posible abrir el fichero lanzamos un error

						printf("Error abriendo : %s", args[redirectpos+1]);
						exit(1);
					}
					args[redirectpos] = NULL; // eliminamos '<'
					args[redirectpos+1] = NULL; // eliminamos nombre de fichero
					fnum1=fileno(stdin); //descriptor de fichero de stdin
					fnum2=fileno(infile); //descriptor de fichero de stdout
					dup2(fnum2,fnum1); // creamos la copia de los descriptores

				}

				if(background==0){ //Foreground
          set_terminal(getpid());
			}

				if(execvp(*args, args)<0){ //si no se ejecuto execvp lanzamos un error
					printf("ERROR: Child process error\n");
					fclose(infile); // cerramos fichero de entrada en caso de que falle el exec
					exit(1);
				}
				fclose(outfile); //cerramos fichero de salida.

				}
				else{
					if(background==0){ // No se encontro '&' el proceso tiene el terminal
            set_terminal(pid_fork); // Dale al proceso el terminal
						tcgetattr(shell_terminal, &termpidconf); // Toma su configuración de terminal
						waitpid(pid_fork, &status, WUNTRACED); // Vuelve si el hijo ha parado y dime que ha ocurrido
						status_res=analyze_status(status, &info);
						printf("Foreground pid: %d, Command: %s, %s, Info: %d\n",pid_fork,*args,status_strings[status_res],info); // Informa al usuario
						set_terminal(getpid()); // Devuelve la terminal al shell
						tcsetattr(shell_terminal, TCSANOW, &termconf); // Restaura la configuración del terminal original
						if(status_res==SUSPENDED){ // si fue suspendido añadelo indicando que su estado es STOPPED
							job * this_job = new_job(pid_fork, args[0], STOPPED); // estado == STOPPED
              block_SIGCHLD();
							add_job(backjobs, this_job); // Lo añadimos a la lista
              unblock_SIGCHLD();
						}
					}else if (background == 1 ){ // Se encontro '&', background == 1, el proceso esta en segundo plano
						printf("Background job running.. pid: %d, Command: %s\n",pid_fork,*args); // Informa al usuario
						job * this_job = new_job(pid_fork, args[0], BACKGROUND); // Estado == BACKGROUND
						block_SIGCHLD();
						add_job(backjobs, this_job); // lo añadimos a la lista
						unblock_SIGCHLD();
					}
					else{ // Se encontro '#', background == 2, el proceso esta en segundo plano y es de tipo respawnable
						printf("Respawnable job running.. pid: %d, Command: %s \n",pid_fork,*args); // Informa al usuario
						job * this_job = new_job(pid_fork, args[0], RESPAWNABLE); // Estado == RESPAWNABLE
						block_SIGCHLD();
						add_job(backjobs, this_job); // lo añadimos a la lista
						unblock_SIGCHLD();
						memcpy(rsargs,args, sizeof(char[MAX_LINE/2])); // hacemos una copia del comando y sus argumentos y lo guardamos en rsargs
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

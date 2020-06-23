// proj2.c
// IOS project 2
// Author: Jakub Bartko, xbartk07
//         xbartk07@stud.fit.vutbr.cz
// FIT VUT Brno, 25.04.2020

// Usage:
//    ./proj2 PI IG JG IT JT
//        for descrition of arguments check section Arguments

// Description:
//    Implemetation of Faneuil Hall Problem

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>    // notAllServed
#include <unistd.h>     // fork, sleep
#include <semaphore.h>  // semaphores
#include <time.h>       // srand
#include <fcntl.h>      // O_CREAT
#include <stdarg.h>     // writeToFile
#include <sys/wait.h>   // wait
#include <sys/mman.h>   // mmap
#include <setjmp.h>     // setjmp, longjmp

#define nArgs 6

// macro for random number in range [min, max]
#define random(min, max) (float)(rand() % (max + 1 - min) + min)
// strtol with validation of its arguments
long getVal_s( char *string );
// output action to [file]
void writeToFile(FILE* file, const char* format, ...);
// clean up allocated memory
void cleanup();
// cleans up mem allocs & exits with EXIT_FAILURE and err msg
void err( const char* format, ... );

void immigrant();   // represents immigrant process
void judge();       // represents judge process

// Fork Failure exception handling
void handler_termIMM();
jmp_buf termIMM;
void handler_termJudge();
jmp_buf termJudge;

sem_t *Mutex,     // for actions that require to be atomic
      *Door,      // entrance to building
      *RegBooth,  // registration booth
      *Confirm;   // confirmation of immigrant's request

int *action,  // action ID
    *I,       // for global immigrant tracting -- each gets unique ID
    *NE,      // IMMs entered & not confirmed
    *NC,      // IMMs checked & not confirmed
    *NB,      // IMMs currently in the building
    *confirmed; // IMMs already confirmed

// Arguments
int PI,    // number of immigrants
    IG,     // max  IMM generating                 delay
    JG,     // max  judge returns to building      delay
    IT,     // max  IMM picks up certificate       delay
    JT;     // max  judge confirmes all requests   delay

FILE *file;   // Output file

int main( int argc, char *argv[] )
{
  // opens file for output
  file = fopen( "proj2.out", "w" );
  setbuf( file, NULL );
  if ( file == NULL )   err( "Failed to open file for output" );

  // gets arguments
  if ( argc != nArgs )  err( "Invalid number of arguments" );
  PI = getVal_s( argv[1] );
  IG = getVal_s( argv[2] );
  JG = getVal_s( argv[3] );
  IT = getVal_s( argv[4] );
  JT = getVal_s( argv[5] );
  if (  PI < 1 ||
        IG < 0 || IG > 2000 ||
        JG < 0 || JG > 2000 ||
        IT < 0 || IT > 2000 ||
        JT < 0 || JT > 2000 )
    err( "Value of argument(s) out of bounds" );

  action    = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  confirmed = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  I         = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  NE        = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  NC        = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  NB        = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (  action    == NULL ||
        confirmed == NULL ||
        I         == NULL ||
        NE        == NULL ||
        NC        == NULL ||
        NB        == NULL )   err( "Failed to allocate memory" );
  *action = 1; *I = 1; *confirmed = 0;
  *NE = 0; *NC = 0; *NB = 0;

  srand( time(NULL) ); // seeding for random()

  // initializing semaphores
  Mutex     = sem_open( "/Mutex",     O_CREAT, 0666, 1 );
  Door      = sem_open( "/Door",      O_CREAT, 0666, 1 );
  RegBooth  = sem_open( "/RegBooth",  O_CREAT, 0666, 1 );
  Confirm   = sem_open( "/Confirm",   O_CREAT, 0666, 0 );
  // ^initialy locks confirmation -- for judge to unlock
  if (  Mutex     == SEM_FAILED ||
        Door      == SEM_FAILED ||
        RegBooth  == SEM_FAILED ||
        Confirm   == SEM_FAILED )   err( "Failed to initialize semaphores" );

  // creates Judge
  pid_t pidJudge = fork();
  if      ( pidJudge == -1 )   err( "Failed to fork" );
  else if ( pidJudge == 0 )
  {
    // generating IMMs
    pid_t immigrants[ PI ];
    for ( int iterator = 0; iterator < PI; iterator++ )
    {
      sleep( random(0, IG) / 1000 );

      immigrants[ iterator ] = fork();
      if ( immigrants[ iterator ] == -1 )
      {
        // terminates all IMM processes
        for ( int i = 0; i < iterator; i++ )
          kill( immigrants[i], SIGUSR1 );

        // waiting for all generated IMMs to terminate
        for ( int i = 0; i < iterator; i++ )  waitpid( immigrants[i], NULL, 0 );
        cleanup();
        fprintf( stderr, "ERROR: Failed to fork\n" );
        kill( pidJudge, SIGUSR1 ); // terminates Judge + child <-- this process
      }
      else if ( immigrants[ iterator ] == 0 )
      {
        immigrant();
        exit(EXIT_SUCCESS);
      }
    }
    // finished generating IMMs --> waiting for all IMMs
    for ( int i = 0; i < PI; i++ )  waitpid( immigrants[i], NULL, 0 );
    cleanup();
    exit(EXIT_SUCCESS);
  }
  else
    judge();

  waitpid( pidJudge, NULL, 0 ); // waits for all

  sem_unlink( "/Mutex" );
  sem_unlink( "/Door" );
  sem_unlink( "/RegBooth" );
  sem_unlink( "/Confirm" );

  return 0;
}


void immigrant()
{
  // Fork Failure exception handling
  signal( SIGUSR1, handler_termIMM );
  if ( setjmp(termIMM) )
  {
    cleanup();
    return;
  }

  int id = *I; // IMM's unique ID

  // initializes
  sem_wait(Mutex);
  fprintf( file, "%-10d%-7s%-20d%s\n", *action, ": IMM ", id, ": starts" );
  (*action)++; (*I)++;
  sem_post(Mutex);

  // enters the building
  sem_wait(Door);
  writeToFile( file, "%-7s%-20d%-25s: %-10d: %-10d: %-10d\n", ": IMM ", id, ": enters", ++(*NE), (*NC), ++(*NB) );
  sem_post(Door);

  // registers
  sem_wait(RegBooth);
  writeToFile( file, "%-7s%-20d%-25s: %-10d: %-10d: %-10d\n", ": IMM ", id, ": checks", (*NE), ++(*NC), (*NB) );
  sem_post(RegBooth);

  // waits for judges confirmation
  sem_wait(Confirm);
  (*confirmed)++;
  sem_post(Confirm);
  writeToFile( file, "%-7s%-20d%-25s: %-10d: %-10d: %-10d\n", ": IMM ", id, ": wants certificate", (*NE), (*NC), (*NB) );

  // getting certificate
  sleep( random(0, IT) / 1000 );
  writeToFile( file, "%-7s%-20d%-25s: %-10d: %-10d: %-10d\n", ": IMM ", id, ": got certificate", (*NE), (*NC), (*NB) );

  // leaves the building
  sem_wait(Door);
  writeToFile( file, "%-7s%-20d%-25s: %-10d: %-10d: %-10d\n", ": IMM ", id, ": leaves", (*NE), (*NC), --(*NB) );
  sem_post(Door);

  cleanup();
}

void judge()
{
  // Fork Failure exception handling
  signal( SIGUSR1, handler_termJudge );
  if ( setjmp(termJudge) )
  {
    cleanup();
    return;
  }

  do { // works untill all IMMs certified
    // arrives at building
    sleep( random(0, JG) / 1000 );
    writeToFile( file, "%-27s%s\n", ": JUDGE ", ": wants to enter" );

    // enters the building && locks door till the Judge leaves
    sem_wait(Door);
    writeToFile( file, "%-27s%-25s: %-10d: %-10d: %-10d\n", ": JUDGE ", ": enters", (*NE), (*NC), (*NB) );

    if ( *NE != *NC ) // waits for all IMMs in the building to register
    {
      writeToFile( file, "%-27s%-25s: %-10d: %-10d: %-10d\n", ": JUDGE ", ": waits for imm", (*NE), (*NC), (*NB) );
      sem_wait(RegBooth);
      sem_post(RegBooth);
    }

    // confirmation process
    writeToFile( file, "%-27s%-25s: %-10d: %-10d: %-10d\n", ": JUDGE ", ": starts confirmation", (*NE), (*NC), (*NB) );
    sleep( random(0, JT) / 1000 );
    *NE = 0; *NC = 0;     // confirmes all
    writeToFile( file, "%-27s%-25s: %-10d: %-10d: %-10d\n", ": JUDGE ", ": ends confirmation", (*NE), (*NC), (*NB) );
    sem_post(Confirm);

    // leaving the building
    sleep( random(0, JT) / 1000 );
    sem_wait(Confirm);  // renew queue for confirmations
    writeToFile( file, "%-27s%-25s: %-10d: %-10d: %-10d\n", ": JUDGE ", ": leaves", (*NE), (*NC), (*NB) );
    sem_post(Door);     // unlocks door after leaving
  }  while ( *confirmed < PI ); // not all IMMs served

  writeToFile( file, "%-27s%-20s\n", ": JUDGE ", ": finishes" );
  cleanup();
}

void handler_termIMM(){
  longjmp( termIMM, 1 );
}
void handler_termJudge(){
  longjmp( termJudge, 1 );
}

void writeToFile(FILE* file, const char* format, ...)
{
  sem_wait(Mutex);
    va_list argptr;
    va_start(argptr, format);

    fprintf(file,"%-10d",*action);
    vfprintf(file, format, argptr);

    va_end(argptr);

    (*action)++;
  sem_post(Mutex);
}

void cleanup()
{
  sem_close( Mutex );
  sem_close( Door );
  sem_close( RegBooth );
  sem_close( Confirm );

  munmap( confirmed,  sizeof(int) );
  munmap( action,   sizeof(int) );
  munmap( I,        sizeof(int) );
  munmap( NE,       sizeof(int) );
  munmap( NC,       sizeof(int) );
  munmap( NB,       sizeof(int) );

  if ( file != NULL ) fclose( file );
}

void err(const char* format, ...)
{
  va_list argptr;
  va_start(argptr, format);

  fprintf( stderr, "ERROR: " );
  vfprintf( stderr, format, argptr );
  fprintf( stderr, "\n");

  va_end(argptr);

  cleanup();
  exit(EXIT_FAILURE);
}

long getVal_s( char *string )
{
  char *ptr;
  long val = strtol(string, &ptr, 0);
  if(*ptr != '\0')
    err( "Invalid arguments (not a number)" );

  return val;
}

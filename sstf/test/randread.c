/* 
Here's the code ... crank up 8 of these in parallel and watch
the disk smoke.  Syntax is:

randread <start_dir> <seed> <file_prob.> <dir_factor>

Be sure that some of them start at "/"

Enjoy!
-RG
p.s. the code is largely due to Mike Westall, so please give him
credit if you're writing up experiments on it ...
*/

/* randread.c */

/* This function reads a unix directory tree... It can be */
/* used as a base for a file finder or space computer     */
/* or general command sweeper.                            */

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

/* These constants control the workload generation */

static int     rseed;           /* Seed for generator              */
static float   fileprob;        /* Probability a file will be read */
static float   dirprob;         /* Probability a directory will be */
                                /* processed.                      */
static float   dirfactor;       /* Factor used to reduce dirprob   */
                                /* at each recursive call.         */

char databuf[512];

float unival()
{
   float fval;
   fval = (float)random() / RAND_MAX;
   return(fval);
}

/**/
/* Read a file in sequential hunks of 512 bytes */

readfile(
char *fname)
{
   FILE *input;
   int  blocks;
   int  amt;

   input = fopen(fname, "r");

   if (input != 0)
   {
       blocks = 0;
       do
       {
           amt = fread(databuf, 1, sizeof(databuf), input);
           blocks += 1;
       }   while(amt > 0);
       //printf("  %s %8d \n", fname, blocks);
       fclose(input);
   }
}

/**/
/* Recover the inode data for the current file or directory */

statfile(
char *fname,           /* Name of the file to stat..           */
struct stat *sbuf,     /* Address of the stat return buffer    */
char *cwdbuf)          /* Name of the active directory.        */
{

   int rc;
   char namebuf[512];          /* Fully qualified name buffer. */

   strcpy(namebuf, cwdbuf);
   strcat(namebuf, "/");
   strcat(namebuf, fname);

   rc = lstat(namebuf, sbuf);

}

/**/
/* Process all the files in the directory pointed to by DIRP */

procdir(
DIR *dirp,               /* Open directory handle.           */
char *cwdbuf)            /* Currently active directory       */
{
   struct dirent *dbuf;
   struct stat    sbuf;

/* Read a file or directory name */

   dbuf = readdir(dirp);

   while (dbuf != 0)
   {

   /* Get the inode data */

      statfile(dbuf->d_name, &sbuf, cwdbuf);
      //printf("%04x  %s \n", sbuf.st_mode, dbuf->d_name);
      fflush(stdout);

   /* Recursively process subdirectories.. This is the easy  */
   /* way to aviod recursively processing self or parent but */
   /* it misses any real directories starting with .         */

   /* more rmg mods to prevent hang */
      if (dbuf->d_name[0] != '.' && (S_ISDIR(sbuf.st_mode) ||
         S_ISREG(sbuf.st_mode)) )
      {

      /* The recursive call to nextdir will change the cwd  */
      /* chdir("..") can't be used to get back because of   */
      /* links... 					    */

         if (S_ISDIR(sbuf.st_mode))
         {
            nextdir(dbuf->d_name);    /* Process sub directory */
            chdir(cwdbuf);            /* Change back to current */
         }
         else if (S_ISREG(sbuf.st_mode))
         {
             if (unival() <= fileprob)
                readfile(dbuf->d_name);
         }
      }
      dbuf = readdir(dirp);
   }
}

/**/

/* Start a new directory */

nextdir(
char *dirname)
{
   DIR *dirp;               /* Directory handle */
   char cwdbuf[512];        /* Name buffer.     */

   if (unival() >= dirprob)
      return;

   dirprob *= dirfactor;
   dirp = opendir(dirname);

   if (dirp != 0)
   {
      chdir(dirname);
      getcwd(cwdbuf, sizeof(cwdbuf));
      //printf("\n---> Processing directory %s \n", cwdbuf);
      if(strcmp(cwdbuf,"/proc")) procdir(dirp, cwdbuf);
      closedir(dirp);
   /* chdir("..");   */
   }
   else
   {
      printf("Failed to open directory %s\n", dirname);
      printf("From directory %s \n", cwdbuf);
   }
   dirprob /= dirfactor;
}

main(
int   argc,
char  **argv)
{
   rseed     = 1;
   fileprob  = 1.0;
   dirprob   = 1.0;
   dirfactor = 1.0;

   if (argc > 2)
      sscanf(argv[2], "%d", &rseed);
   if (argc > 3)
      sscanf(argv[3], "%f", &fileprob);
   if (argc > 4)
      sscanf(argv[4], "%f", &dirfactor);

   //fprintf(stderr, "Random reader processing directory %s \n", argv[1]);
   //fprintf(stderr, "Srand = %d.. Fileprob = %f.. Dirfactor = %f \n",
   //                 rseed, fileprob, dirfactor);

   srandom(rseed);
   nextdir(argv[1]);
}



#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2006. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

char *srcdir = NULL, *blddir = NULL;

int verbose = 0, copy_files = 0;

static void
copy_file (char *sname, char *tname, char *pname, int native_make)
{
  char *p;
  int in_fd, out_fd, l;

  unsigned char buf[4096];

  if (strcmp (pname, ".depend") == 0)
    return;

  if (!native_make)
    if (strncmp (pname, "Makefile", 8) == 0)
      return;

  if (strlen (pname) > 6)
    {
      p = pname + strlen (pname) - 6;	/* Seek to the _cfg.[c-h] suffix */
      if (strcmp (p, "_cfg.c") == 0)
	return;
      if (strcmp (p, "_cfg.h") == 0)
	return;
    }

  if (!copy_files)
  {
     symlink (sname, tname);
     return;
  }

  if ((in_fd=open(sname, O_RDONLY, 0))==-1)
     {
	     perror(sname);
	     exit(-1);
     }

  if ((out_fd=creat(tname, 0644))==-1)
     {
	     perror(tname);
	     exit(-1);
     }

  while ((l=read(in_fd, buf, sizeof(buf)))>0)
  {
	  if (write(out_fd, buf, l)!=l)
	     {
		     perror(tname);
		     exit(-1);
	     }
  }

  if (l==-1)
     {
	     perror(sname);
	     exit(-1);
     }

  close(in_fd);
  close(out_fd);
}

static int
copy_tree (char *fromdir, char *tgtdir, int native_make)
{
  DIR *dir;
  struct dirent *de;
  struct stat st;

  if (tgtdir != NULL)
    {
      mkdir (tgtdir, 0700);
    }

  if ((dir = opendir (fromdir)) == NULL)
    {
      fprintf (stderr, "Bad source directory %s\n", fromdir);
    }

  while ((de = readdir (dir)) != NULL)
    {
      char sname[1024], tname[1024];

      sprintf (sname, "%s/%s", fromdir, de->d_name);
      if (tgtdir != NULL)
	sprintf (tname, "%s/%s", tgtdir, de->d_name);
      else
	sprintf (tname, "%s", de->d_name);

      if (stat (sname, &st) == -1)
	{
	  perror (sname);
	  exit (-1);
	}

      if (S_ISDIR (st.st_mode))
	{
	  if (de->d_name[0] != '.')
	    {
	      char tmp[256];
	      int is_native = 0;
	      struct stat st2;

	      sprintf (tmp, "%s/.nativemake", sname);
	      if (stat (tmp, &st2) != -1)
		is_native = 1;
	      printf ("DIR %s->%s\n", sname, tname);
	      copy_tree (sname, tname, is_native);
	    }
	}
      else
	{
	  printf ("FLE %s->%s\n", sname, tname);
	  copy_file (sname, tname, de->d_name, native_make);
	}
    }

  closedir (dir);
}

int
main (int argc, char *argv[])
{
  int i;
  FILE *f;

  time_t t;
  struct tm *tm;

  if (argc < 3)
    {
      fprintf (stderr, "%s: Bad usage\n", argv[0]);
      exit (-1);
    }

  srcdir = argv[1];
  blddir = argv[2];

  for (i = 3; i < argc; i++)
    {
      if (strcmp (argv[i], "-v") == 0)
	{
	  verbose++;
	  continue;
	}

      if (strcmp (argv[i], "-c") == 0)
	{
	  copy_files = 1;
	  continue;
	}
    }

  copy_tree (srcdir, NULL, 0);

#if 0
  if ((f = fopen ("timestamp.h", "w")) == NULL)
    {
      perror ("timestamp.h");
      exit (-1);
    }

  time (&t);
  tm = gmtime (&t);
  fprintf (f, "#define OSS_COMPILE_DATE \"%04d%02d%02d%02d%02d\"\n",
	   tm->tm_year + 1900,
	   tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min);

  fclose (f);
#endif

  exit (0);
}

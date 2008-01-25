/*
 * This file is part of Open Sound System
 *
 * Copyright (C) 4Front Technologies 1996-2007.
 *
 * This software is released under CDDL 1.0 source license for operating systems
 * that have their full source code available under the BSD or CDDL licenses.
 *
 * See the COPYING file included in the main directory of this source
 * distribution. Please contact sales@opensound.com for further info.
 */

/*
 * NOTE! This is not the development version of this file. Please download the
 * sources of the latest Open Sound System version from 
 * http://developer.opensound.com/sources if you plan to make any changes
 * to the software. We may not be able to incorporate changes made to this
 * file back to the official OSS version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#define MAX_SUBDIRS	64
#define MAX_OBJECTS	64
#define MAX_INCLUDES	32
#define MAXOPTS		64

typedef struct
{
  char project_name[64];
  char os_name[64];
  int mode;
#define MD_UNDEF	0
#define MD_USERLAND	1
#define MD_SBIN		2
#define MD_KERNEL	3
#define MD_KERNEL_	4
#define MD_MODULE_	5
#define MD_MODULE	6
#define MD_SHLIB	7
  char cflags[256];
  char ldflags[256];
  char OSflags[256];
  int license;
#define LIC_FREE	0
#define LIC_RESTRICTED	1
  char bus[16];
  char endianess[16];
  char ccomp[256];
  char cplusplus[256];
  char system[32], arch[32], platform[32];

  unsigned int flags;
#define F_USEARCH		0x00000001


  int check_os, os_ok, os_bad;
  int check_cpu, cpu_ok, cpu_bad;
} conf_t;

static conf_t conf = {
  "Open Sound System",
  "Solaris",
  MD_USERLAND,
  "",				/* cflags */
  "",				/* ldflags */
  "",				/* OSflags */
  LIC_FREE,
  "PCI",			/* bus */
  "LITTLE",			/* Endianess */
  "cc",				/* c compiler */
  "c++"				/* cplusplus */
};

static char this_os[64] = "kernel/OS/SunOS";
static int kernelonly = 0;
static int useronly = 0;

static int nincludes = 0;
static char *includes[MAX_INCLUDES];
static int do_cleanup = 0;

static char arch[32] = "";

static int
parse_config (FILE * f, conf_t * conf)
{
  char line[256], *p, *parms;

  while (fgets (line, sizeof (line) - 1, f) != NULL)
    {
      p = line + strlen (line) - 1;
      if (*p == '\n')
	*p = 0;

      if (*line == '#')		/* Comment line */
	continue;

      parms = p = line;
      while (*parms && *parms != '=')
	parms++;

      if (*parms == '=')
	*parms++ = 0;

      if (strcmp (parms, "$GTKCFLAGS") == 0)
	{
	  if (getenv ("GTK2") != NULL)
	    parms = "`pkg-config gtk+-2.0 --cflags`";

	  if (getenv ("GTK1") != NULL)
	    parms = "`gtk-config --cflags` -DGTK1_ONLY";
	}

      if (strcmp (parms, "$GTKLDFLAGS") == 0)
	{
	  if (getenv ("GTK2") != NULL)
	    parms = "`pkg-config gtk+-2.0 --libs`";

	  if (getenv ("GTK1") != NULL)
	    parms = "`gtk-config --libs`";
	}

      if (strcmp (line, "project") == 0)
	{
	  strcpy (conf->project_name, parms);
	  continue;
	}

      if (strcmp (line, "cflags") == 0)
	{
	  strcpy (conf->cflags, parms);
	  continue;
	}

      if (strcmp (line, "ldflags") == 0)
	{
	  strcpy (conf->ldflags, parms);
	  continue;
	}

      if (strcmp (line, "osflags") == 0)
	{
	  if (*conf->OSflags)
	    strcat (conf->OSflags, " ");
	  strcat (conf->OSflags, parms);
	  continue;
	}

      if (strcmp (line, "bus") == 0)
	{
	  strcpy (conf->bus, parms);
	  continue;
	}

      if (strcmp (line, "OS") == 0)
	{
	  strcpy (conf->os_name, parms);
	  continue;
	}

      if (strcmp (line, "mode") == 0)
	{
	  if (strcmp (parms, "user") == 0)
	    {
	      conf->mode = MD_USERLAND;
	      continue;
	    }

	  if (strcmp (parms, "sbin") == 0)
	    {
	      conf->mode = MD_SBIN;
	      continue;
	    }

	  if (strcmp (parms, "shlib") == 0)
	    {
	      conf->mode = MD_SHLIB;
	      continue;
	    }

	  if (strcmp (parms, "kernel") == 0)
	    {
	      conf->mode = MD_KERNEL;
	      continue;
	    }

	  if (strcmp (parms, "undefined") == 0)
	    {
	      conf->mode = MD_UNDEF;
	      continue;
	    }

	  if (strcmp (parms, "kernelmode") == 0)
	    {
	      conf->mode = MD_KERNEL_;
	      continue;
	    }

	  if (strcmp (parms, "module") == 0)
	    {
	      conf->mode = MD_MODULE_;
	      continue;
	    }

	  fprintf (stderr, "Bad mode %s\n", parms);
	  exit (-1);
	}

      if (strcmp (line, "license") == 0)
	{
	  if (strcmp (parms, "free") == 0)
	    conf->license = LIC_FREE;
	  if (strcmp (parms, "restricted") == 0)
	    conf->license = LIC_RESTRICTED;
	  continue;
	}

      if (strcmp (line, "depends") == 0)
	{
	  char tmp[64];
	  sprintf (tmp, "HAVE_%s", parms);
	  if (getenv (tmp) == NULL)
	    {
	      printf
		("Directory depends on the %s package which is not available\n",
		 parms);

	      return 0;
	    }
	  continue;
	}

      if (strcmp (line, "targetos") == 0)
	{
	  conf->check_os = 1;
	  if (strcmp (conf->system, parms) == 0)
	    conf->os_ok = 1;
	  continue;
	}

      if (strcmp (line, "forgetos") == 0)
	{
	  if (strcmp (conf->system, parms) == 0)
	    conf->os_bad = 1;
	  continue;
	}

      if (strcmp (line, "targetcpu") == 0)
	{
	  conf->check_cpu = 1;
	  if (strcmp (conf->arch, parms) == 0)
	    conf->cpu_ok = 1;
	  continue;
	}

      if (strcmp (line, "forgetcpu") == 0)
	{
	  if (strcmp (conf->arch, parms) == 0)
	    conf->cpu_bad = 1;
	  continue;
	}

      if (strcmp (line, "endian") == 0)
	{
	  conf->check_cpu = 1;
	  if (strcmp (conf->endianess, parms) == 0)
	    conf->cpu_ok = 1;
	  continue;
	}

      if (strcmp (line, "platform") == 0)
	{
	  conf->check_cpu = 1;
	  if (strcmp (conf->platform, parms) == 0)
	    conf->cpu_ok = 1;
	  continue;
	}

      printf ("\t     %s\n", line);
      printf ("\t     ^\n");
      printf ("\t*** Unknown parameter ***\n");
    }

  if (conf->os_bad)
    {
      printf ("\tIncompatible OS - directory skipped\n");
      return 0;
    }

  if (conf->check_os && !conf->os_ok)
    {
      printf ("\tIncompatible OS - directory skipped\n");
      return 0;
    }

  if (conf->cpu_bad)
    {
      printf ("\tIncompatible CPU/arch - directory skipped\n");
      return 0;
    }

  if (conf->check_cpu && !conf->cpu_ok)
    {
      printf ("\tIncompatible CPU/arch - directory skipped\n");
      return 0;
    }

  return 1;
}

#if defined(linux)
#include "gen_driver_linux.inc"
#endif

#if defined(__FreeBSD__)
#include "gen_driver_freebsd.inc"
#endif

#if defined(sun)
#include "gen_driver_solaris.inc"
#endif

#if defined(__SCO_VERSION__)
#include "gen_driver_sco.inc"
#endif

static int
is_cplusplus (char *fname)
{
  while (*fname && *fname != '.')
    fname++;

  if (strcmp (fname, ".cpp") == 0)
    return 1;
  if (strcmp (fname, ".C") == 0)
    return 1;

  return 0;
}

static int
cmpstringp (const void *p1, const void *p2)
{
  /* The arguments to this function are "pointers to
   *      pointers to char", but strcmp() arguments are "pointers
   *           to char", hence the following cast plus dereference */

  return strcmp (*(char **) p1, *(char **) p2);
}

static int
scan_dir (char *path, char *name, char *topdir, conf_t * cfg, int level)
{
  int i;
  DIR *dir;
  struct dirent *de;
  struct stat st;
  char tmp[256];
  FILE *f;
  FILE *cf;
  char cfg_name[64];
  char cfg_header[64];
  int cfg_seen = 0;

  conf_t conf;

  int ndirs = 0;
  char *subdirs[MAX_SUBDIRS];

  int nobjects = 0;
  char *objects[MAX_OBJECTS], *obj_src[MAX_OBJECTS];
  int nsources = 0;
  char *sources[MAX_OBJECTS];
  char obj[128], *p, *suffix;
  char *objdir = "OBJDIR";
  int include_local_makefile = 0;

#define MAX_FILENAME 128
  char *filenames[MAX_FILENAME];
  int n_filenames = 0;

  memcpy (&conf, cfg, sizeof (conf));

  if (conf.mode == MD_MODULE_)
    conf.mode = MD_MODULE;

  if (conf.mode == MD_KERNEL_)
    conf.mode = MD_KERNEL;

  sprintf (tmp, "%s/.config", path);
  if ((cf = fopen (tmp, "r")) != NULL)
    {
      if (!parse_config (cf, &conf))
	{
	  /* Not compatible with this environment */
	  fclose (cf);
	  return 0;
	}
      fclose (cf);
    }

  sprintf (tmp, "%s/.nativemake", path);	/* Use the existing makefile */
  if (stat (tmp, &st) != -1)
    {
      return 1;
    }

  sprintf (cfg_name, "%s_cfg.c", name);
  sprintf (cfg_header, "%s_cfg.h", name);

  sprintf (tmp, "%s/Makefile.%s", path, conf.system);
  unlink (tmp);

  sprintf (tmp, "%s/Makefile", path);
  unlink (tmp);

  sprintf (tmp, "%s/.nomake", path);
  if (stat (tmp, &st) != -1)
    return 0;

  sprintf (tmp, "%s/.makefile", path);
  if (stat (tmp, &st) != -1)
    include_local_makefile = 1;

  printf ("Scanning %s\n", path);

  if (kernelonly)
    if (conf.mode == MD_USERLAND || conf.mode == MD_SBIN)
      return 0;

  if (useronly)
    if (conf.mode != MD_USERLAND && conf.mode != MD_SBIN
	&& conf.mode != MD_UNDEF)
      return 0;

  if ((dir = opendir (path)) == NULL)
    {
      perror (path);
      exit (-1);
    }

  while ((de = readdir (dir)) != NULL)
    {
      if (de->d_name[0] == '.')
	continue;

      if (n_filenames >= MAX_FILENAME)
	{
	  fprintf (stderr, "Too many files in directory %s\n", path);
	  exit (-1);
	}

      filenames[n_filenames++] = strdup (de->d_name);
    }

  qsort (filenames, n_filenames, sizeof (char *), cmpstringp);

  for (i = 0; i < n_filenames; i++)
    {
      sprintf (tmp, "%s/%s", path, filenames[i]);
      if (stat (tmp, &st) == -1)
	{
	  perror (tmp);
	  continue;
	}

      if (S_ISDIR (st.st_mode))
	{
	  char top[256];

	  if (topdir == NULL)
	    strcpy (top, "..");
	  else
	    sprintf (top, "../%s", topdir);

	  if (scan_dir (tmp, filenames[i], top, &conf, level + 1))
	    {
	      if (ndirs >= MAX_SUBDIRS)
		{
		  fprintf (stderr, "Too many subdirs in %s\n", path);
		  exit (-1);
		}

	      subdirs[ndirs++] = strdup (filenames[i]);
	    }
	  continue;
	}
      /*      printf("%s/%s\n", path, filenames[i]); */

      if (nobjects >= MAX_OBJECTS || nsources >= MAX_OBJECTS)
	{
	  fprintf (stderr, "Too many objects in s\n", path);
	  exit (-1);
	}

      strcpy (obj, filenames[i]);
      p = obj;
      suffix = "";

      while (*p)
	{
	  if (*p == '.')
	    suffix = p;
	  p++;
	}

      if (strcmp (suffix, ".c") == 0)
	{
	  sources[nsources++] = strdup (obj);
	  if (strcmp (obj, cfg_name) == 0)
	    cfg_seen = 1;
	}

      if (strcmp (suffix, ".c") == 0 ||
	  strcmp (suffix, ".C") == 0 || strcmp (suffix, ".cpp") == 0)
	{
	  obj_src[nobjects] = strdup (obj);
	  *suffix = 0;
	  strcat (obj, ".o");
	  objects[nobjects++] = strdup (obj);
	}
    }

  closedir (dir);
  sprintf (tmp, "%s/.depend", path);
  unlink (tmp);
  sprintf (tmp, "touch %s/.depend", path);
  system (tmp);

  if (level == 1 && *this_os && !useronly)
    {
      subdirs[ndirs++] = strdup (this_os);
    }

  if (!cfg_seen && conf.mode == MD_MODULE)
    {
#if !defined(linux) && !defined(__FreeBSD__)
      sprintf (tmp, "%s_cfg.c", name);
      sources[nsources++] = strdup (tmp);

      obj_src[nobjects] = strdup (tmp);
      sprintf (tmp, "%s_cfg.o", name);
      objects[nobjects++] = strdup (tmp);
#endif
    }
  if (conf.mode == MD_MODULE)
    generate_driver (name, &conf, cfg_name, cfg_header, path, topdir);

  if (do_cleanup || (ndirs == 0 && nobjects == 0))
    {
      return 0;
    }

  sprintf (tmp, "%s/Makefile", path);
#if 0
  if ((f = fopen (tmp, "w")) == NULL)
    {
      perror (tmp);
      exit (-1);
    }

  if (include_local_makefile)
    {
      fprintf (f, "\n");
      fprintf (f, "include .makefile\n");
      fprintf (f, "\n");
    }

  fprintf (f, "all:\n");
  fprintf (f,
	   "\t$(MAKE) $(BUILDFLAGS) BUILDFLAGS=\"$(BUILDFLAGS)\" -f Makefile.`uname -s` all\n\n");

  fprintf (f, "config:\n");
  fprintf (f,
	   "\t$(MAKE) $(BUILDFLAGS) BUILDFLAGS=\"$(BUILDFLAGS)\" -f make.defs config\n\n");

  fprintf (f, "purge:\n");
  fprintf (f,
	   "\t$(MAKE) $(BUILDFLAGS) BUILDFLAGS=\"$(BUILDFLAGS)\" -f make.defs purge\n\n");

  fprintf (f, "dirs:\n");
  fprintf (f,
	   "\t$(MAKE) $(BUILDFLAGS) BUILDFLAGS=\"$(BUILDFLAGS)\" -f Makefile.`uname -s` dirs\n\n");

  fprintf (f, "clean:\n");
  fprintf (f,
	   "\t$(MAKE)  $(BUILDFLAGS) BUILDFLAGS=\"$(BUILDFLAGS)\" -f Makefile.`uname -s` clean\n\n");

  fprintf (f, "lint:\n");
  fprintf (f,
	   "\t$(MAKE)  $(BUILDFLAGS) BUILDFLAGS=\"$(BUILDFLAGS)\" -f Makefile.`uname -s` lint\n\n");

  fprintf (f, "dep:\n");
  fprintf (f,
	   "\t$(MAKE)  $(BUILDFLAGS) BUILDFLAGS=\"$(BUILDFLAGS)\" -f Makefile.`uname -s` dep\n\n");

  fclose (f);

  sprintf (tmp, "%s/Makefile.%s", path, conf.system);
#endif
  if ((f = fopen (tmp, "w")) == NULL)
    {
      perror (tmp);
      exit (-1);
    }

  fprintf (f, "# Makefile for %s module %s\n\n", conf.project_name, name);
  fprintf (f, "CC=%s\n", conf.ccomp);
  fprintf (f, "CPLUSPLUS=%s\n", conf.cplusplus);
#if defined(__SCO_VERSION__)
  if (*conf.cflags != 0)
    fprintf (f, "CFLAGS=%s\n", conf.cflags);
#endif
  if (*conf.ldflags != 0)
    fprintf (f, "LDFLAGS=%s\n", conf.ldflags);
  fprintf (f, "OSFLAGS=%s\n", conf.OSflags);

  fprintf (f, "OS=%s\n", conf.system);
  fprintf (f, "ARCH=%s\n", conf.arch);

  if (topdir == NULL)
    fprintf (f, "TOPDIR=.\n");
  else
    fprintf (f, "TOPDIR=%s\n", topdir);

  fprintf (f, "OBJDIR=$(TOPDIR)/target/objects\n");
  fprintf (f, "TMPDIR=.\n");
  fprintf (f, "MODDIR=$(TOPDIR)/target/modules\n");
  fprintf (f, "BINDIR=$(TOPDIR)/target/bin\n");
  fprintf (f, "SBINDIR=$(TOPDIR)/target/sbin\n");

  fprintf (f, "THISOS=%s\n", this_os);

  if (conf.mode != MD_USERLAND && conf.mode != MD_SBIN)
    {
      fprintf (f, "CFLAGS=-D_KERNEL\n");
#ifdef sun
      /* fprintf(f, "CFLAGS += -xmodel=kernel\n"); */
#endif

#ifdef linux
# if defined(__x86_64__)
      fprintf (f,
	       "CFLAGS += -O6 -fno-common  -mcmodel=kernel -mno-red-zone  -fno-asynchronous-unwind-tables -ffreestanding\n");
# else
      if (getenv ("NO_REGPARM") == NULL)
	{
	  printf ("Compiling with USE_REGPARM\n");
	  fprintf (f,
		   "CFLAGS += -O6 -fno-common -mregparm=3 -DUSE_REGPARM\n");
	}
      else
	{
	  printf ("Compiling with NO_REGPARM\n");
	  fprintf (f, "CFLAGS += -O6 -fno-common -DNO_REGPARM\n");
	}
# endif
      /* fprintf(f, "CFLAGS += -W -Wno-unused -Wno-sign-compare\n"); */
#endif

#if defined(__SCO_VERSION__)
      fprintf (f, "CFLAGS=-O -D_KERNEL -D_DDI=8\n");
#endif

#if defined(__FreeBSD__)
# if defined(__x86_64__)
      fprintf (f,
	       "CFLAGS += -O6 -fno-common  -mcmodel=kernel -mno-red-zone  -fno-asynchronous-unwind-tables -ffreestanding\n");
# endif
#endif

    }
#if !defined(__SCO_VERSION__)
  if (*conf.cflags != 0)
    fprintf (f, "CFLAGS += %s\n", conf.cflags);
#endif
  if (conf.mode != MD_KERNEL)
    objdir = "TMPDIR";

  if (nincludes > 0)
    {
      int i;
      fprintf (f, "INCLUDES=");
      for (i = 0; i < nincludes; i++)
	{
	  if (i > 0)
	    fprintf (f, " ");
	  if (includes[i][0] == '/')
	    fprintf (f, "%s", includes[i]);
	  else
	    fprintf (f, "-I$(TOPDIR)/%s", includes[i]);
	}
      fprintf (f, "\n");
    }

  if (ndirs > 0)
    {
      int i;

      fprintf (f, "SUBDIRS=");
      for (i = 0; i < ndirs; i++)
	{
	  if (i > 0)
	    fprintf (f, " ");
	  fprintf (f, "%s", subdirs[i]);
	}
      fprintf (f, "\n");
    }

  if (nobjects > 0)
    {
      int i;

      fprintf (f, "OBJECTS=");

      for (i = 0; i < nobjects; i++)
	{
	  if (i > 0)
	    fprintf (f, " ");
	  fprintf (f, "$(%s)/%s", objdir, objects[i]);
	}

      fprintf (f, "\n");
    }

  if (conf.mode == MD_MODULE)
    {
      fprintf (f, "TARGETS=$(MODDIR)/%s $(MODDIR)/%s.o\n", name, name);
      fprintf (f, "DEPDIR=\n");
    }
  else if ((conf.mode == MD_USERLAND) && nobjects > 0)
    {
      fprintf (f, "TARGETS=$(BINDIR)/%s\n", name);
      fprintf (f, "DEPDIR=$(BINDIR)/\n");
    }
  else if ((conf.mode == MD_SBIN) && nobjects > 0)
    {
      fprintf (f, "TARGETS=$(SBINDIR)/%s\n", name);
      fprintf (f, "DEPDIR=$(SBINDIR)/\n");
    }
  else
    {
      fprintf (f, "TARGETS=%s\n", name);
      fprintf (f, "DEPDIR=\n");
    }

  if (nsources > 0)
    {
      int i;

      fprintf (f, "CSOURCES=");

      for (i = 0; i < nsources; i++)
	{
	  if (i > 0)
	    fprintf (f, " ");
	  fprintf (f, "%s", sources[i]);
	}

      fprintf (f, "\n");
    }

  fprintf (f, "\n");

  if (include_local_makefile)
    {
      fprintf (f, "include .makefile\n");
      fprintf (f, "\n");
    }

  /*
   * Create the default target
   */
  fprintf (f, "all: ");

  if (conf.mode == MD_USERLAND && nsources > 0)
    {
      fprintf (f, "$(TARGETS) ");
    }
  else if (conf.mode == MD_MODULE)
    {
      if (nobjects > 0)
	fprintf (f, "$(MODDIR)/%s.o ", name);
    }
  else if (conf.mode != MD_KERNEL)
    {
      if (nobjects > 0)
	{
	  if (conf.mode == MD_SBIN)
	    fprintf (f, "$(SBINDIR)/%s ", name);
	  else
	    fprintf (f, "$(BINDIR)/%s ", name);
	}
    }
  else
    {
      if (nobjects > 0)
	fprintf (f, "objects ");
    }

  if (ndirs > 0)
    fprintf (f, "subdirs ");
  fprintf (f, "\n");
#if 0
  if (level == 1)
    fprintf (f,
	     "\t-sh $(THISOS)/build.sh \"$(ARCH)\" \"$(INCLUDES)\" \"$(CFLAGS)\"\n");
  fprintf (f, "\n");
#endif

  /*
   * Create the lint target
   */
  fprintf (f, "lint: ");
  if (nobjects > 0)
    fprintf (f, "lint_sources ");
  if (ndirs > 0)
    fprintf (f, "lint_subdirs ");
  fprintf (f, "\n\n");

  /*
   * Create the dep target
   */
  fprintf (f, "dep: ");
  if (nobjects > 0)
    fprintf (f, "dep_local ");
  if (ndirs > 0)
    fprintf (f, "dep_subdirs ");
  fprintf (f, "\n\n");

  fprintf (f, "include $(TOPDIR)/make.defs\n");
  fprintf (f, "\n");

  if (conf.mode == MD_USERLAND)
    {
      fprintf (f, "%s:\t$(BINDIR)/%s\n\n", name, name);

      fprintf (f, "$(BINDIR)/%s:\t$(OBJECTS)\n", name);
      fprintf (f,
	       "\t$(CC) $(CFLAGS) $(LIBRARIES) $(LDFLAGS) -s -o $(BINDIR)/%s $(OBJECTS)\n",
	       name);
      fprintf (f, "\n\n");
    }

  if (conf.mode == MD_SBIN)
    {
      fprintf (f, "%s:\t$(SBINDIR)/%s\n\n", name, name);

      fprintf (f, "$(SBINDIR)/%s:\t$(OBJECTS)\n", name);
      fprintf (f,
	       "\t$(CC) $(CFLAGS) $(LIBRARIES) $(LDFLAGS) -s -o $(SBINDIR)/%s $(OBJECTS)\n",
	       name);
      fprintf (f, "\n\n");
    }

  if (conf.mode == MD_MODULE)
    {
      fprintf (f, "$(MODDIR)/%s.o:\t$(OBJECTS)\n", name);
      fprintf (f, "\t$(LD) $(LDARCH) -r -o $(MODDIR)/%s.o $(OBJECTS)\n",
	       name);
      fprintf (f, "\n\n");
    }

  if (nobjects > 0)
    {
      int i;

      for (i = 0; i < nobjects; i++)
	{
	  fprintf (f, "$(%s)/%s:\t%s\n", objdir, objects[i], obj_src[i]);

	  if (is_cplusplus (obj_src[i]))
	    fprintf (f,
		     "\t$(CPLUSPLUS) -c $(CFLAGS) $(OSFLAGS) $(INCLUDES) %s -o $(%s)/%s\n",
		     obj_src[i], objdir, objects[i]);
	  else
	    fprintf (f,
		     "\t$(CC) -c $(CFLAGS) $(OSFLAGS) $(LIBRARIES) $(INCLUDES) %s -o $(%s)/%s\n",
		     obj_src[i], objdir, objects[i]);
	  fprintf (f, "\n");
	}
    }

  fprintf (f, "clean: clean_local");
  if (ndirs > 0)
    fprintf (f, " clean_subdirs");
  fprintf (f, "\n\n");

  fclose (f);
  return 1;
}

static int already_configured = 0;

static void
produce_output (conf_t * conf)
{
  if (already_configured)
    return;

  scan_dir (".", "main", NULL, conf, 1);
  scan_dir (this_os, "main", "../../..", conf, 3);

  already_configured = 1;
}

#ifdef linux
#include "srcconf_linux.inc"
#endif

#ifdef __FreeBSD__
#include "srcconf_freebsd.inc"
#endif

#ifdef sun
#include "srcconf_solaris.inc"
#endif

static void
check_endianess (conf_t * conf)
{
  unsigned char probe[4] = { 1, 2, 3, 4 };

  if ((*(unsigned int *) &probe) == 0x01020304)
    {
      strcat (conf->OSflags, " -DOSS_BIG_ENDIAN");
      strcpy (conf->endianess, "BIG");
    }
  else
    {
      strcat (conf->OSflags, " -DOSS_LITTLE_ENDIAN");
      strcpy (conf->endianess, "LITTLE");
    }
}

static void
check_system (conf_t * conf)
{
  struct utsname un;

  if (uname (&un) == -1)
    {
      perror ("uname");
      exit (-1);
    }

  if (strcmp (un.sysname, "UnixWare") == 0)
    strcpy (un.sysname, "SCO_SV");
  printf ("System: %s\n", un.sysname);
  strcpy (conf->system, un.sysname);
  sprintf (this_os, "kernel/OS/%s", un.sysname);
  printf ("Release: %s\n", un.release);
  printf ("Machine: %s\n", un.machine);
  strcpy (conf->arch, un.machine);

#ifdef HAVE_SYSDEP
  check_sysdep (conf, &un);
#else
  if (strcmp (un.machine, "i386") == 0 ||
      strcmp (un.machine, "i486") == 0 ||
      strcmp (un.machine, "i586") == 0 || strcmp (un.machine, "i686") == 0)
    {
      strcpy (conf->platform, "i86pc");
    }
  else
    {
      fprintf (stderr, "Cannot determine the platform for %s\n", un.machine);
      exit (-1);
    }
#endif

  if (*conf->platform == 0)
    {
      fprintf (stderr, "Panic: No platform\n");
      exit (-1);
    }

  check_endianess (conf);
}

int
main (int argc, char *argv[])
{
  int i;

  for (i = 1; i < argc; i++)
    if (argv[i][0] == '-')
      switch (argv[i][1])
	{
	case 'A':
	  strcpy (arch, &argv[i][2]);
	  printf ("Arch=%s\n", arch);
	  break;
	case 'K':
	  kernelonly = 1;
	  break;		/* Compile only the kernel mode parts */
	case 'U':
	  useronly = 1;
	  break;		/* Compile only the user land utilities */
	}


#if defined(linux) || defined(__FreeBSD__) || defined(__SCO_VERSION__)
  mkdir ("target", 0755);
  mkdir ("target/build", 0755);
  system ("touch target/build/.nomake");
#endif

  if (getenv ("SOL9") != NULL)
    system ("touch kernel/drv/ossusb/.nomake");

  check_system (&conf);

  produce_output (&conf);
  exit (0);
}

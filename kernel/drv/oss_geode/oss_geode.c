/*
 * Purpose: Driver for the NS/Cyrix Geode AC97 audio controller
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2001-2007. All rights reserved.

#include "oss_geode_cfg.h"
#include "oss_pci.h"
#include "ac97.h"

#define CYRIX_VENDOR_ID			0x1078
#define CYRIX_GEODE			0x0103
#define NATIONAL_VENDOR_ID		0x100b
#define NATIONAL_SC1200			0x0503

#ifdef USE_POLL
static void geode_poll (unsigned long dummy);
DEFINE_TIMER (geode_timer, geode_poll);
#endif

#define MAX_PORTC 2

typedef struct
{
  unsigned int ptr;
  unsigned int size;
#define PRD_EOT			0x80000000
#define PRD_EOP			0x40000000
#define PRD_JMP			0x20000000
}
PRD_rec;

typedef struct
{
  int open_mode;
  int speed;
  int bits;
  int channels;
  int trigger_bits;
  int audio_enabled;
  int audiodev;
}
geode_portc;

typedef struct
{
  oss_device_t *osdev;
  int physaddr;
  void *linaddr;
  unsigned int *f3bar;
  int irq;
  char *chip_name;
  oss_mutex_t mutex;
  oss_mutex_t low_mutex;

  int mixer_dev;
  ac97_devc ac97devc;

  geode_portc portc[MAX_PORTC];
  int open_mode;

  PRD_rec *prdin, *prdout;
  unsigned long prdin_phys, prdout_phys;
}
geode_devc;

#ifdef USE_POLL
static int poll_started = 0, poll_interval = 1;
#endif

extern int geode_use_pollmode;
extern int geode_use_src;

static int
geodeintr (oss_device_t * osdev)
{

  geode_devc *devc = osdev->devc;
  geode_portc *portc;
  int i, n;
  int serviced = 0;
  unsigned int pos;
  int ptr;

#if 0
  if (poll_started)
    {
      poll_started = 0;
      REMOVE_TIMER (geode_timer, geode_poll);
    }
#endif

  for (i = 0; i < MAX_PORTC; i++)
    {
      portc = &devc->portc[i];

      if (portc->trigger_bits & PCM_ENABLE_OUTPUT)
	{
	  dmap_t *dmap;
	  dmap = audio_engines[portc->audiodev]->dmap_out;
	  pos = PCI_READL (devc->osdev, devc->f3bar + 0x24);
	  pos = (pos - devc->prdout_phys) / 8;
	  ptr = pos;
	  ptr--;

	  if (ptr < 0)
	    ptr = 0;
	  ptr %= dmap->nfrags;

	  n = 0;
	  while (ptr != dmap_get_qhead (dmap) && n++ < dmap->nfrags)
	    oss_audio_outputintr (portc->audiodev, 0);
	  serviced = 1;
	}


      if (portc->trigger_bits & PCM_ENABLE_INPUT)
	{
	  dmap_t *dmap;

	  dmap = audio_engines[portc->audiodev]->dmap_in;

	  pos = PCI_READL (devc->osdev, devc->f3bar + 0x2c);
	  pos = (pos - devc->prdin_phys) / 8;
	  ptr = pos;
	  ptr--;

	  if (ptr < 0)
	    ptr = 0;
	  ptr %= dmap->nfrags;

	  n = 0;
	  while (ptr != dmap_get_qtail (dmap) && n++ < dmap->nfrags)
	    oss_audio_inputintr (portc->audiodev, 0);

	  serviced = 1;
	}
    }
  return serviced;
}

#ifdef USE_POLL
static void
geode_poll (unsigned long dummy)
{
  unsigned int pos;
  int ptr;
  int i, n, devno;

  for (devno = 0; devno < ngeodedevs; devno++)
    {
      geode_devc *devc = &geode_info[devno];
      oss_native_word flags;

      for (i = 0; i < MAX_PORTC; i++)
	{
	  geode_portc *portc;
	  portc = &devc->portc[i];

	  if (portc->trigger_bits & PCM_ENABLE_OUTPUT)
	    {
	      dmap_t *dmap;

	      MUTEX_ENTER (devc->mutex, flags);
	      dmap = audio_engines[portc->audiodev]->dmap_out;
	      pos = PCI_READL (devc->osdev, devc->f3bar + 0x24);
	      pos = (pos - devc->prdout_phys) / 8;
	      ptr = pos;
	      ptr--;

	      if (ptr < 0)
		ptr = 0;
	      ptr %= dmap->nfrags;

	      n = 0;
	      MUTEX_EXIT (devc->mutex, flags);

	      while (ptr != dmap_get_qhead (dmap) && n++ < dmap->nfrags)
		oss_audio_outputintr (portc->audiodev, 0);
	    }


	  if (portc->trigger_bits & PCM_ENABLE_INPUT)
	    {
	      dmap_t *dmap;

	      MUTEX_ENTER (devc->mutex, flags);
	      dmap = audio_engines[portc->audiodev]->dmap_in;

	      pos = PCI_READL (devc->osdev, devc->f3bar + 0x2c);
	      pos = (pos - devc->prdin_phys) / 8;
	      ptr = pos;
	      ptr--;

	      if (ptr < 0)
		ptr = 0;
	      ptr %= dmap->nfrags;

	      n = 0;

	      MUTEX_EXIT (devc->mutex, flags);
	      while (ptr != dmap_get_qtail (dmap) && n++ < dmap->nfrags)
		oss_audio_inputintr (portc->audiodev, 0);
	    }
	}
    }
  if (poll_interval < 1)
    poll_interval = 1;
  ACTIVATE_TIMER (geode_timer, geode_poll, poll_interval);
}
#endif


static int
codec_valid_data (geode_devc * devc, int command, unsigned int *data)
{
  int y;
  for (y = 0; y < 1000; y++)
    {
      *data = PCI_READL (devc->osdev, devc->f3bar + 0x08);
      if ((*data & 0x7F000000) != (command & 0x7F000000))
	continue;
      if ((*data & 0x00030000) == 0x00030000)
	return 1;
    }
  return 0;
}

static int
ac97_read (void *devc_, int wAddr)
{
  geode_devc *devc = devc_;
  int i;
  unsigned int x, y;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);

  for (i = 0; i < 10000; i++)
    if (!(PCI_READL (devc->osdev, devc->f3bar + 0x0c) & 0x10000))
      break;

  for (i = 0; i < 10000; i++)
    {
      MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);

      MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);
      x = (wAddr << 24) | 0x80000000;
      PCI_WRITEL (devc->osdev, devc->f3bar + 0x0c, x);

      if (codec_valid_data (devc, x, &y))
	{
	  MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);
	  return y & 0xffff;
	}
    }

  MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);
  return -1;
}

static int
ac97_write (void *devc_, int wAddr, int wData)
{
  geode_devc *devc = devc_;
  unsigned int tmp, i;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);

  tmp = (wAddr << 24) | wData;
  PCI_WRITEL (devc->osdev, devc->f3bar + 0x0c, tmp);

  /* wait for codec to be ready */
  for (i = 0; i <= 10000; i++)
    if (!(PCI_READL (devc->osdev, devc->f3bar + 0x0c) & 0x10000))
      break;

  if (i > 10000)
    {
      cmn_err (CE_WARN, "AC97 write timeout\n");
    }

  MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);

  return 0;
}

static int
geode_audio_set_rate (int dev, int arg)
{
  geode_portc *portc = audio_engines[dev]->portc;
  if (arg == 0)
    return portc->speed;

  if (audio_engines[dev]->flags & ADEV_FIXEDRATE)
    arg = 48000;

  if (arg > 48000)
    arg = 48000;
  if (arg < 8000)
    arg = 8000;
  portc->speed = arg;
  return portc->speed;
}

static short
geode_audio_set_channels (int dev, short arg)
{
  geode_portc *portc = audio_engines[dev]->portc;

  if (arg == 0)
    return portc->channels;

  if (audio_engines[dev]->flags & ADEV_STEREOONLY)
    arg = 2;

  if ((arg != 1) && (arg != 2))
    return portc->channels;
  portc->channels = arg;

  return portc->channels;
}

static unsigned int
geode_audio_set_format (int dev, unsigned int arg)
{
  geode_portc *portc = audio_engines[dev]->portc;

  if (arg == 0)
    return portc->bits;

  if (audio_engines[dev]->flags & ADEV_16BITONLY)
    arg = 16;

  if (!(arg & (AFMT_U8 | AFMT_S16_LE)))
    return portc->bits;
  portc->bits = arg;
  return portc->bits;
}

/*ARGSUSED*/
static int
geode_audio_ioctl (int dev, unsigned int cmd, ioctl_arg arg)
{
  return -EINVAL;
}

static void geode_audio_trigger (int dev, int state);

static void
geode_audio_reset (int dev)
{
  geode_audio_trigger (dev, 0);
}

static void
geode_audio_reset_input (int dev)
{
  geode_portc *portc = audio_engines[dev]->portc;
  geode_audio_trigger (dev, portc->trigger_bits & ~PCM_ENABLE_INPUT);
}

static void
geode_audio_reset_output (int dev)
{
  geode_portc *portc = audio_engines[dev]->portc;
  geode_audio_trigger (dev, portc->trigger_bits & ~PCM_ENABLE_OUTPUT);
}

/*ARGSUSED*/
static int
geode_audio_open (int dev, int mode, int open_flags)
{
  geode_portc *portc = audio_engines[dev]->portc;
  geode_devc *devc = audio_engines[dev]->devc;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  if (portc->open_mode)
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return -EBUSY;
    }

  if (devc->open_mode & mode)
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return -EBUSY;
    }

  devc->open_mode |= mode;

  portc->open_mode = mode;
  portc->audio_enabled &= ~mode;

  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

#ifdef USE_POLL
/* Startup the poll timer, if interruts work, the poll timer is disabled in
 *  the interrupt service routine otherwise geode_poll() is called */
  if (geode_use_pollmode)
    if (!poll_started)
      {
	poll_interval = OSS_HZ / 50;
	if (poll_interval < 1)
	  poll_interval = 1;
	INIT_TIMER (geode_timer, geode_poll);
	ACTIVATE_TIMER (geode_timer, geode_poll, poll_interval);
	poll_started = 1;
      }
#endif
  return 0;
}

static void
geode_audio_close (int dev, int mode)
{
  geode_portc *portc = audio_engines[dev]->portc;
  geode_devc *devc = audio_engines[dev]->devc;

  geode_audio_reset (dev);
  portc->open_mode = 0;
  devc->open_mode &= ~mode;
  portc->audio_enabled &= ~mode;
}

/*ARGSUSED*/
static void
geode_audio_output_block (int dev, oss_native_word buf, int count,
			  int fragsize, int intrflag)
{
  geode_portc *portc = audio_engines[dev]->portc;

  portc->audio_enabled |= PCM_ENABLE_OUTPUT;
  portc->trigger_bits &= ~PCM_ENABLE_OUTPUT;
}

/*ARGSUSED*/
static void
geode_audio_start_input (int dev, oss_native_word buf, int count,
			 int fragsize, int intrflag)
{
  geode_portc *portc = audio_engines[dev]->portc;

  portc->audio_enabled |= PCM_ENABLE_INPUT;
  portc->trigger_bits &= ~PCM_ENABLE_INPUT;
}

static void
geode_audio_trigger (int dev, int state)
{
  geode_devc *devc = audio_engines[dev]->devc;
  geode_portc *portc = audio_engines[dev]->portc;
  unsigned char *f3bar_b = (unsigned char *) devc->f3bar;
  int i;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);

  if (portc->open_mode & OPEN_WRITE)
    {
      if (state & PCM_ENABLE_OUTPUT)
	{
	  if ((portc->audio_enabled & PCM_ENABLE_OUTPUT) &&
	      !(portc->trigger_bits & PCM_ENABLE_OUTPUT))
	    {
	      f3bar_b[0x20] = 0x01;
	      f3bar_b[0x21] = 0x00;
	      portc->trigger_bits |= PCM_ENABLE_OUTPUT;
	    }
	}
      else
	{
	  if ((portc->audio_enabled & PCM_ENABLE_OUTPUT) &&
	      (portc->trigger_bits & PCM_ENABLE_OUTPUT))
	    {
	      portc->audio_enabled &= ~PCM_ENABLE_OUTPUT;
	      portc->trigger_bits &= ~PCM_ENABLE_OUTPUT;

	      for (i = 0; i < 512; i++)
		{
		  devc->prdout[i].size = PRD_EOT;	/* Stop */
		}
	    }
	}
    }

  if (portc->open_mode & OPEN_READ)
    {
      if (state & PCM_ENABLE_INPUT)
	{
	  if ((portc->audio_enabled & PCM_ENABLE_INPUT) &&
	      !(portc->trigger_bits & PCM_ENABLE_INPUT))
	    {

	      f3bar_b[0x28] = 0x09;
	      f3bar_b[0x29] = 0x00;
	      portc->trigger_bits |= PCM_ENABLE_INPUT;
	    }
	}
      else
	{
	  if ((portc->audio_enabled & PCM_ENABLE_INPUT) &&
	      (portc->trigger_bits & PCM_ENABLE_INPUT))
	    {
	      portc->audio_enabled &= ~PCM_ENABLE_INPUT;
	      portc->trigger_bits &= ~PCM_ENABLE_INPUT;

	      for (i = 0; i < 512; i++)
		{
		  devc->prdin[i].size = PRD_EOT;	/* Stop */
		}
	    }
	}
    }
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}

/*ARGSUSED*/
static int
geode_audio_prepare_for_input (int dev, int bsize, int bcount)
{
  geode_devc *devc = audio_engines[dev]->devc;
  geode_portc *portc = audio_engines[dev]->portc;
  dmap_t *dmap = audio_engines[dev]->dmap_in;
  unsigned char *f3bar_b = (unsigned char *) devc->f3bar;
  int i, stat;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  ac97_recrate (&devc->ac97devc, portc->speed);

#if 0
  if (dmap->nfrags > 256)
    {
      dmap->nfrags = 256;
      dmap->bytes_in_use = 256 * dmap->fragment_size;
    }
#endif


  /* clear out the prd table */
  memset (devc->prdin, 0, 512 * sizeof (PRD_rec));

  /* Clear DMA Bus Master Status */
  stat = f3bar_b[0x29];
  stat++;			/* To supress warnings by lint */

  /* Initialize PRD entries */
  for (i = 0; i < dmap->nfrags; i++)
    {
      devc->prdin[i].ptr = dmap->dmabuf_phys + (i * dmap->fragment_size);
      if (geode_use_pollmode)
	devc->prdin[i].size = dmap->fragment_size;
      else
	devc->prdin[i].size = dmap->fragment_size | PRD_EOP;
    }

  /* Initialize the JMP entry back to the beginning */
  devc->prdin[dmap->nfrags].ptr = devc->prdin_phys;
  if (geode_use_pollmode)
    devc->prdin[dmap->nfrags].size = PRD_JMP;
  else
    devc->prdin[dmap->nfrags].size = PRD_JMP | PRD_EOP;

  PCI_WRITEL (devc->osdev, devc->f3bar + 0x2c, devc->prdin_phys);

  portc->audio_enabled &= ~PCM_ENABLE_INPUT;
  portc->trigger_bits &= ~PCM_ENABLE_INPUT;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
  return 0;
}

/*ARGSUSED*/
static int
geode_audio_prepare_for_output (int dev, int bsize, int bcount)
{
  geode_devc *devc = audio_engines[dev]->devc;
  geode_portc *portc = audio_engines[dev]->portc;
  dmap_t *dmap = audio_engines[dev]->dmap_out;
  unsigned char *f3bar_b = (unsigned char *) devc->f3bar;
  int i, stat;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  ac97_playrate (&devc->ac97devc, portc->speed);
#if 0
  if (dmap->nfrags > 256)
    {
      dmap->nfrags = 256;
      dmap->bytes_in_use = 256 * dmap->fragment_size;
    }
#endif
  /* clear out the PRD table */
  memset (devc->prdout, 0, 512 * sizeof (PRD_rec));


  /* Initialize PRD entries */
  for (i = 0; i < dmap->nfrags; i++)
    {
      devc->prdout[i].ptr = dmap->dmabuf_phys + (i * dmap->fragment_size);
      if (geode_use_pollmode)
	devc->prdout[i].size = dmap->fragment_size;
      else
	devc->prdout[i].size = dmap->fragment_size | PRD_EOP;
    }

  /* Initialize the JMP entry back to the beginning */
  devc->prdout[dmap->nfrags].ptr = devc->prdout_phys;
  if (geode_use_pollmode)
    devc->prdout[dmap->nfrags].size = PRD_JMP;
  else
    devc->prdout[dmap->nfrags].size = PRD_JMP | PRD_EOP;

  PCI_WRITEL (devc->osdev, devc->f3bar + 0x24, devc->prdout_phys);

  /* Clear DMA Bus master status */
  stat = f3bar_b[0x21];
  stat++;			/* To supress warnings by lint */
  portc->audio_enabled &= ~PCM_ENABLE_OUTPUT;
  portc->trigger_bits &= ~PCM_ENABLE_OUTPUT;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  return 0;
}

static int
geode_get_buffer_pointer (int dev, dmap_t * dmap, int direction)
{
  geode_devc *devc = audio_engines[dev]->devc;
  int ptr = 0;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);
  if (direction == PCM_ENABLE_OUTPUT)
    {
      ptr = PCI_READL (devc->osdev, devc->f3bar + 0x24);
    }
  if (direction == PCM_ENABLE_INPUT)
    {
      ptr = PCI_READL (devc->osdev, devc->f3bar + 0x2c);
    }
  MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);
  return ptr - dmap->dmabuf_phys;
}

static const audiodrv_t geode_audio_driver = {
  geode_audio_open,
  geode_audio_close,
  geode_audio_output_block,
  geode_audio_start_input,
  geode_audio_ioctl,
  geode_audio_prepare_for_input,
  geode_audio_prepare_for_output,
  geode_audio_reset,
  NULL,
  NULL,
  geode_audio_reset_input,
  geode_audio_reset_output,
  geode_audio_trigger,
  geode_audio_set_rate,
  geode_audio_set_format,
  geode_audio_set_channels,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,				/* geode_alloc_buffer */
  NULL,				/* geode_free_buffer */
  NULL,
  NULL,
  geode_get_buffer_pointer
};

static int
init_geode (geode_devc * devc)
{
  int i, caps, opts;
  int first_dev = 0;
  oss_native_word phaddr;

  /* Allocate the buffers for the prdin/prdout tables */
  devc->prdin =
    (PRD_rec *) CONTIG_MALLOC (devc->osdev, 512 * sizeof (PRD_rec),
			       MEMLIMIT_32BITS, &phaddr);
  if (devc->prdin == NULL)
    {
      cmn_err (CE_WARN, "Can't allocate memory for PRD input tables\n");
      return 0;
    }

  devc->prdin_phys = phaddr;

  devc->prdout =
    (PRD_rec *) CONTIG_MALLOC (devc->osdev, 512 * sizeof (PRD_rec),
			       MEMLIMIT_32BITS, &phaddr);
  if (devc->prdout == NULL)
    {
      cmn_err (CE_WARN, "Can't allocate memory for PRD output tables\n");
      return 0;
    }

  devc->prdout_phys = phaddr;

  if (!geode_use_pollmode)
    {
      /* VSA2 IRQ config method */
      OUTW (devc->osdev, 0xFC53, 0xAC1C);
      OUTW (devc->osdev, 0x108, 0xAC1C);
      OUTW (devc->osdev, devc->irq, 0xAC1E);

      /* VSA1 IRQ config method */
      OUTL (devc->osdev, 0x800090D0, 0x0CF8);
      OUTL (devc->osdev, (devc->irq << 16) | 0xA00A, 0x0CFC);
      oss_udelay (10000);
    }

  /* Now configure the OSS devices */

  devc->mixer_dev =
    ac97_install (&devc->ac97devc, "AC97 Mixer", ac97_read, ac97_write, devc,
		  devc->osdev);

  if (devc->mixer_dev < 0)
    return 0;

  opts = ADEV_AUTOMODE | ADEV_STEREOONLY | ADEV_16BITONLY;

  if (!ac97_varrate (&devc->ac97devc))
    {
      opts |= ADEV_FIXEDRATE;
    }

  if (geode_use_src)
    {
      opts |= ADEV_FIXEDRATE;
    }

  for (i = 0; i < MAX_PORTC; i++)
    {
      int adev;
      geode_portc *portc = &devc->portc[i];
      char tmp_name[100];

      if (i == 0)
	{
	  strcpy (tmp_name, devc->chip_name);
	  caps = opts | ADEV_DUPLEX;
	}
      else
	{
	  strcpy (tmp_name, devc->chip_name);
	  caps = opts | ADEV_DUPLEX | ADEV_SHADOW;
	}

      if ((adev = oss_install_audiodev (OSS_AUDIO_DRIVER_VERSION,
					devc->osdev,
					devc->osdev,
					tmp_name,
					&geode_audio_driver,
					sizeof (audiodrv_t),
					caps, AFMT_S16_LE, NULL, -1)) < 0)
	{
	  adev = -1;
	  return 0;
	}
      else
	{
	  if (i == 0)
	    first_dev = adev;
	  audio_engines[adev]->devc = devc;
	  audio_engines[adev]->portc = portc;
	  audio_engines[adev]->rate_source = first_dev;
	  audio_engines[adev]->mixer_dev = devc->mixer_dev;
	  audio_engines[adev]->min_rate = 8000;
	  audio_engines[adev]->max_rate = 48000;
	  audio_engines[adev]->caps |= PCM_CAP_FREERATE;
	  portc->open_mode = 0;
	  portc->audiodev = adev;
	  portc->audio_enabled = 0;
	  if (caps & ADEV_FIXEDRATE)
	    {
	      audio_engines[adev]->fixed_rate = 48000;
	      audio_engines[adev]->min_rate = 48000;
	    }
	}

    }
  return 1;
}

int
oss_geode_attach (oss_device_t * osdev)
{
  unsigned char pci_revision /*, pci_latency */ ;
  unsigned short pci_command, vendor, device;
  unsigned int pci_ioaddr;
  int err;
  geode_devc *devc;

  DDB (cmn_err (CE_WARN, "Entered Geode probe routine\n"));

  pci_read_config_word (osdev, PCI_VENDOR_ID, &vendor);
  pci_read_config_word (osdev, PCI_DEVICE_ID, &device);
  if (vendor != CYRIX_VENDOR_ID || device != CYRIX_GEODE)
    if (vendor != NATIONAL_VENDOR_ID || device != NATIONAL_SC1200)
      return 0;
  pci_read_config_byte (osdev, PCI_REVISION_ID, &pci_revision);
  pci_read_config_word (osdev, PCI_COMMAND, &pci_command);
  pci_read_config_dword (osdev, PCI_MEM_BASE_ADDRESS_0, &pci_ioaddr);

  pci_command |= PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY;
  pci_write_config_word (osdev, PCI_COMMAND, pci_command);

  if (pci_ioaddr == 0)
    {
      cmn_err (CE_WARN, "BAR0 not initialized by BIOS\n");
      return 0;
    }


  if ((devc = PMALLOC (osdev, sizeof (*devc))) == NULL)
    {
      cmn_err (CE_WARN, "Out of memory\n");
      return 0;
    }

  devc->osdev = osdev;
  osdev->devc = devc;
  devc->physaddr = pci_ioaddr;
  devc->irq = 5;
  devc->chip_name = "Geode/NS5530 AC97 Controller";

  MUTEX_INIT (devc->osdev, devc->mutex, MH_DRV);
  MUTEX_INIT (devc->osdev, devc->low_mutex, MH_DRV + 1);

  oss_register_device (osdev, devc->chip_name);

  if ((err = oss_register_interrupts (devc->osdev, 0, geodeintr, NULL)) < 0)
    {
      cmn_err (CE_WARN, "Can't register interrupt handler, err=%d\n", err);
      return 0;
    }

  /* Now map the Memory space */
  devc->linaddr = MAP_PCI_MEM (devc->osdev, 0, devc->physaddr, 0x60000);
  devc->f3bar = (unsigned int *) devc->linaddr;

  return init_geode (devc);	/* Detected */
}


int
oss_geode_attach (oss_device_t * osdev)
{
  geode_devc *devc = (geode_devc *) osdev->devc;
  unsigned char *f3bar_b = (unsigned char *) devc->f3bar;

#ifdef USE_POLL
  if (poll_started)
    {
      poll_started = 0;
      REMOVE_TIMER (geode_timer, geode_poll);
    }
#endif

  if (oss_disable_device (osdev) < 0)
    return 0;

  f3bar_b[0x20] = 0;		/* Disable output */
  f3bar_b[0x28] = 0;		/* Disable input */
  PCI_WRITEL (devc->osdev, devc->f3bar + 0x24, 0);
  PCI_WRITEL (devc->osdev, devc->f3bar + 0x2c, 0);

  oss_unregister_interrupts (devc->osdev);

  MUTEX_CLEANUP (devc->mutex);
  MUTEX_CLEANUP (devc->low_mutex);

  UNMAP_PCI_MEM (devc->osdev, 0, devc->physaddr, devc->linaddr, 0x60000);

  if (devc->prdin != NULL)
    CONTIG_FREE (devc->osdev, devc->prdin, 512 * sizeof (PRD_rec));
  if (devc->prdout != NULL)
    CONTIG_FREE (devc->osdev, devc->prdout, 512 * sizeof (PRD_rec));

  devc->prdin = devc->prdout = NULL;
  oss_unregister_device (devc->osdev);

  return 1;
}
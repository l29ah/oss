/*
 * Purpose: Kernel space support module for user land audio/mixer drivers
 *
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2008. All rights reserved.

#include "oss_userdev_cfg.h"
#include <oss_userdev_exports.h>
#include "userdev.h"

oss_device_t *userdev_osdev = NULL;
static int client_dev = -1, server_dev = -1; /* Control devces */

/*
 * Global device lists and the mutex that protects them.
 */
oss_mutex_t userdev_global_mutex;

/*
 * The oss_userdev driver creates new device pairs on-demand. All device
 * pairs that are not in use will be kept in the userdev_free_device_list
 * (linked) list. If this list contains any entries then they will be
 * reused whenever a new device pair is required.
 */
userdev_devc_t *userdev_free_device_list = NULL;

/*
 * Linked list for device pairs that have a server attached. These device 
 * pairs are available for the clients.
 */
userdev_devc_t *userdev_active_device_list = NULL;

/*ARGSUSED*/
static int
userdev_server_redirect (int dev, int mode, int open_flags)
{
/*
 * Purpose: This entry point is used to create new userdev instances and to redirect clients to them.
 */
  int server_engine;

cmn_err(CE_CONT, "userdev_server_redirect(%d, %d, %x)\n", dev, mode, open_flags);

  if ((server_engine=usrdev_find_free_device_pair()) >= 0)
     return server_engine;
  
  return userdev_create_device_pair();
}

/*ARGSUSED*/
static int
userdev_client_redirect (int dev, int mode, int open_flags)
{
/*
 * Purpose: This entry point is used to create new userdev instances and to redirect clients to them.
 */
cmn_err(CE_CONT, "userdev_client_redirect(%d, %d, %x)\n", dev, mode, open_flags);
  return -EIO;
}

/*
 * Dummy audio driver entrypoint functions. 
 *
 * Functionality of the control device is handled by userdev_[client|server]_redirect().
 * The other entry points are not used for any purpose but the audio core
 * framework expects to see them.
 */
/*ARGSUSED*/
static int
userdev_control_set_rate (int dev, int arg)
{
  /* Dumy routine - Not actually used */
  return 48000;
}

/*ARGSUSED*/
static short
userdev_control_set_channels (int dev, short arg)
{
  /* Dumy routine - Not actually used */
	return 2;
}

/*ARGSUSED*/
static unsigned int
userdev_control_set_format (int dev, unsigned int arg)
{
  /* Dumy routine - Not actually used */
	return AFMT_S16_NE;
}

static void
userdev_control_reset (int dev)
{
  /* Dumy routine - Not actually used */
}

/*ARGSUSED*/
static int
userdev_control_open (int dev, int mode, int open_flags)
{
  /* Dumy routine - Not actually used */
  return -EIO;
}

/*ARGSUSED*/
static void
userdev_control_close (int dev, int mode)
{
  /* Dumy routine - Not actually used */
}

/*ARGSUSED*/
static int
userdev_control_ioctl (int dev, unsigned int cmd, ioctl_arg arg)
{
  /* Dumy routine - Not actually used */
  return -EINVAL;
}

/*ARGSUSED*/
static void
userdev_control_output_block (int dev, oss_native_word buf, int count, int fragsize,
			int intrflag)
{
  /* Dumy routine - Not actually used */
}

/*ARGSUSED*/
static void
userdev_control_start_input (int dev, oss_native_word buf, int count, int fragsize,
		       int intrflag)
{
  /* Dumy routine - Not actually used */
}

/*ARGSUSED*/
static int
userdev_control_prepare_for_input (int dev, int bsize, int bcount)
{
  /* Dumy routine - Not actually used */
	return -EIO;
}

/*ARGSUSED*/
static int
userdev_control_prepare_for_output (int dev, int bsize, int bcount)
{
  /* Dumy routine - Not actually used */
  return -EIO;
}

static audiodrv_t userdev_server_control_driver = {
  userdev_control_open,
  userdev_control_close,
  userdev_control_output_block,
  userdev_control_start_input,
  userdev_control_ioctl,
  userdev_control_prepare_for_input,
  userdev_control_prepare_for_output,
  userdev_control_reset,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* trigger */
  userdev_control_set_rate,
  userdev_control_set_format,
  userdev_control_set_channels,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  userdev_server_redirect
};

static audiodrv_t userdev_client_control_driver = {
  userdev_control_open,
  userdev_control_close,
  userdev_control_output_block,
  userdev_control_start_input,
  userdev_control_ioctl,
  userdev_control_prepare_for_input,
  userdev_control_prepare_for_output,
  userdev_control_reset,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, /* trigger */
  userdev_control_set_rate,
  userdev_control_set_format,
  userdev_control_set_channels,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  userdev_client_redirect
};

static void
attach_control_device(void)
{
/*
 * Create the control device files that are used to create client/server
 * device pairs and to redirect access to them.
 */

  if ((client_dev = oss_install_audiodev (OSS_AUDIO_DRIVER_VERSION,
				    userdev_osdev,
				    userdev_osdev,
				    "User space audio device client side",
				    &userdev_client_control_driver,
				    sizeof (audiodrv_t),
				    ADEV_AUTOMODE, AFMT_S16_NE, NULL, -1)) < 0)
    {
      return;
    }

  if ((server_dev = oss_install_audiodev_with_devname (OSS_AUDIO_DRIVER_VERSION,
				    userdev_osdev,
				    userdev_osdev,
				    "User space audio device server side",
				    &userdev_server_control_driver,
				    sizeof (audiodrv_t),
				    ADEV_AUTOMODE, AFMT_S16_NE, NULL, -1, 
				    "server")) < 0)
    {
      return;
    }
}

int
oss_userdev_attach (oss_device_t * osdev)
{
  userdev_osdev = osdev;

  osdev->devc = NULL;
  MUTEX_INIT (osdev, userdev_global_mutex, MH_DRV);

  oss_register_device (osdev, "User space audio driver subsystem");

  attach_control_device();

  return 1;
}

int
oss_userdev_detach (oss_device_t * osdev)
{
  userdev_devc_t *devc;

  if (oss_disable_device (osdev) < 0)
    return 0;

  devc = userdev_free_device_list;

  while (devc != NULL)
  {
	  userdev_devc_t *next = devc->next_instance;

	  userdev_delete_device_pair(devc);

	  devc = next;
  }

  oss_unregister_device (osdev);

  MUTEX_CLEANUP(userdev_global_mutex);

  return 1;
}

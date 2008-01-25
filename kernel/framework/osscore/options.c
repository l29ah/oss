/*
 * Purpose: Configuration options for OSS (osscore.conf)
 *
 * Description:
 * This file contains various configuration options for the osscore module. 
 * They can be set in the osscore.conf configuration file.
 *
 * Each option variable must also be defined as extern in the proper header
 * file (of the subsystem that uses them) or in the source file that uses them.
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2006. All rights reserved.

#include <oss_config.h>

#ifndef NO_GLOBAL_OPTIONS
/*
 * Core settings
 *
 * Remember to update kernel/drv/osscore/.params when adding, removing or
 * changing the following options. The .params file is used when generating the
 * driver.conf files. Also don't forget to update osscore.man.
 */

int max_intrate = 100;		/* 10 msec minimum interrupt interval */
int src_quality = 3;		/* Sample rate conversion quality (0-5) */
int ac97_amplifier = 1;		/* External amplifier enable for AC97 */
int ac97_recselect = 0;		/* Enables independent L/R ch rec source selection */
int cooked_enable = 1;
int dma_buffsize = 0;		/* Size of the DMA buffer in kbytes (0=use default) */
int flat_device_model = 0;	/* 0=new audio device model, 1=old model */
int detect_trace = 0;		/* Se to 1 if detection tracing is required */

oss_option_map_t oss_global_options[] = {
  {"max_intrate", &max_intrate},
  {"detect_trace", &detect_trace},
  {"src_quality", &src_quality},
  {"ac97_amplifier", &ac97_amplifier},
  {"ac97_recselect", &ac97_recselect},
  {"cooked_enable", &cooked_enable},
  {"dma_buffsize", &dma_buffsize},
  {"flat_device_model", &flat_device_model},
  {NULL, NULL}
};
#endif

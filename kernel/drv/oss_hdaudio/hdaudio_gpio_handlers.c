/*
 * Purpose: GPIO initialization handlers for some High Definition Audio systems
 *
 * This file contains codec initialization functions for some HDaudio
 * systems that require initialization of GPIO bits. All functions should
 * return OSS_EAGAIN so that hdaudio_codec.c knows to call the generic
 * codec/mixer initialization routine for the codec. Alternatively the
 * GPIO init function may call the codec/mixer init function for the
 * given system directly (return my_mixer_init_func()).
 *
 * Note that if the system has a dedicated mixer initialization function
 * then also GPIO initialization needs to be performed in the mixer init
 * function (since the same mixer_init function pointers in hdaudio_codecds.h
 * are shared for both purposes).
 *
 * For example:
 *
 * int
 * hdaudio_GPIO_init (int dev, hdaudio_mixer_t * mixer, int cad, int top_group)
 * {
 *	codec_t *codec = mixer->codecs[cad];
 *	int afg = codec->afg;	// Audio function group root widget
 *
 *	// Now use the corb_read() and corb_write() functions to set the
 *	// GPIO related verbs (registers) to the required values.
 *
 * 	return OSS_EAGAIN;	// Fallback
 * }
 *
 * To write the GPIO registers you can use:
 *
 *	  corb_write (mixer, cad, afg, 0, SET_GPIO_DIR, 0xNNNNNNNN);
 *	  corb_write (mixer, cad, afg, 0, SET_GPIO_ENABLE, 0xNNNNNNNN);
 *	  corb_write (mixer, cad, afg, 0, SET_GPIO_DATA, 0xNNNNNNNN);
 *
 * Also (if necessary) you can use the following calls. However they will probably
 * need changes to hdaudio_codec.c so that the unsolicited responses are handled peoperly:
 *
 *	  corb_write (mixer, cad, afg, 0, SET_GPIO_WKEN, 0xNNNNNNNN);
 *	  corb_write (mixer, cad, afg, 0, SET_GPIO_UNSOL, 0xNNNNNNNN);
 *	  corb_write (mixer, cad, afg, 0, SET_GPIO_STICKY, 0xNNNNNNNN);
 *
 * Next the function prototype should be added to hdaudio_codecids.h. Finally
 * edit the subdevices[] array in hdaudio_codecids.h so that the function
 * gets called when given codec (subsystem vendor+device) is detected in the 
 * system. It is not recommended to use the codecs[] table to
 * detect systems that need GPIO handling. The same codec may be used
 * in many different systems and most of them don't require GPIO init.
 * However this is possible if the handler uses the subvendor+subdevice ID to detect the system.
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2004-2008. All rights reserved.

#include "oss_hdaudio_cfg.h"
#include "hdaudio.h"
#include "hdaudio_codec.h"
#include "hdaudio_codecids.h"

int
hdaudio_mac_GPIO_init (int dev, hdaudio_mixer_t * mixer, int cad, int top_group)
{
 	codec_t *codec = mixer->codecs[cad];
 	int afg = codec->afg;	// Audio function group root widget
	unsigned int subdevice = codec->subvendor_id;
	unsigned int codec_id = codec->vendor_id;

	// TODO: Populate this function with the real stuff
	
cmn_err(CE_CONT, "hdaudio_mac_GPIO_init() entered, afg=%d, subdevice=0x%08x, codec=0x%08x\n", afg, subdevice, codec_id);

	return OSS_EAGAIN; /* Continue with the default mixer init */
}
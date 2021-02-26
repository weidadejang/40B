/*
 * xShare.c
 *
 *  Created on: Feb 20, 2021
 *      Author: root
 */

#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include "common.h"

extern pthread_key_t tls_key_threadnr;

void *Share_thread(void *arg)
{
	UNUSED(arg);
	//pthread_setspecific (tls_key_threadnr, "Share");
	pthread_detach(pthread_self());
	pthread_setspecific (tls_key_threadnr, "C2JThread");
	while(1)
	{

	}
}


void start_so_task(void)
{
	  pthread_t pd;
	  pthread_create(&pd, NULL, Share_thread, NULL);
}




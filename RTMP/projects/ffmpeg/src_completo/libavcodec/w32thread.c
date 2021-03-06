/*
 * Copyright (c) 2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
//#define DEBUG

#include "avcodec.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>


//Fernando: 20080908
//#undef printf
//#define LogStr(str)  printf ( "************************** %s: %s - %s-%d **************************\n", __func__, str, __FILE__, __LINE__)
//#define LogStr(str) av_log(NULL, AV_LOG_ERROR, "************************** %s: %s - %s-%d **************************\n", __func__, str, __FILE__, __LINE__);
#define LogStr(str) ;




typedef struct ThreadContext
{
    AVCodecContext *avctx;
    HANDLE thread;
    HANDLE work_sem;
    HANDLE done_sem;
    int (*func)( AVCodecContext *c, void *arg );
    void *arg;
    int ret;
} ThreadContext;

static unsignedWINAPI attribute_align_arg thread_func( void *v )
{
    LogStr ("Init");

    ThreadContext *c = v;

    for (;;)
    {
        //printf("thread_func %X enter wait\n", (int)v); fflush(stdout);
        WaitForSingleObject(c->work_sem, INFINITE);
        //printf("thread_func %X after wait (func=%X)\n", (int)v, (int)c->func); fflush(stdout);
        if (c->func)
        {
            c->ret = c->func(c->avctx, c->arg);
        }
        else
        {
            LogStr ("Exit");
            return 0;
        }

        //printf("thread_func %X signal complete\n", (int)v); fflush(stdout);
        ReleaseSemaphore(c->done_sem, 1, 0);
    }

    LogStr ("Exit");
    return 0;
}

/**
 * Free what has been allocated by avcodec_thread_init().
 * Must be called after decoding has finished, especially do not call while avcodec_thread_execute() is running.
 */
void avcodec_thread_free( AVCodecContext *s )
{
    LogStr ("Init");

    ThreadContext *c = s->thread_opaque;
    int i;

    for (i = 0; i < s->thread_count; i++)
    {

        c[i].func = NULL;
        ReleaseSemaphore(c[i].work_sem, 1, 0);
        WaitForSingleObject(c[i].thread, INFINITE);

        if (c[i].work_sem)
        {
            CloseHandle(c[i].work_sem);
        }

        if (c[i].done_sem)
        {
            CloseHandle(c[i].done_sem);
        }
    }

    av_freep(&s->thread_opaque);

    LogStr ("Exit");
}

int avcodec_thread_execute( AVCodecContext *s, int(*func)( AVCodecContext *c2, void *arg2 ), void **arg, int *ret, int count )
{
    LogStr ("Init");

    ThreadContext *c = s->thread_opaque;
    int i;

    assert(s == c->avctx);
    assert(count <= s->thread_count);

    /* note, we can be certain that this is not called with the same AVCodecContext by different threads at the same time */

    for (i = 0; i < count; i++)
    {
        c[i].arg = arg[i];
        c[i].func = func;
        c[i].ret = 12345;

        ReleaseSemaphore(c[i].work_sem, 1, 0);
    }
    for (i = 0; i < count; i++)
    {
        WaitForSingleObject(c[i].done_sem, INFINITE);

        c[i].func = NULL;
        if (ret)
        {
            ret[i] = c[i].ret;
        }
    }

    LogStr ("Exit");
    return 0;
}

int avcodec_thread_init( AVCodecContext *s, int thread_count )
{
    LogStr ("Init");

    int i;
    ThreadContext *c;
    uint32_t threadid;

    s->thread_count = thread_count;

    assert(!s->thread_opaque);
    c = av_mallocz(sizeof(ThreadContext) * thread_count);
    s->thread_opaque = c;

    for (i = 0; i < thread_count; i++)
    {
        //printf("init semaphors %d\n", i); fflush(stdout);
        c[i].avctx = s;

        if (!(c[i].work_sem = CreateSemaphore(NULL, 0, s->thread_count, NULL)))
        {
            goto fail;
        }

        if (!(c[i].done_sem = CreateSemaphore(NULL, 0, s->thread_count, NULL)))
        {
            goto fail;
        }

        //printf("create thread %d\n", i); fflush(stdout);
        c[i].thread = (HANDLE) _beginthreadex(NULL, 0, thread_func, &c[i], 0, &threadid);
        if (!c[i].thread)
        {
            goto fail;
        }
    }
    //printf("init done\n"); fflush(stdout);

    s->execute = avcodec_thread_execute;

    LogStr ("Exit");
    return 0;
    fail: avcodec_thread_free(s);
    LogStr ("Exit");
    return -1;
}

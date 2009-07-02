/*
 * Copyright (c) 2009 Ryan Huffman
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Authors:
 *	Ryan Huffman (ryanhuffman@gmail.com)
 */

#ifndef TUIO_H 
#define TUIO_H 

#include <X11/extensions/XI.h>
#include <xf86Xinput.h>
#include <lo/lo.h>
#include <hal/libhal.h>

/**
 * Tuio device information, including list of current object
 */
typedef struct _TuioDevice {
    lo_server server;

    int fseq_new, fseq_old;
    int processed;

    struct _Object *obj_list;

    /* List of unused devices that can be allocated for use
     * by ObjectPtr. */
    struct _ObjectDev *unused_device_list;

} TuioDeviceRec, *TuioDevicePtr;

/**
 * An "Object" can represent a tuio blob or cursor (/tuio/2Dcur or
 * /tuio/blob
 */
typedef struct _Object {
    int id;
    float x, y;
    int alive;
    struct _ObjectDev *objdev;

    /* Stores pending information about this object */
    struct {
        Bool alive;
        Bool set;
        float x, y;
    } pending;

    struct _Object *next;

} ObjectRec, *ObjectPtr;

/**
 * Object devices are special devices created at the creation of the first
 * tuio device (aka "core device).  They are tuio devices but are only used 
 * to route object movements through.
 */
typedef struct _ObjectDev {
    InputInfoPtr pInfo;
    struct _ObjectDev *next;
} ObjectDevRec, *ObjectDevPtr;

#endif


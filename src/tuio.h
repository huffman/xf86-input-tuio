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

#ifndef Bool
#define Bool int
#endif
#ifndef True
#define True 1
#endif
#ifndef False
#define False 0
#endif

#define MIN_SUBDEVICES 0 /* min/max subdevices */
#define MAX_SUBDEVICES 20
#define DEFAULT_SUBDEVICES 5

#define DEFAULT_PORT 3333 /* Default UDP port to listen on */

/* Valuators */
#define NUM_VALUATORS 4
#define VAL_X_VELOCITY "X Velocity"
#define VAL_Y_VELOCITY "Y Velocity"
#define VAL_ACCELERATION "Acceleration"

/**
 * Tuio device information, including list of current object
 */
typedef struct _TuioDevice {
    lo_server server;

    int fseq_new, fseq_old;
    int processed;

    int num_subdev;

    struct _Object *obj_list;

    /* List of unused devices that can be allocated for use
     * with ObjectPtr. */
    struct _SubDevice *subdev_list;

    /* Remaining variables are set by "Option" values */
    int tuio_port;
    int init_num_subdev;
    Bool post_button_events;
    int fseq_threshold; /* Maximum difference between consecutive fseq values
                           that will allow a packet to be dropped */
    Bool dynadd_subdev;

} TuioDeviceRec, *TuioDevicePtr;

/**
 * An "Object" can represent a tuio blob or cursor (/tuio/2Dcur or
 * /tuio/2Dblb
 */
typedef struct _Object {
    struct _Object *next;

    int id;
    float xpos, ypos;
    float xvel, yvel;
    int alive;
    struct _SubDevice *subdev;

    /* Stores pending information about this object */
    struct {
        Bool alive;
        Bool set;
        Bool button;
        float xpos, ypos;
        float xvel, yvel;
    } pending;
} ObjectRec, *ObjectPtr;

/**
 * Subdevices are special devices created at the creation of the first
 * tuio device (aka "core" device).  They are tuio devices but are only used 
 * to route object movements through.
 */
typedef struct _SubDevice {
    struct _SubDevice *next;

    InputInfoPtr pInfo;
} SubDeviceRec, *SubDevicePtr;

#endif


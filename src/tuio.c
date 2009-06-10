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

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <xorg/xf86.h>
#include <xorg-server.h>
#include <xf86Xinput.h>
#include <exevents.h>
#include <xorgVersion.h>
#include <X11/extensions/XIproto.h>
#include <X11/extensions/XInput2.h>
#include <xf86_OSlib.h>
#include <lo/lo.h>

#include "tuio.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Module Functions */
static pointer
TuioPlug(pointer, pointer, int *, int *);

static void
TuioUnplug(pointer);

/* Driver Functions */
static InputInfoPtr
TuioPreInit(InputDriverPtr, IDevPtr, int);

static void
TuioUnInit(InputDriverPtr, InputInfoPtr, int);

static void
TuioReadInput(InputInfoPtr);

static int
TuioControl(DeviceIntPtr, int);

/* Internal Functions */
static int
_tuio_lo_cur2d_handle(const char *path,
                   const char *types,
                   lo_arg **argv,
                   int argc,
                   void *data,
                   void *user_data);

static void
lo_error(int num,
         const char *msg,
         const char *path);


static XF86ModuleVersionInfo TuioVersionRec =
{
    "tuio",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
    ABI_CLASS_XINPUT,
    ABI_XINPUT_VERSION,
    MOD_CLASS_XINPUT,
    {0, 0, 0, 0}
};

_X_EXPORT InputDriverRec TUIO = 
{
    1,
    "tuio",
    NULL,
    TuioPreInit,
    TuioUnInit,
    NULL,
    0
};

_X_EXPORT XF86ModuleData tuioModuleData =
{
    &TuioVersionRec,
    TuioPlug,
    TuioUnplug
};

static pointer
TuioPlug(pointer	module,
         pointer	options,
         int		*errmaj,
         int		*errmin)
{
    xf86AddInputDriver(&TUIO, module, 0);
    return module;
}

static void
TuioUnplug(pointer	p)
{
}

/**
 * Pre-initialization of new device
 *
 * TODO:
 *  - Parse configuration options
 *  - Setup internal data
 */
static InputInfoPtr
TuioPreInit(InputDriverPtr drv,
            IDevPtr dev,
            int flags)
{
    InputInfoPtr  pInfo;
    TuioDevicePtr pTuio;
    const char *device;

    if (!(pInfo = xf86AllocateInput(drv, 0)))
        return NULL;

    pTuio = xcalloc(1, sizeof(TuioDeviceRec));
    if (!pTuio) {
            pInfo->private = NULL;
            xf86DeleteInput(pInfo, 0);
            return NULL;
    }

    pInfo->private = pTuio;

    pInfo->name = xstrdup(dev->identifier);
    pInfo->flags = 0;
    pInfo->type_name = XI_TOUCHSCREEN; /* FIXME: Correct type? */
    pInfo->conf_idev = dev;
    pInfo->read_input = TuioReadInput; /* Set callback */
    pInfo->switch_mode = NULL;
    pInfo->device_control = TuioControl; /* Set callback */
    
    /* Process common device options */
    xf86CollectInputOptions(pInfo, NULL, NULL);
    xf86ProcessCommonOptions(pInfo, pInfo->options);

    pInfo->flags |= XI86_OPEN_ON_INIT;
    pInfo->flags |= XI86_CONFIGURED;

    return pInfo;
}

static void
TuioUnInit(InputDriverPtr drv,
           InputInfoPtr pInfo,
           int flags)
{
    TuioDevicePtr pTuio = pInfo->private;

    xfree(pTuio);
    xf86DeleteInput(pInfo, 0);
}

/**
 * Handle new data on the socket
 */
static void
TuioReadInput(InputInfoPtr pInfo)
{
    char data;

    while(xf86WaitForInput(pInfo->fd, 0) > 0)
    {
        /* Read Input */
        
        /* xf8PostMotionEvent() */
    }
}

/**
 * Handle device state changes
 */
static int
TuioControl(DeviceIntPtr device,
            int what)
{
    InputInfoPtr pInfo = device->public.devicePrivate;
    TuioDevicePtr pTuio = pInfo->private;

    switch (what)
    {
        case DEVICE_INIT:
            xf86Msg(X_INFO, "%s: Init.\n", pInfo->name);
            break;

        case DEVICE_ON: /* Open device socket and start listening! */
            xf86Msg(X_INFO, "%s: On.\n", pInfo->name);
            if (device->public.on) /* already on! */
                break;

            pTuio->server = lo_server_new("3333", lo_error); 
            if (pTuio->server == NULL) {
                xf86Msg(X_ERROR, "%s: Error creating new lo_server.\n", 
                        pInfo->name);
                return BadAlloc;
            }

            /* Register to receive all /tuio/2Dcur messages */
            lo_server_add_method(pTuio->server, "/tuio/2Dcur", NULL, 
                                 _tuio_lo_cur2d_handle, NULL);

            pInfo->fd = lo_server_get_socket_fd(pTuio->server);

            xf86FlushInput(pInfo->fd);
            xf86AddEnabledDevice(pInfo);
            device->public.on = TRUE;
            break;

        case DEVICE_OFF:
            xf86Msg(X_INFO, "%s: Off.\n", pInfo->name);
            if (!device->public.on)
                break;

            lo_server_free(pTuio->server);

            xf86RemoveEnabledDevice(pInfo);
            pInfo->fd = -1;
            device->public.on = FALSE;
            break;

        case DEVICE_CLOSE:
            xf86Msg(X_INFO, "%s: Close.\n", pInfo->name);

            lo_server_free(pTuio->server);

            xf86RemoveDevice(pInfo);
            pInfo->fd = -1;
            device->public.on = FALSE;
            break;

    }
    return Success;
}

static int
_tuio_start_lo_server()
{
}

static void
_tuio_setup_lo_server(lo_server s)
{
    /* /tuio/2Dcur set s x y X Y m */
    /* /tuio/2Dcur fseq [int32] */
    lo_server_add_method(s, "/tuio/2Dcur", NULL, _tuio_lo_cur2d_handle, NULL);
}

/**
 * Handles OSC messages in the /tuio/2Dcur address space
 */
static int
_tuio_lo_cur2d_handle(const char *path,
                   const char *types,
                   lo_arg **argv,
                   int argc,
                   void *data,
                   void *user_data)
{
    if (argc < 1) {
        //xf86Msg(X_ERROR, "%s: On.\n", pInfo->name);
    }

    if (strcmp(argv[0], "set")) {

    } else if (strcmp(argv[0], "alive")) {

    } else if (strcmp(argv[0], "fseq")) {

    }
}

/**
 * liblo error handler
 */
static void
lo_error(int num,
         const char *msg,
         const char *path)
{
}


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

/* Driver Function */
static InputInfoPtr
TuioPreInit(InputDriverPtr, IDevPtr, int);

static void
TuioUnInit(InputDriverPtr, InputInfoPtr, int);

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

static void
TuioReadInput(InputInfoPtr pInfo)
{
}

static int
TuioControl(DeviceIntPtr device, int what)
{
    return 1;
}

static InputInfoPtr
TuioPreInit(InputDriverPtr drv, IDevPtr dev, int flags)
{
    InputInfoPtr pInfo;
    const char *device;

    if (!(pInfo = xf86AllocateInput(drv, 0)))
        return NULL;

    return pInfo;
}

static void
TuioUnInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags)
{
}


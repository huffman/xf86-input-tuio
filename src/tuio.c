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

#include <unistd.h>

#include <xf86Xinput.h>
#include <X11/extensions/XIproto.h>
#include <X11/extensions/XInput2.h>
#include <xf86_OSlib.h>

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
_tuio_init_buttons(DeviceIntPtr device);

static int
_tuio_init_axes(DeviceIntPtr device);

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

/* Object list manipulation functions */
static ObjectPtr
_object_get(ObjectPtr head, int id);

static ObjectPtr 
_object_insert(ObjectPtr head);

static void
_object_remove(ObjectPtr head, int id);

/* Driver information */
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

/* Remove this? */
static void
TuioUnplug(pointer	p)
{
}

/**
 * Pre-initialization of new device
 *
 * TODO:
 *  - Parse configuration options
 */
static InputInfoPtr
TuioPreInit(InputDriverPtr drv,
            IDevPtr dev,
            int flags)
{
    InputInfoPtr  pInfo;
    TuioDevicePtr pTuio;

    if (!(pInfo = xf86AllocateInput(drv, 0)))
        return NULL;

    /* The TuioDevicePtr will hold object and other
     * information */
    pTuio = xcalloc(1, sizeof(TuioDeviceRec));
    if (!pTuio) {
            pInfo->private = NULL;
            xf86DeleteInput(pInfo, 0);
            return NULL;
    }

    pTuio->list_head = xcalloc(1, sizeof(ObjectRec));
    if (!pTuio->list_head) {
            xfree(pTuio);
            pInfo->private = NULL;
            xf86DeleteInput(pInfo, 0);
            return NULL;
    }
    pTuio->list_head->id = -1;
    pTuio->list_head->next = NULL;

    pInfo->private = pTuio;

    /* Set up InputInfoPtr */
    pInfo->name = xstrdup(dev->identifier);
    pInfo->flags = 0;
    pInfo->type_name = XI_TOUCHSCREEN; /* FIXME: Correct type? */
    pInfo->conf_idev = dev;
    pInfo->read_input = TuioReadInput; /* Set callback */
    pInfo->device_control = TuioControl; /* Set callback */
    pInfo->switch_mode = NULL;
    
    /* Process common device options */
    xf86CollectInputOptions(pInfo, NULL, NULL);
    xf86ProcessCommonOptions(pInfo, pInfo->options);

    pInfo->flags |= XI86_OPEN_ON_INIT;
    pInfo->flags |= XI86_CONFIGURED;

    return pInfo;
}

/**
 * Clean up
 */
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
    TuioDevicePtr pTuio = pInfo->private;
    ObjectPtr head = pTuio->list_head;
    ObjectPtr obj = head->next;
    ObjectPtr objtemp;

    while(xf86WaitForInput(pInfo->fd, 0) > 0)
    {
        /* liblo will receive a message and call the appropriate
         * handlers (i.e. _tuio_lo_cur2d_hande())
         * If nothing is found (this SHOULDN'T happen, but if it did,
         * all the objects would be deleted), just return */
        if(!lo_server_recv_noblock(pTuio->server, 0))
            return;

        /* During the processing of the previous message/bundle,
         * any "active" messages will be handled by flagging
         * the listed object ids.  Now that processing is done,
         * remove any dead object ids. */
        while (obj != NULL) {
            if (!obj->alive) {
                objtemp = obj->next;
                _object_remove(head, obj->id);
                obj = objtemp;
            } else {
                obj->alive = 0; /* Reset for next message */
                obj = obj->next;
            }
        }
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
            xf86Msg(X_INFO, "%s: Init\n", pInfo->name);
            _tuio_init_buttons(device);
            _tuio_init_axes(device);
            break;

        case DEVICE_ON: /* Open device socket and start listening! */
            xf86Msg(X_INFO, "%s: On.\n", pInfo->name);
            if (device->public.on) /* already on! */
                break;

            /* Setup server */
            pTuio->server = lo_server_new_with_proto("3333", LO_UDP, lo_error);
            if (pTuio->server == NULL) {
                xf86Msg(X_ERROR, "%s: Error allocating new lo_server\n", 
                        pInfo->name);
                return BadAlloc;
            }

            /* Register to receive all /tuio/2Dcur messages */
            lo_server_add_method(pTuio->server, "/tuio/2Dcur", NULL, 
                                 _tuio_lo_cur2d_handle, pInfo);

            pInfo->fd = lo_server_get_socket_fd(pTuio->server);
            xf86Msg(X_INFO, "%s: Socket = %i\n", pInfo->name, pInfo->fd);

            xf86FlushInput(pInfo->fd);
            xf86AddEnabledDevice(pInfo);
            device->public.on = TRUE;
            break;

        case DEVICE_OFF:
            xf86Msg(X_INFO, "%s: Off\n", pInfo->name);
            if (!device->public.on)
                break;

            xf86RemoveEnabledDevice(pInfo);

            lo_server_free(pTuio->server);
            pInfo->fd = -1;
            device->public.on = FALSE;
            break;

        case DEVICE_CLOSE:
            xf86Msg(X_INFO, "%s: Close\n", pInfo->name);

            xfree(pTuio);
            break;

    }
    return Success;
}

static int
_tuio_create_master() {
    //XICreateMasterInfo ci;
    //unsigned int blobid;
    //char cur_name[21]; /* Max len: 20 char + \0 */

    //sprintf(cur_name, "tuio_blob_%u", blobid);

    //c.type = XICreateMaster;
    //c.name =  cur_name;
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
    InputInfoPtr pInfo = user_data;
    TuioDevicePtr pTuio = pInfo->private;
    ObjectPtr head = pTuio->list_head;
    ObjectPtr obj, objtemp;
    int valuators[2];
    int i;

    if (argc < 1) {
        xf86Msg(X_ERROR, "%s: \n", pInfo->name);
        return 1;
    }

    /* Parse message type */

    /* Set message type:  */
    if (strcmp(argv[0], "set") == 0) {
        obj = _object_get(head, argv[1]->i);
        if(obj == NULL) {
            obj = _object_insert(head);
            obj->id = argv[1]->i;
        }

        obj->x = argv[2]->f;
        obj->y = argv[3]->f;
        
        /* OKAY FOR NOW, MAYBE UPDATE */
        valuators[0] = obj->x * 0x7FFFFFFF;
        valuators[1] = obj->y * 0x7FFFFFFF;

        xf86PostMotionEventP(pInfo->dev,
                            TRUE, /* is_absolute */
                            0, /* first_valuator */
                            2, /* num_valuators */
                            valuators);
         
    } else if (strcmp(argv[0], "alive") == 0) {
        obj = head->next;
        while (obj != NULL) {
            for (i=1; i<argc; i++) {
                if (argv[i]->i == obj->id) {
                    obj->alive = 1;
                    break;
                }
            }
            obj = obj->next;
        }

    } else if (strcmp(argv[0], "fseq") == 0) {
        pTuio->fseq_old = pTuio->fseq_new;
        pTuio->fseq_new = argv[1]->i;

    }
    return 0;
}

/**
 * liblo error handler
 */
static void
lo_error(int num,
         const char *msg,
         const char *path)
{
    xf86Msg(X_ERROR, "liblo: %s\n", msg);
}

/**
 * Retrieves an object from a list based on its id.  Returns NULL if
 * not found.
 */
static ObjectPtr
_object_get(ObjectPtr head, int id) {
    ObjectPtr obj = head->next;

    while (obj != NULL && obj->id != id) {
        obj = obj->next;
    }

    return obj;
}

/**
 * Allocates and inserts a new object at the beginning of a list.
 * Pointer to the new object is returned.
 * Doesn't check for duplicate ids, so call _object_get() beforehand
 * to make sure it doesn't exist already!!
 *
 * @return ptr to new object
 */
static ObjectPtr 
_object_insert(ObjectPtr head) {
    ObjectPtr obj = xcalloc(1, sizeof(ObjectRec));
    obj->next = head->next;
    head->next = obj;
    return obj;
}

/**
 * Removes an object from a list.  It is assumed that the object 
 * is in the list
 */
static void
_object_remove(ObjectPtr head, int id) {
    ObjectPtr obj = head;
    ObjectPtr objtmp;

    while (obj->next != NULL) {
        if (obj->next->id == id) {
            objtmp = obj->next;
            obj->next = objtmp->next;
            xfree(objtmp);
            break;
        }
        obj = obj->next;
    }
}

/**
 * Init the button map device.  We only use one button.
 */
static int
_tuio_init_buttons(DeviceIntPtr device)
{
    InputInfoPtr        pInfo = device->public.devicePrivate;
    CARD8               *map;
    Atom                *labels;
    const int           num_buttons = 1; /* left-click */
    int                 ret = Success;
    int                 i;

    map = xcalloc(num_buttons, sizeof(CARD8));
    labels = xcalloc(num_buttons, sizeof(Atom));

    for (i = 0; i < num_buttons; i++)
        map[i] = i;

    if (!InitButtonClassDeviceStruct(device, num_buttons,
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 7
                                     labels,
#endif
                                     map)) {
        xf86Msg(X_ERROR, "%s: Failed to register buttons.\n", pInfo->name);
        ret = BadAlloc;
    }

    xfree(labels);
    xfree(map);
    return ret;
}

/**
 * Init valuators for device, use x/y coordinates.
 */
static int
_tuio_init_axes(DeviceIntPtr device)
{
    InputInfoPtr        pInfo = device->public.devicePrivate;
    int                 i;
    const int           num_axes = 2;
    Atom *atoms;

    atoms = xcalloc(2, sizeof(Atom));

    if (!InitValuatorClassDeviceStruct(device,
                                       num_axes,
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 7
                                       atoms,
#endif
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 3
                                       GetMotionHistory,
#endif
                                       GetMotionHistorySize(),
                                       0))
        return BadAlloc;

    /* Use absolute mode.  Currently, TUIO coords are mapped to the
     * full screen area */
    pInfo->dev->valuator->mode = Absolute;
    if (!InitAbsoluteClassDeviceStruct(device))
        return BadAlloc;

    for (i = 0; i < num_axes; i++)
    {
        xf86InitValuatorAxisStruct(device, i,
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 7
                                   atoms[i],
#endif
                                   0, 0x7FFFFFFF, 1, 1, 1);
        xf86InitValuatorDefaults(device, i);
    }
    return Success;
}

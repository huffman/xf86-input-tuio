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

/* InputInfoPtr for main tuio device */
static TuioDevicePtr g_pTuio;

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
TuioObjReadInput(InputInfoPtr pInfo);

static void
TuioReadInput(InputInfoPtr);

static int
TuioControl(DeviceIntPtr, int);

/* Internal Functions */
static int
_init_devices(InputInfoPtr pInfo, int num);

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
_free_tuiodev(TuioDevicePtr pTuio);

static void
lo_error(int num,
         const char *msg,
         const char *path);

/* Object list manipulation functions */
static ObjectPtr
_object_get(ObjectPtr head, int id);

static ObjectPtr 
_object_new(TuioDevicePtr pTuio, int id);

static void
_object_remove(TuioDevicePtr pTuio, ObjectPtr *head, int id);

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

/**
 * Unplug
 */
static void
TuioUnplug(pointer	p)
{
}

/**
 * Pre-initialization of new device
 * This can be entered by either a "core" tuio device
 * or an extension "Object" device that is used for routing individual object
 * events through.
 */
static InputInfoPtr
TuioPreInit(InputDriverPtr drv,
            IDevPtr dev,
            int flags)
{
    InputInfoPtr  pInfo;
    TuioDevicePtr pTuio = NULL;
    ObjectPtr obj;
    ObjectDevPtr objdev;
    char *type;
    int id;

    if (!(pInfo = xf86AllocateInput(drv, 0)))
        return NULL;

    /* If Type == Object, this is a device for an object to use */
    type = xf86CheckStrOption(dev->commonOptions, "Type", NULL); 

    if (type != NULL && strcmp(type, "Object") == 0) {

        xf86Msg(X_INFO, "%s: Object device found\n", dev->identifier);

        /* Allocate device storage and add to device list */
        objdev = xcalloc(1, sizeof(ObjectDevRec));
        if (!g_pTuio->unused_device_list) {
            g_pTuio->unused_device_list = objdev;
        } else {
            objdev->next = g_pTuio->unused_device_list;
            g_pTuio->unused_device_list = objdev;
        }
        objdev->pInfo = pInfo;

    } else {

        if (!(pTuio = xcalloc(1, sizeof(TuioDeviceRec)))) {
            xf86DeleteInput(pInfo, 0);
            return NULL;
        }
        g_pTuio = pTuio;

        pInfo->private = pTuio;
    }

    /* Set up InputInfoPtr */
    pInfo->name = xstrdup(dev->identifier);
    pInfo->flags = 0;
    pInfo->type_name = XI_TOUCHSCREEN; /* FIXME: Correct type? */
    pInfo->conf_idev = dev;
    pInfo->read_input = pTuio ? TuioReadInput : TuioObjReadInput; /* Set callback */
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
    xf86DeleteInput(pInfo, 0);
}

/**
 * Empty callback for object device.
 */
static void
TuioObjReadInput(InputInfoPtr pInfo) {
    return;
}

/**
 * Handle new TUIO  data on the socket
 */
static void
TuioReadInput(InputInfoPtr pInfo)
{
    TuioDevicePtr pTuio = pInfo->private;
    ObjectPtr *head = &pTuio->obj_list;
    ObjectPtr obj = pTuio->obj_list;
    ObjectPtr objtmp;
    int valuators[2];

    while(xf86WaitForInput(pInfo->fd, 0) > 0)
    {
        /* The liblo handler will set this flag if anything was processed */
        pTuio->processed = 0;

        /* liblo will receive a message and call the appropriate
         * handlers (i.e. _tuio_lo_cur2d_hande()) */
        lo_server_recv_noblock(pTuio->server, 0);

        /* During the processing of the previous message/bundle,
         * any "active" messages will be handled by flagging
         * the listed object ids.  Now that processing is done,
         * remove any dead object ids and set any pending changes.
         * Also check to make sure the processed data was newer than
         * the last processed data */
        if (pTuio->processed && pTuio->fseq_new > pTuio->fseq_old) {

            while (obj != NULL) {
                if (!obj->alive) {
                    xf86PostButtonEvent(obj->objdev->pInfo->dev, TRUE, 1, FALSE, 0, 0);
                    objtmp = obj->next;
                    _object_remove(pTuio, head, obj->id);
                    obj = objtmp;
                } else {
                    if (obj->pending.set && obj->objdev) {
                        obj->x = obj->pending.x;
                        obj->y = obj->pending.y;
                        obj->pending.set = False;

                        if (obj->objdev) {
                            /* OKAY FOR NOW, MAYBE UPDATE */
                            valuators[0] = obj->x * 0x7FFFFFFF;
                            valuators[1] = obj->y * 0x7FFFFFFF;

                            xf86PostMotionEventP(obj->objdev->pInfo->dev,
                                    TRUE, /* is_absolute */
                                    0, /* first_valuator */
                                    2, /* num_valuators */
                                    valuators);
                        }
                    }
                    obj->alive = 0; /* Reset for next message */
                    obj = obj->next;
                }
            }
            pTuio->fseq_old = pTuio->fseq_new;
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

            /* If this is a "core" device, create object devices */
            if (pTuio)
                _init_devices(pInfo, 5);
            break;

        case DEVICE_ON: /* Open socket and start listening! */
            xf86Msg(X_INFO, "%s: On.\n", pInfo->name);
            if (device->public.on)
                break;

            if (!pTuio) {
                pInfo->fd = 0;
                goto finish;
            }

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

            xf86FlushInput(pInfo->fd);

finish:     xf86AddEnabledDevice(pInfo);
            device->public.on = TRUE;
            break;

        case DEVICE_OFF:
            xf86Msg(X_INFO, "%s: Off\n", pInfo->name);
            if (!device->public.on)
                break;

            xf86RemoveEnabledDevice(pInfo);

            if (pTuio) {
                lo_server_free(pTuio->server);
                pInfo->fd = -1;
            }

            device->public.on = FALSE;
            break;

        case DEVICE_CLOSE:
            xf86Msg(X_INFO, "%s: Close\n", pInfo->name);
            break;

    }
    return Success;
}

/**
 * Initialize the device properties
 * TODO
 */
TuioPropertyInit() {
}

/**
 * Free a TuioDeviceRec
 */
static void
_free_tuiodev(TuioDevicePtr pTuio) {
    ObjectPtr obj = pTuio->obj_list;
    ObjectPtr tmp;

    while (obj != NULL) {
        tmp = obj->next;
        xfree(obj);
        obj = tmp;
    }

    xfree(pTuio);
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
                      void *user_data) {
    InputInfoPtr pInfo = user_data;
    TuioDevicePtr pTuio = pInfo->private;
    ObjectPtr head = pTuio->obj_list;
    ObjectPtr obj, objtemp;
    char *act;
    int i;

    if (argc == 0) {
        xf86Msg(X_ERROR, "%s: Error in /tuio/cur2d (argc == 0)\n", 
                pInfo->name);
        return 0;
    }

    /* Flag as being processed, used in TuioReadInput() */
    pTuio->processed = 1;

    /* Parse message type */
    /* Set message type:  */
    if (strcmp((char *)argv[0], "set") == 0) {

        /* Simple type check */
        if (strcmp(types, "sifffff")) {
            xf86Msg(X_ERROR, "%s: Error in /tuio/cur2d set msg (types == %s)\n", 
                    pInfo->name, types);
            return 0;
        }

        obj = _object_get(head, argv[1]->i);

        /* If not found, create a new object */
        if (obj == NULL) {
            obj = _object_new(pTuio, argv[1]->i);
            if (obj->objdev)
                xf86PostButtonEvent(obj->objdev->pInfo->dev, TRUE, 1, TRUE, 0, 0);
        }

        obj->pending.x = argv[2]->f;
        obj->pending.y = argv[3]->f;
        obj->pending.set = True;

    } else if (strcmp((char *)argv[0], "alive") == 0) {

        /* Mark all objects that are still alive */
        obj = head;
        while (obj != NULL) {
            for (i=1; i<argc; i++) {
                if (argv[i]->i == obj->id) {
                    obj->alive = True;
                    break;
                }
            }
            obj = obj->next;
        }

    } else if (strcmp((char *)argv[0], "fseq") == 0) {
        /* Simple type check */
        if (strcmp(types, "si")) {
            xf86Msg(X_ERROR, "%s: Error in /tuio/cur2d fseq msg (types == %s)\n", 
                    pInfo->name, types);
            return 0;
        }
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
 * New device creation through hal
 * I referenced the wacom hal-setup patch while writing this:
 * http://cvs.fedoraproject.org/viewvc/rpms/linuxwacom/devel/linuxwacom-0.8.2.2-hal-setup.patch?revision=1.1&view=markup
 *
 * @return 0 if successful, 1 if failure
 */
static int
_init_devices(InputInfoPtr pInfo, int num) {
    DBusError error;
    DBusConnection *conn;
    LibHalContext *ctx;
    char *newdev;
    char *name;
    int i;

    /* We need a new device to send motion/button events through.
     * There isn't a great way to do this right now without native
     * blob events, so just hack it out for now.  Woot. */

    /* Open connection to dbus and create contex */
    dbus_error_init(&error);
    if ((conn = dbus_bus_get(DBUS_BUS_SYSTEM, &error)) == NULL) {
        xf86Msg(X_ERROR, "%s: Failed to open dbus connection: %s\n",
                pInfo->name, error.message);
		return 1;
	}
	if ((ctx = libhal_ctx_new()) == NULL) {
        xf86Msg(X_ERROR, "%s: Failed to obtain hal context: %s\n",
                pInfo->name, error.message);
		return 1;
	}

    dbus_error_init(&error);
    libhal_ctx_set_dbus_connection(ctx, conn);
    if (!libhal_ctx_init(ctx, &error)) {
        xf86Msg(X_ERROR, "%s: Failed to initialize hal context: %s\n",
                pInfo->name, error.message);
		return 1;
    }

    /* Create new devices through hal */
    for (i=0; i<num; i++) {

        dbus_error_init(&error);
        newdev = libhal_new_device(ctx, &error);
        if (dbus_error_is_set(&error) == TRUE) {
            xf86Msg(X_ERROR, "%s: Failed to create input device: %s\n",
                    pInfo->name, error.message);
            return 1;
        }

        dbus_error_init(&error);
        libhal_device_set_property_string(ctx, newdev, "input.device",
                "blob", &error);
        if (dbus_error_is_set(&error) == TRUE) {
            xf86Msg(X_ERROR, "%s: Failed to set hal property: %s\n",
                    pInfo->name, error.message);
            return 1;
        }

        dbus_error_init(&error);
        libhal_device_set_property_string(ctx, newdev,
                "input.x11_driver", "tuio",
                &error);
        if (dbus_error_is_set(&error) == TRUE) {
            xf86Msg(X_ERROR, "%s: Failed to set hal property: %s\n",
                    pInfo->name, error.message);
            return 1;
        }

        /* Set "Type" property.  This will be used in TuioPreInit to determine
         * whether the new device is an object device or not */
        dbus_error_init(&error);
        libhal_device_set_property_string(ctx, newdev,
                "input.x11_options.Type",
                "Object", &error);
        if (dbus_error_is_set(&error) == TRUE) {
            xf86Msg(X_ERROR, "%s: Failed to set hal property: %s\n",
                    pInfo->name, error.message);
            return 1;
        }

        asprintf(&name, "Tuio Obj (%s) %i", pInfo->name, i);

        /* Set name */
        dbus_error_init(&error);
        libhal_device_set_property_string(ctx, newdev,
                           "info.product", name,
                           &error);
        if (dbus_error_is_set(&error) == TRUE) {
            xf86Msg(X_ERROR, "%s: Failed to set hal property: %s\n",
                 pInfo->name, error.message);
            return 1;
        }

        /* Finalize creation of new device */
        dbus_error_init(&error);
        libhal_device_commit_to_gdl(ctx, newdev, "/org/freedesktop/Hal/devices/tuio_subdev", &error);
        if (dbus_error_is_set (&error) == TRUE) {
            xf86Msg(X_ERROR, "%s: Failed to add input device: %s\n",
                 pInfo->name, error.message);
            return 1;
        }

        xfree(name);
    }

    //libhal_context_free(ctx);

    return 0;
}

/**
 * Retrieves an object from a list based on its id.
 *
 * @returns NULL if not found.
 */
static ObjectPtr
_object_get(ObjectPtr head, int id) {
    ObjectPtr obj = head;

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
 * @return ptr to newly inserted object
 */
static ObjectPtr 
_object_new(TuioDevicePtr pTuio, int id) {
    ObjectPtr obj = pTuio->obj_list;
    ObjectDevPtr objdev = pTuio->unused_device_list;

    ObjectPtr new_obj = xcalloc(1, sizeof(ObjectRec));

    if (obj)
        new_obj->next = obj;
    pTuio->obj_list = new_obj;

    if (objdev) 
        pTuio->unused_device_list = objdev->next;
    new_obj->objdev = objdev;
    objdev = NULL;

    new_obj->id = id;
    new_obj->alive = True;

    return new_obj;
}

/**
 * Removes an object from a list.
 */
static void
_object_remove(TuioDevicePtr pTuio, ObjectPtr *head, int id) {
    ObjectPtr obj = *head;
    ObjectPtr objtmp;
    ObjectDevPtr objdev;

    if (obj != NULL && obj->id == id) {
        *head = (*head)->next;
        if (obj->objdev) {
            objdev = pTuio->unused_device_list;
            if (objdev)
                obj->objdev->next = objdev;
            pTuio->unused_device_list = obj->objdev;
        }
    } else if (obj != NULL) {
        while (obj->next != NULL) {
            if (obj->next->id == id) {
                objtmp = obj->next;
                obj->next = objtmp->next;
                
                if (objtmp->objdev) {
                    objdev = pTuio->unused_device_list;
                    if (objdev)
                        objtmp->objdev->next = objdev;
                    pTuio->unused_device_list = objtmp->objdev;
                }
                    
                xfree(objtmp);
                break;
            }
            obj = obj->next;
        }
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
    int numbuttons = 4;
    int                 ret = Success;
    int                 i;

    map = xcalloc(numbuttons, sizeof(CARD8));
    labels = xcalloc(1, sizeof(Atom));
    for (i=0; i<numbuttons; i++)
        map[i] = i;

    if (!InitButtonClassDeviceStruct(device, numbuttons /* 1 button */,
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 7
                                     labels,
#endif
                                     map)) {
        xf86Msg(X_ERROR, "%s: Failed to register buttons.\n", pInfo->name);
        ret = BadAlloc;
    }

    xfree(labels);
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

    atoms = xcalloc(num_axes, sizeof(Atom));

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

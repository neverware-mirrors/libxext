/*
 * Copyright � 2007 Peter Hutterer
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the author not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  The author makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* 
 * XGE is an extension to re-use a single opcode for multiple events,
 * depending on the extension. XGE allows events >32 bytes.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define NEED_EVENTS
#define NEED_REPLIES

#include <stdio.h>
#include <X11/extensions/geproto.h>
#include <X11/extensions/ge.h>
#include <X11/Xlibint.h>
#include <X11/extensions/XInput.h>
#include <X11/extensions/extutil.h>
#include <X11/extensions/Xge.h>

/***********************************************************************/
/*                    internal data structures                         */
/***********************************************************************/

/* NULL terminated list of registered extensions. */
typedef struct _XGEExtNode {
    int extension;
    XExtensionHooks* hooks;
    struct _XGEExtNode* next;
} XGEExtNode, *XGEExtList;

/* Internal data for GE extension */
typedef struct _XGEData {
    XEvent data;
    XExtensionVersion *vers;
    XGEExtList extensions;
} XGEData;

/* forward declarations */
extern XExtDisplayInfo* _xgeFindDisplay(Display*);
static Bool _xgeWireToEvent(Display*, XEvent*, xEvent*);
Status _xgeEventToWire(Display*, XEvent*, xEvent*);
static int _xgeDpyClose(Display*, XExtCodes*);
static XExtensionVersion* _xgeGetExtensionVersion(Display*, 
                                                  _Xconst char*, 
                                                  XExtDisplayInfo*);
static Bool _xgeCheckExtension(Display* dpy, XExtDisplayInfo* info);

/* main extension information data */
static XExtensionInfo *xge_info;
static char xge_extension_name[] = GE_NAME;
static XExtensionHooks xge_extension_hooks = {
    NULL,	        /* create_gc */
    NULL,	        /* copy_gc */
    NULL,	        /* flush_gc */
    NULL,	        /* free_gc */
    NULL,	        /* create_font */
    NULL,	        /* free_font */
    _xgeDpyClose,	/* close_display */
    _xgeWireToEvent,	/* wire_to_event */
    _xgeEventToWire,	/* event_to_wire */
    NULL,	        /* error */
    NULL,	        /* error_string */
};


XExtDisplayInfo *_xgeFindDisplay(Display *dpy) 
{
    XExtDisplayInfo *dpyinfo; 
    if (!xge_info) 
    { 
        if (!(xge_info = XextCreateExtension())) 
            return NULL; 
    }
    if (!(dpyinfo = XextFindDisplay (xge_info, dpy)))
    {
        dpyinfo = XextAddDisplay (xge_info, 
                                  dpy, 
                                  xge_extension_name,
                                  &xge_extension_hooks, 
                                  0 /* no events, see below */, 
                                  NULL);
        /* We don't use an extension opcode, so we have to set the handlers
         * directly. If GenericEvent would be > 64, the job would be done by
         * XExtAddDisplay  */
        XESetWireToEvent (dpy, 
                          GenericEvent, 
                          xge_extension_hooks.wire_to_event);
        XESetEventToWire (dpy, 
                          GenericEvent, 
                          xge_extension_hooks.event_to_wire);
    }
    return dpyinfo;
}

/*
 * Check extension is set up and internal data fields are filled.
 */
Bool 
_xgeCheckExtInit(Display* dpy, XExtDisplayInfo* info)
{
    if(!_xgeCheckExtension(dpy, info))
    {
        goto cleanup;
    }

    if (!info->data) 
    {
        XGEData* data = (XGEData*)Xmalloc(sizeof(XGEData));
        if (!data) {
            goto cleanup;
        }
        /* get version from server */
        data->vers = 
            _xgeGetExtensionVersion(dpy, "Generic Event Extension", info);
        data->extensions = NULL;
        info->data = (XPointer)data;
    }

    return True;

cleanup:
    UnlockDisplay(dpy);
    return False;
}

/* Return 1 if XGE extension exists, 0 otherwise. */
static Bool 
_xgeCheckExtension(Display* dpy, XExtDisplayInfo* info)
{
    XextCheckExtension(dpy, info, xge_extension_name, False);
    return True;
}


/* Retrieve XGE version number from server. */
static XExtensionVersion*
_xgeGetExtensionVersion(Display* dpy, 
                            _Xconst char* name, 
                            XExtDisplayInfo*info)
{
    xGEQueryVersionReply rep;
    xGEQueryVersionReq *req;
    XExtensionVersion *vers;


    LockDisplay(dpy);
    GetReq(GEQueryVersion, req);
    req->reqType = info->codes->major_opcode;
    req->ReqType = X_GEQueryVersion;
    req->majorVersion = GE_MAJOR;
    req->minorVersion = GE_MINOR;

    if (!_XReply (dpy, (xReply *) &rep, 0, xTrue)) 
    {
        UnlockDisplay (dpy);
        SyncHandle ();
        Xfree(info);
        return NULL;
    }

    vers = (XExtensionVersion*)Xmalloc(sizeof(XExtensionVersion));
    vers->major_version = rep.majorVersion;
    vers->minor_version = rep.minorVersion;
    UnlockDisplay (dpy);
    return vers;
}

/*
 * Display closing routine.
 */
  
static int
_xgeDpyClose(Display* dpy, XExtCodes* codes)
{
    XExtDisplayInfo *info = _xgeFindDisplay(dpy);

    if (info->data != NULL) {
        XGEData* xge_data = (XGEData*)info->data;

        if (xge_data->extensions)
        {
            XGEExtList current, next; 
            current = xge_data->extensions;
            while(current)
            {
                next = current->next;
                Xfree(current);
                current = next;
                next = next->next;
            }
        }

        XFree(xge_data->vers);
        XFree(xge_data);
    }

    return XextRemoveDisplay(xge_info, dpy);
}

/*
 * protocol to Xlib event conversion routine. 
 */
static Bool
_xgeWireToEvent(Display* dpy, XEvent* re, xEvent *event)
{
    int extension;
    XGEExtList it;
    XExtDisplayInfo* info = _xgeFindDisplay(dpy);
    if (!info)
        return False;
    if (!_xgeCheckExtInit(dpy, info))
        return False;

    extension = ((xGenericEvent*)event)->extension;

    it = ((XGEData*)info->data)->extensions;
    while(it)
    {
        if (it->extension == extension)
        {
            return (it->hooks->wire_to_event(dpy, re, event));
        }
        it = it->next;
    }

    fprintf(stderr, 
        "_xgeWireToEvent: Unknown extension %d, this should never happen.\n",
            extension);
    return False;
}

/*
 * xlib event to protocol conversion routine. 
 */
Status
_xgeEventToWire(Display* dpy, XEvent* re, xEvent* event)
{
    int extension;
    XGEExtList it;
    XExtDisplayInfo* info = _xgeFindDisplay(dpy);
    if (!info)
        return 1; /* error! */

    extension = ((XGenericEvent*)re)->extension;

    it = ((XGEData*)info->data)->extensions;
    while(it)
    {
        if (it->extension == extension)
        {
            return (it->hooks->event_to_wire(dpy, re, event));
        }
        it = it->next;
    }

    fprintf(stderr, 
        "_xgeEventToWire: Unknown extension %d, this should never happen.\n",
        extension);

    return Success;
}

/*
 * Extensions need to register callbacks for their events.
 */
Bool
xgeExtRegister(Display* dpy, int offset, XExtensionHooks* callbacks)
{
    XGEExtNode* newExt;
    XGEData* xge_data;

    XExtDisplayInfo* info = _xgeFindDisplay(dpy);
    if (!info)
        return False; /* error! */

    if (!_xgeCheckExtInit(dpy, info))
        return False;

    xge_data = (XGEData*)info->data;

    newExt = (XGEExtNode*)Xmalloc(sizeof(XGEExtNode));
    if (!newExt)
    {
        fprintf(stderr, "xgeExtRegister: Failed to alloc memory.\n");
        return False;
    }

    newExt->extension = offset;
    newExt->hooks = callbacks;
    newExt->next = xge_data->extensions;
    xge_data->extensions = newExt;

    return True;
}

/***********************************************************************/
/*                    Client interfaces                                */
/***********************************************************************/

/* Set event_base and error_base to the matching values for XGE. 
 * Note that since XGE doesn't use any errors and events, the actual return
 * value is of limited use.
 */
Bool 
XGEQueryExtension(Display* dpy, int* event_base, int* error_base)
{
    XExtDisplayInfo* info = _xgeFindDisplay(dpy);
    if (!_xgeCheckExtInit(dpy, info))
        return False;

    *event_base = info->codes->first_event;
    *error_base = info->codes->first_error;
    return True;
}

/* Get XGE version number. 
 * Doesn't actually get it from server, that should have been done beforehand
 * already 
 */
Bool
XGEQueryVersion(Display* dpy,
                int *major_version,
                int *minor_version)
{
    XExtDisplayInfo* info = _xgeFindDisplay(dpy);
    if (!info)
        return False;

    if (!_xgeCheckExtInit(dpy, info))
        return False;

    *major_version = ((XGEData*)info->data)->vers->major_version;
    *minor_version = ((XGEData*)info->data)->vers->minor_version;

    return True;
}

/***************************************************************************
 *   Copyright (C) 2012~2012 by CSSlayer                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/

/**
 * @file fcitximcontext.c
 *
 * This is a gtk im module for fcitx, using DBus as a protocol.
 *        This is compromise to gtk and firefox, users are being sucked by them
 *        again and again.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <clutter/x11/clutter-x11.h>
#include <clutter-imcontext/clutter-imcontext.h>
#include <clutter/clutter-keysyms.h>
#include "fcitx/fcitx.h"
#include "fcitximcontext.h"
#include "fcitx-config/fcitx-config.h"
#include "client.h"
#include <fcitx-utils/log.h>
#include <dbus/dbus-glib.h>
#include <sys/time.h>

#define LOG_LEVEL DEBUG

struct _FcitxIMContext {
    ClutterIMContext parent;
    ClutterIMRectangle area;
    FcitxIMClient* client;
    int has_focus;
    guint32 time;
    gboolean use_preedit;
    gboolean is_inpreedit;
    char* preedit_string;
    int cursor_pos;
};

typedef struct _ProcessKeyStruct {
    FcitxIMContext* context;
    ClutterKeyEvent* event;
} ProcessKeyStruct;

struct _FcitxIMContextClass {
    ClutterIMContextClass parent;
    /* klass members */
};

/* functions prototype */
static void     fcitx_im_context_class_init(FcitxIMContextClass   *klass);
static void     fcitx_im_context_init(FcitxIMContext        *im_context);
static void     fcitx_im_context_finalize(GObject               *obj);
static gboolean fcitx_im_context_filter_keypress(ClutterIMContext          *context,
        ClutterKeyEvent           *key);
static void     fcitx_im_context_reset(ClutterIMContext          *context);
static void     fcitx_im_context_focus_in(ClutterIMContext          *context);
static void     fcitx_im_context_focus_out(ClutterIMContext          *context);
static void     fcitx_im_context_show(ClutterIMContext          *context);
static void     fcitx_im_context_hide(ClutterIMContext          *context);
static void     fcitx_im_context_set_cursor_location(ClutterIMContext          *context,
        ClutterIMRectangle             *area);
static void     fcitx_im_context_set_use_preedit(ClutterIMContext          *context,
        gboolean               use_preedit);
static void     fcitx_im_context_get_preedit_string(ClutterIMContext          *context,
        gchar                **str,
        PangoAttrList        **attrs,
        gint                  *cursor_pos);


static void
_set_cursor_location_internal(FcitxIMContext *fcitxcontext);
static void
_fcitx_im_context_enable_im_cb(DBusGProxy* proxy, void* user_data);
static void
_fcitx_im_context_close_im_cb(DBusGProxy* proxy, void* user_data);
static void
_fcitx_im_context_commit_string_cb(DBusGProxy* proxy, char* str, void* user_data);
static void
_fcitx_im_context_forward_key_cb(DBusGProxy* proxy, guint keyval, guint state, gint type, void* user_data);
static void
_fcitx_im_context_update_preedit_cb(DBusGProxy* proxy, char* str, int cursor_pos, void* user_data);
static void
_fcitx_im_context_connect_cb(FcitxIMClient* client, void* user_data);
static void
_fcitx_im_context_destroy_cb(FcitxIMClient* client, void* user_data);
static void
_fcitx_im_context_set_capacity(FcitxIMContext* fcitxcontext);

static GType _fcitx_type_im_context = 0;

static guint _signal_commit_id = 0;
static guint _signal_preedit_changed_id = 0;
static guint _signal_preedit_start_id = 0;
static guint _signal_preedit_end_id = 0;
static guint _signal_delete_surrounding_id = 0;
static guint _signal_retrieve_surrounding_id = 0;


static
boolean FcitxIsHotKey(FcitxKeySym sym, int state, FcitxHotkey * hotkey);

static
boolean FcitxIsHotKey(FcitxKeySym sym, int state, FcitxHotkey * hotkey)
{
    state &= FcitxKeyState_Ctrl_Alt_Shift;
    if (hotkey[0].sym && sym == hotkey[0].sym && (hotkey[0].state == state))
        return true;
    if (hotkey[1].sym && sym == hotkey[1].sym && (hotkey[1].state == state))
        return true;
    return false;
}

void
fcitx_im_context_register_type(GTypeModule *type_module)
{
    static const GTypeInfo fcitx_im_context_info = {
        sizeof(FcitxIMContextClass),
        (GBaseInitFunc) NULL,
        (GBaseFinalizeFunc) NULL,
        (GClassInitFunc) fcitx_im_context_class_init,
        (GClassFinalizeFunc) NULL,
        NULL, /* klass data */
        sizeof(FcitxIMContext),
        0,
        (GInstanceInitFunc) fcitx_im_context_init,
        0
    };

    if (!_fcitx_type_im_context) {
        if (type_module) {
            _fcitx_type_im_context =
                g_type_module_register_type(type_module,
                                            CLUTTER_TYPE_IM_CONTEXT,
                                            "FcitxIMContext",
                                            &fcitx_im_context_info,
                                            (GTypeFlags)0);
        } else {
            _fcitx_type_im_context =
                g_type_register_static(CLUTTER_TYPE_IM_CONTEXT,
                                       "FcitxIMContext",
                                       &fcitx_im_context_info,
                                       (GTypeFlags)0);
        }
    }
}

GType
fcitx_im_context_get_type(void)
{
    if (_fcitx_type_im_context == 0) {
        fcitx_im_context_register_type(NULL);
    }

    g_assert(_fcitx_type_im_context != 0);
    return _fcitx_type_im_context;
}

FcitxIMContext *
fcitx_im_context_new(void)
{
    GObject *obj = g_object_new(FCITX_TYPE_IM_CONTEXT, NULL);
    return FCITX_IM_CONTEXT(obj);
}

///
static void
fcitx_im_context_class_init(FcitxIMContextClass *klass)
{
    ClutterIMContextClass *im_context_class = CLUTTER_IM_CONTEXT_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    im_context_class->filter_keypress = fcitx_im_context_filter_keypress;
    im_context_class->reset = fcitx_im_context_reset;
    im_context_class->get_preedit_string = fcitx_im_context_get_preedit_string;
    im_context_class->focus_in = fcitx_im_context_focus_in;
    im_context_class->focus_out = fcitx_im_context_focus_out;
    im_context_class->set_cursor_location = fcitx_im_context_set_cursor_location;
    im_context_class->set_use_preedit = fcitx_im_context_set_use_preedit;
    im_context_class->show = fcitx_im_context_show;
    im_context_class->hide = fcitx_im_context_hide;
    gobject_class->finalize = fcitx_im_context_finalize;

    _signal_commit_id =
        g_signal_lookup("commit", G_TYPE_FROM_CLASS(klass));
    g_assert(_signal_commit_id != 0);

    _signal_preedit_changed_id =
        g_signal_lookup("preedit-changed", G_TYPE_FROM_CLASS(klass));
    g_assert(_signal_preedit_changed_id != 0);

    _signal_preedit_start_id =
        g_signal_lookup("preedit-start", G_TYPE_FROM_CLASS(klass));
    g_assert(_signal_preedit_start_id != 0);

    _signal_preedit_end_id =
        g_signal_lookup("preedit-end", G_TYPE_FROM_CLASS(klass));
    g_assert(_signal_preedit_end_id != 0);

    _signal_delete_surrounding_id =
        g_signal_lookup("delete-surrounding", G_TYPE_FROM_CLASS(klass));
    g_assert(_signal_delete_surrounding_id != 0);

    _signal_retrieve_surrounding_id =
        g_signal_lookup("retrieve-surrounding", G_TYPE_FROM_CLASS(klass));
    g_assert(_signal_retrieve_surrounding_id != 0);
}


static void
fcitx_im_context_init(FcitxIMContext *context)
{
    FcitxLog(LOG_LEVEL, "fcitx_im_context_init");
    context->client = NULL;
    context->area.x = -1;
    context->area.y = -1;
    context->area.width = 0;
    context->area.height = 0;
    context->use_preedit = TRUE;
    context->cursor_pos = 0;
    context->preedit_string = NULL;

    context->time = CLUTTER_CURRENT_TIME;

    context->client = FcitxIMClientOpen(_fcitx_im_context_connect_cb, _fcitx_im_context_destroy_cb, G_OBJECT(context));
}

static void
fcitx_im_context_finalize(GObject *obj)
{
    FcitxLog(LOG_LEVEL, "fcitx_im_context_finalize");
    FcitxIMContext *context = FCITX_IM_CONTEXT(obj);

    FcitxIMClientClose(context->client);
    context->client = NULL;

    if (context->preedit_string)
        g_free(context->preedit_string);
    context->preedit_string = NULL;
}

///
static gboolean
fcitx_im_context_filter_keypress(ClutterIMContext *context,
                                 ClutterKeyEvent  *event)
{
    FcitxLog(LOG_LEVEL, "fcitx_im_context_filter_keypress");
    FcitxIMContext *fcitxcontext = FCITX_IM_CONTEXT(context);

    if (G_UNLIKELY(event->modifier_state & FcitxKeyState_HandledMask))
        return TRUE;

    if (G_UNLIKELY(event->modifier_state & FcitxKeyState_IgnoredMask))
        return FALSE;

    if (IsFcitxIMClientValid(fcitxcontext->client) && fcitxcontext->has_focus) {
        if (!IsFcitxIMClientEnabled(fcitxcontext->client)) {
            if (!FcitxIsHotKey(event->keyval, event->modifier_state, FcitxIMClientGetTriggerKey(fcitxcontext->client)))
                return FALSE;
        }

        fcitxcontext->time = event->time;

        int ret = FcitxIMClientProcessKeySync(fcitxcontext->client,
                                                event->keyval,
                                                event->hardware_keycode,
                                                event->modifier_state,
                                                (event->type == CLUTTER_KEY_PRESS) ? (FCITX_PRESS_KEY) : (FCITX_RELEASE_KEY),
                                                event->time);
        if (ret <= 0) {
            event->modifier_state |= FcitxKeyState_IgnoredMask;
            return FALSE;
        } else {
            event->modifier_state |= FcitxKeyState_HandledMask;
            return TRUE;
        }
    } else {
        return FALSE;
    }
    return FALSE;
}

static void
_fcitx_im_context_update_preedit_cb(DBusGProxy* proxy, char* str, int cursor_pos, void* user_data)
{
    FcitxLog(LOG_LEVEL, "_fcitx_im_context_commit_string_cb");
    FcitxIMContext* context =  FCITX_IM_CONTEXT(user_data);

    gboolean visible = false;

    if (context->preedit_string != NULL) {
        if (strlen(context->preedit_string) != 0)
            visible = true;
        g_free(context->preedit_string);
        context->preedit_string = NULL;
    }
    context->preedit_string = g_strdup(str);
    char* tempstr = g_strndup(str, cursor_pos);
    context->cursor_pos =  fcitx_utf8_strlen(tempstr);
    g_free(tempstr);

    gboolean new_visible = false;

    if (context->preedit_string != NULL) {
        if (strlen(context->preedit_string) != 0)
            new_visible = true;
    }
    gboolean flag = new_visible != visible;

    if (new_visible) {
        if (flag) {
            /* invisible => visible */
            g_signal_emit(context, _signal_preedit_start_id, 0);
        }
        g_signal_emit(context, _signal_preedit_changed_id, 0);
    } else {
        if (flag) {
            /* visible => invisible */
            g_signal_emit(context, _signal_preedit_changed_id, 0);
            g_signal_emit(context, _signal_preedit_end_id, 0);
        } else {
            /* still invisible */
            /* do nothing */
        }
    }

    g_signal_emit(context, _signal_preedit_changed_id, 0);
}


///
static void
fcitx_im_context_focus_in(ClutterIMContext *context)
{
    FcitxLog(LOG_LEVEL, "fcitx_im_context_focus_in");
    FcitxIMContext *fcitxcontext = FCITX_IM_CONTEXT(context);

    if (fcitxcontext->has_focus)
        return;

    fcitxcontext->has_focus = true;

    if (IsFcitxIMClientValid(fcitxcontext->client)) {
        FcitxIMClientFocusIn(fcitxcontext->client);
    }

    /* set_cursor_location_internal() will get origin from X server,
     * it blocks UI. So delay it to idle callback. */
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                    (GSourceFunc) _set_cursor_location_internal,
                    g_object_ref(fcitxcontext),
                    (GDestroyNotify) g_object_unref);

    return;
}

static void
fcitx_im_context_focus_out(ClutterIMContext *context)
{
    FcitxLog(LOG_LEVEL, "fcitx_im_context_focus_out");
    FcitxIMContext *fcitxcontext = FCITX_IM_CONTEXT(context);

    if (!fcitxcontext->has_focus) {
        return;
    }

    fcitxcontext->has_focus = false;

    if (IsFcitxIMClientValid(fcitxcontext->client)) {
        FcitxIMClientFocusOut(fcitxcontext->client);
    }


    if (fcitxcontext->preedit_string != NULL)
        g_free(fcitxcontext->preedit_string);
    fcitxcontext->preedit_string = NULL;
    fcitxcontext->cursor_pos = 0;
    g_signal_emit(fcitxcontext, _signal_preedit_changed_id, 0);
    g_signal_emit(fcitxcontext, _signal_preedit_end_id, 0);

    return;
}

void fcitx_im_context_show(ClutterIMContext* context)
{
    FcitxLog(LOG_LEVEL, "fcitx_im_context_focus_out");
    FcitxIMContext *fcitxcontext = FCITX_IM_CONTEXT(context);

    if (!fcitxcontext->has_focus) {
        clutter_im_context_focus_in(context);
    }
    
    if (IsFcitxIMClientValid(fcitxcontext->client) && !IsFcitxIMClientEnabled(fcitxcontext->client)) {
        FcitxIMClientEnableIC(fcitxcontext->client);
    }
}


void fcitx_im_context_hide(ClutterIMContext* context)
{
    FcitxLog(LOG_LEVEL, "fcitx_im_context_focus_out");
    FcitxIMContext *fcitxcontext = FCITX_IM_CONTEXT(context);
    
    if (IsFcitxIMClientValid(fcitxcontext->client) && IsFcitxIMClientEnabled(fcitxcontext->client)) {
        FcitxIMClientCloseIC(fcitxcontext->client);
    }

    if (fcitxcontext->has_focus) {
        clutter_im_context_focus_out(context);
    }
}


///
static void
fcitx_im_context_set_cursor_location(ClutterIMContext *context,
                                     ClutterIMRectangle *area)
{
    FcitxLog(LOG_LEVEL, "fcitx_im_context_set_cursor_location %d %d %d %d", area->x, area->y, area->height, area->width);
    FcitxIMContext *fcitxcontext = FCITX_IM_CONTEXT(context);

    if (fcitxcontext->area.x == area->x &&
            fcitxcontext->area.y == area->y &&
            fcitxcontext->area.width == area->width &&
            fcitxcontext->area.height == area->height) {
        return;
    }
    fcitxcontext->area = *area;

    if (IsFcitxIMClientValid(fcitxcontext->client)) {
        _set_cursor_location_internal(fcitxcontext);
    }

    return;
}

static void
_set_cursor_location_internal(FcitxIMContext *fcitxcontext)
{
    ClutterIMContext* context = CLUTTER_IM_CONTEXT(fcitxcontext);
    ClutterActor *stage = clutter_actor_get_stage (context->actor);
    Window current_window, root, parent, *childs;
    unsigned int nchild;
    XWindowAttributes winattr;
    Display *xdpy;
    float fx, fy;
    gint x, y;

    if (!stage)
        return;

    clutter_actor_get_transformed_position (context->actor, &fx, &fy);
    x = fx;
    y = fy;

    xdpy = clutter_x11_get_default_display ();
    current_window = clutter_x11_get_stage_window(CLUTTER_STAGE(stage));

    if (!xdpy || !current_window)
        return;

    while(1) {
        XGetWindowAttributes (xdpy, current_window, &winattr);
        x += winattr.x;
        y += winattr.y;

        XQueryTree(xdpy, current_window, &root, &parent, &childs, &nchild);
        current_window = parent;
        if (root == parent)
        break;
    }

    if (fcitxcontext->area.x != x || fcitxcontext->area.y != y) {
        fcitxcontext->area.x = x;
        fcitxcontext->area.y = y;
    }

    if (context->actor == NULL ||
        !IsFcitxIMClientValid(fcitxcontext->client)) {
        return;
    }

    ClutterIMRectangle area = fcitxcontext->area;
    if (area.x == -1 && area.y == -1 && area.width == 0 && area.height == 0) {
        area.y = 0;
        area.x = 0;
    }

    FcitxIMClientSetCursorLocation(fcitxcontext->client, area.x, area.y + area.height);
    return;
}

///
static void
fcitx_im_context_set_use_preedit(ClutterIMContext *context,
                                 gboolean      use_preedit)
{
    FcitxLog(LOG_LEVEL, "fcitx_im_context_set_use_preedit");
    FcitxIMContext *fcitxcontext = FCITX_IM_CONTEXT(context);

    fcitxcontext->use_preedit = use_preedit;
    _fcitx_im_context_set_capacity(fcitxcontext);
}

void
_fcitx_im_context_set_capacity(FcitxIMContext* fcitxcontext)
{
    if (IsFcitxIMClientValid(fcitxcontext->client)) {
        FcitxCapacityFlags flags = CAPACITY_NONE;
        if (fcitxcontext->use_preedit)
            flags |= CAPACITY_PREEDIT;
        FcitxIMClientSetCapacity(fcitxcontext->client, flags);

    }
}

///
static void
fcitx_im_context_reset(ClutterIMContext *context)
{
    FcitxLog(LOG_LEVEL, "fcitx_im_context_reset");
    FcitxIMContext *fcitxcontext = FCITX_IM_CONTEXT(context);

    if (IsFcitxIMClientValid(fcitxcontext->client)) {
        FcitxIMClientReset(fcitxcontext->client);
    }
}

static void
fcitx_im_context_get_preedit_string(ClutterIMContext   *context,
                                    gchar         **str,
                                    PangoAttrList **attrs,
                                    gint           *cursor_pos)
{
    FcitxLog(LOG_LEVEL, "fcitx_im_context_get_preedit_string");
    FcitxIMContext *fcitxcontext = FCITX_IM_CONTEXT(context);

    if (IsFcitxIMClientValid(fcitxcontext->client) && IsFcitxIMClientEnabled(fcitxcontext->client)) {
        if (str) {
            if (fcitxcontext->preedit_string)
                *str = strdup(fcitxcontext->preedit_string);
            else
                *str = strdup("");
        }
        if (attrs) {
            *attrs = pango_attr_list_new();

            if (str) {
                PangoAttribute *pango_attr;
                pango_attr = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
                pango_attr->start_index = 0;
                pango_attr->end_index = strlen(*str);
                pango_attr_list_insert(*attrs, pango_attr);
            }
        }
        if (cursor_pos)
            *cursor_pos = fcitxcontext->cursor_pos;

    } else {
        if (str) {
            *str = g_strdup("");
        }
        if (attrs) {
            *attrs = pango_attr_list_new();
        }
        if (cursor_pos)
            *cursor_pos = 0;
    }
    return ;
}

void _fcitx_im_context_enable_im_cb(DBusGProxy* proxy, void* user_data)
{
    FcitxLog(LOG_LEVEL, "_fcitx_im_context_enable_im_cb");
    FcitxIMContext* context =  FCITX_IM_CONTEXT(user_data);
    FcitxIMClientSetEnabled(context->client, true);
}

void _fcitx_im_context_close_im_cb(DBusGProxy* proxy, void* user_data)
{
    FcitxLog(LOG_LEVEL, "_fcitx_im_context_close_im_cb");
    FcitxIMContext* context =  FCITX_IM_CONTEXT(user_data);
    FcitxIMClientSetEnabled(context->client, false);

    if (context->preedit_string != NULL)
        g_free(context->preedit_string);
    context->preedit_string = NULL;
    context->cursor_pos = 0;
    g_signal_emit(context, _signal_preedit_changed_id, 0);
    g_signal_emit(context, _signal_preedit_end_id, 0);
}

void _fcitx_im_context_commit_string_cb(DBusGProxy* proxy, char* str, void* user_data)
{
    FcitxLog(LOG_LEVEL, "_fcitx_im_context_commit_string_cb");
    FcitxIMContext* context =  FCITX_IM_CONTEXT(user_data);
    g_signal_emit(context, _signal_commit_id, 0, str);
}

void _fcitx_im_context_forward_key_cb(DBusGProxy* proxy, guint keyval, guint state, gint type, void* user_data)
{
    FcitxLog(LOG_LEVEL, "_fcitx_im_context_forward_key_cb");
    ClutterIMContext* context =  CLUTTER_IM_CONTEXT(user_data);
    const char* signal_name;
    gboolean consumed = FALSE;
    FcitxKeyEventType tp = (FcitxKeyEventType) type;
    ClutterKeyEvent clutter_key_event;
    clutter_key_event.flags = 0;
    clutter_key_event.source = NULL;
    clutter_key_event.keyval = keyval;
    clutter_key_event.hardware_keycode = 0;
    clutter_key_event.unicode_value = 0;
    clutter_key_event.modifier_state = state;
    clutter_key_event.device = NULL;
    
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    clutter_key_event.time = current_time.tv_sec * 1000 + current_time.tv_usec / 1000;
    
    if (tp == FCITX_PRESS_KEY) {
        clutter_key_event.type = CLUTTER_KEY_PRESS;
        signal_name = "key-press-event";
    }
    else {
        clutter_key_event.type = CLUTTER_KEY_RELEASE;
        clutter_key_event.modifier_state |= CLUTTER_RELEASE_MASK;
        signal_name = "key-release-event";
    }
    clutter_key_event.modifier_state |= FcitxKeyState_IgnoredMask;
    clutter_key_event.stage = CLUTTER_STAGE (clutter_actor_get_stage(context->actor));
    
    g_signal_emit_by_name(context->actor, signal_name, &clutter_key_event, &consumed);
    
}

void _fcitx_im_context_connect_cb(FcitxIMClient* client, void* user_data)
{
    FcitxIMContext* context =  FCITX_IM_CONTEXT(user_data);
    if (IsFcitxIMClientValid(client)) {
        FcitxIMClientConnectSignal(client,
                                   G_CALLBACK(_fcitx_im_context_enable_im_cb),
                                   G_CALLBACK(_fcitx_im_context_close_im_cb),
                                   G_CALLBACK(_fcitx_im_context_commit_string_cb),
                                   G_CALLBACK(_fcitx_im_context_forward_key_cb),
                                   G_CALLBACK(_fcitx_im_context_update_preedit_cb),
                                   context,
                                   NULL);
        _fcitx_im_context_set_capacity(context);
    }

}

void _fcitx_im_context_destroy_cb(FcitxIMClient* client, void* user_data)
{
    FcitxIMClientSetEnabled(client, false);
}

// kate: indent-mode cstyle; space-indent on; indent-width 0;

/**
 * @file   Window.cc
 * @author David Reveman <c99drn@cs.umu.se>
 * @date   02-May-2001 21:43:03
 *
 * @brief Implementation of WaWindow and WaChildWindow classes
 *
 * An instance if this class manages one window. Contains functions for
 * creating window decorations, reading window hints and controlling the
 * window.
 *
 * Copyright (C) David Reveman. All rights reserved.
 *
 */

#ifdef    HAVE_CONFIG_H
#  include "../config.h"
#endif // HAVE_CONFIG_H

extern "C" {
#ifdef    HAVE_STDIO_H
#  include <stdio.h>
#endif // HAVE_STDIO_H

#ifdef    STDC_HEADERS
#  include <string.h>
#endif // STDC_HEADERS
}

#include "Window.hh"

/**
 * @fn    WaWindow(Window win_id, WaScreen *scrn) :
 *        WindowObject(win_id, WindowType)
 * @brief Constructor for WaWindow class
 *
 * Reparents the window, reads window hints and creates decorations ...
 *
 * @param win_id Resource ID of window to manage
 * @param scrn The screen the window should be displayed on
 */
WaWindow::WaWindow(Window win_id, WaScreen *scrn) :
    WindowObject(win_id, WindowType) {
    XWindowAttributes init_attrib;
    char *__m_wastrdup_tmp;

    id = win_id;
    wascreen = scrn;
    display = wascreen->display;
    screen_number = wascreen->screen_number;
    waimea = wascreen->waimea;
    ic = wascreen->ic;
    net = waimea->net;
    wm_strut = NULL;
    move_resize = false;
    classhint = NULL;
    name = __m_wastrdup("");

    XGrabServer(display);
    if (validateclient(id))
        XGetWindowAttributes(display, id, &init_attrib);
    else deleted = true;
    XUngrabServer(display);
    
    attrib.colormap = init_attrib.colormap;
    size.win_gravity = init_attrib.win_gravity;
    attrib.x = init_attrib.x;
    attrib.y = init_attrib.y;
    attrib.width  = init_attrib.width;
    attrib.height = init_attrib.height;
    
    want_focus = mapped = dontsend = deleted = ign_config_req = hidden = false;

    desktop_mask = (1L << wascreen->current_desktop->number);

#ifdef SHAPE
    shaped = false;
#endif //SHAPE

#ifdef RENDER
    render_if_opacity = false;
#endif // RENDER
    
    border_w = title_w = handle_w = 0;
    has_focus = false;
    flags.sticky = flags.shaded = flags.max = flags.title = flags.handle =
        flags.border = flags.all = flags.alwaysontop =
        flags.alwaysatbottom = flags.forcedatbottom = false;
    flags.focusable = flags.tasklist = true;
    frameacts = awinacts = pwinacts = titleacts = labelacts = handleacts =
        lgacts = rgacts = NULL;
    transient_for = (Window) 0;
    host = pid = NULL;
    
    int i;
    bacts = new list<WaAction *>*[wascreen->wstyle.b_num];
    for (i = 0; i < wascreen->wstyle.b_num; i++)
        bacts[i] = NULL;

    net->GetWMHints(this);
    net->GetMWMHints(this);
    net->GetWMNormalHints(this);
    net->GetWmPid(this);
    
    Gravitate(ApplyGravity);
    InitPosition();
    
    frame = new WaChildWindow(this, wascreen->id, FrameType);
    handle = new WaChildWindow(this, frame->id, HandleType);
    grip_l = new WaChildWindow(this, frame->id, LGripType);
    grip_r = new WaChildWindow(this, frame->id, RGripType);
    title = new WaChildWindow(this, frame->id, TitleType);

    WaChildWindow *button;
    int left_end = 2;
    int right_end = -2;
    int tw = (signed) wascreen->wstyle.title_height;
    list<ButtonStyle *>::iterator bit =
        wascreen->wstyle.buttonstyles.begin();
    for (i = 0; bit != wascreen->wstyle.buttonstyles.end(); ++bit, ++i) {
        button = new WaChildWindow(this, title->id, ButtonType);
        button->bstyle = *bit;
        button->f_texture = &(*bit)->t_focused;
        button->u_texture = &(*bit)->t_unfocused;
        if ((*bit)->autoplace == WestType)
            button->g_x = left_end;
        else if ((*bit)->autoplace == EastType)
            button->g_x = right_end;
        else button->g_x = (*bit)->x;
        
        if (button->g_x > 0 &&
            (button->g_x + (tw - 2)) > left_end)
            left_end = button->g_x + (tw - 2);
        else if ((button->g_x - (tw - 2)) < right_end)
            right_end = button->g_x - (tw - 2);
        
        buttons.push_back(button);
    }
    label = new WaChildWindow(this, title->id, LabelType);
    label->g_x = left_end + 2;
    label->g_x2 = right_end - 2;

    if (deleted) { delete this; return; }

    net->GetWmState(this);
    net->GetWmType(this);
    net->GetVirtualPos(this);
    net->GetWmStrut(this);
    net->GetDesktop(this);
    net->SetDesktop(this);
    net->SetDesktopMask(this);
    
    ReparentWin();
    if (! net->GetNetName(this)) net->GetXaName(this);
    if (*name == '\0') SetActionLists();
    UpdateGrabs();

#ifdef SHAPE
    Shape();
#endif // SHAPE

    if (deleted) { delete this; return; }
    
    UpdateAllAttributes();

    if (flags.shaded) Shade(NULL, NULL);
    
    waimea->window_table.insert(make_pair(id, this));
    wascreen->wawindow_list.push_back(this);
    wascreen->wawindow_list_map_order.push_back(this);
    if ((! flags.alwaysontop) && (! flags.alwaysatbottom) &&
        (! flags.forcedatbottom))
        wascreen->wa_list_stacking.push_back(this);

    if (deleted) delete this;
}

/**
 * @fn    ~WaWindow(void) 
 * @brief Destructor of WaWindow class
 *
 * Reparents the window back to the root window if it still exists. Destroys
 * all windows used for decorations.
 */
WaWindow::~WaWindow(void) {
    waimea->window_table.erase(id);

    if (transient_for) {
        if (transient_for == wascreen->id) {
            list<WaWindow *>::iterator it =
                wascreen->wawindow_list.begin();
            for (;it != wascreen->wawindow_list.end(); ++it)
                (*it)->transients.remove(id);
        }
        else {
            map<Window, WindowObject *>::iterator hit;
            if ((hit = waimea->window_table.find(transient_for)) !=
                waimea->window_table.end()) {
                if (((*hit).second)->type == WindowType)
                    ((WaWindow *) (*hit).second)->transients.remove(id);
            }
        }
    }
    
    XGrabServer(display);
    if (validateclient(id) && validateclient_mapped(id)) {
        XRemoveFromSaveSet(display, id);
        Gravitate(RemoveGravity);
        if (flags.shaded) attrib.height = restore_shade;
        if (attrib.x >= wascreen->width)
            attrib.x = attrib.x % wascreen->width;
        if (attrib.y >= wascreen->height)
            attrib.y = attrib.y % wascreen->height;

        if (attrib.x + attrib.width <= 0)
            attrib.x = wascreen->width + (attrib.x % wascreen->width);
        if (attrib.y + attrib.height <= 0)
            attrib.y = wascreen->height + (attrib.y % wascreen->height);

        XReparentWindow(display, id, wascreen->id, attrib.x, attrib.y);
    }
    XUngrabServer(display);     

    list<WaChildWindow *>::iterator bit = buttons.begin();
    for (; bit != buttons.end(); ++bit)
        delete *bit;
    
    delete grip_l;
    delete grip_r;
    delete handle;
    delete label;
    delete title;
    
    if (name) delete [] name;
    if (host) delete [] host;
    if (pid) delete [] pid;
    if (classhint && classhint->res_name) XFree(classhint->res_name);
    if (classhint && classhint->res_class) XFree(classhint->res_class);

    delete [] bacts;

    wascreen->wawindow_list.remove(this);
    wascreen->wawindow_list_map_order.remove(this);
    wascreen->wa_list_stacking.remove(this);
    if (flags.alwaysontop)
        wascreen->wawindow_list_stacking_aot.remove(this);
    if (flags.forcedatbottom) {
        wascreen->always_at_bottom_list.remove(frame->id);
    } else if (flags.alwaysatbottom)
        wascreen->wawindow_list_stacking_aab.remove(this);
    if (wm_strut) {
        wascreen->strut_list.remove(wm_strut);
        delete wm_strut;
        if (! wascreen->shutdown) wascreen->UpdateWorkarea();
    }
    
    delete frame;

    if (! wascreen->shutdown) {
        wascreen->net->SetClientList(wascreen);
        wascreen->net->SetClientListStacking(wascreen);
        if (! wascreen->wawindow_list.empty())
            wascreen->wawindow_list.front()->Focus(false);
    }
}

/**
 * @fn    GetActionList(list<WaActionExtList *> *e)
 * @brief Finds individual action list
 *
 * Matches windows class name, class and title with WaActionExtLists. Returns
 * WaActionExtLists action list if a match is found. If no match is found,
 * NULL is returned.
 *
 * @param e List with WaActionExtLists to try to match with
 */
list <WaAction *> *WaWindow::GetActionList(list<WaActionExtList *> *e) {
    list<WaActionExtList *>::iterator it;
    for (it = e->begin(); it != e->end(); ++it) {
        if (classhint) {
            if (classhint->res_name &&
                (*it)->name->Match(classhint->res_name))
                return &((*it)->list);
            else if (classhint->res_class &&
                     (*it)->cl->Match(classhint->res_class))
                return &((*it)->list);
        }
        if ((*it)->title->Match(name))
            return &((*it)->list);
    }
    return NULL;
}

/**
 * @fn    SetActionLists(void)
 * @brief Set all actions lists
 *
 * Updates all action lists for the window.
 */
void WaWindow::SetActionLists(void) {
    if (has_focus) {
        actionlist = GetActionList(&wascreen->config.ext_awinacts);
        if (! actionlist) actionlist = &wascreen->config.awinacts;
    }
    else {
        actionlist = GetActionList(&wascreen->config.ext_pwinacts);
        if (! actionlist) actionlist = &wascreen->config.pwinacts;
    }
    frame->actionlist = GetActionList(&wascreen->config.ext_frameacts);
    if (! frame->actionlist) frame->actionlist = &wascreen->config.frameacts;
    title->actionlist = GetActionList(&wascreen->config.ext_titleacts);
    if (! title->actionlist) title->actionlist = &wascreen->config.titleacts;
    label->actionlist = GetActionList(&wascreen->config.ext_labelacts);
    if (! label->actionlist) label->actionlist = &wascreen->config.labelacts;
    handle->actionlist = GetActionList(&wascreen->config.ext_handleacts);
    if (! handle->actionlist)
        handle->actionlist = &wascreen->config.handleacts;
    grip_l->actionlist = GetActionList(&wascreen->config.ext_lgacts);
    if (! grip_l->actionlist)
        grip_l->actionlist = &wascreen->config.lgacts;
    grip_r->actionlist = GetActionList(&wascreen->config.ext_rgacts);
    if (! grip_r->actionlist) grip_r->actionlist = &wascreen->config.rgacts;

    int i;
    list<WaChildWindow *>::iterator it = buttons.begin();
    for (i = 0; it != buttons.end(); ++it, ++i) {
        (*it)->actionlist = GetActionList(wascreen->config.ext_bacts[i]);
        if (! (*it)->actionlist)
            (*it)->actionlist = wascreen->config.bacts[i];
    }
}

/**
 * @fn    Gravitate(int multiplier)
 * @brief Applies or removes window gravity
 *
 * If multiplier parameter is RemoveGravity when we set the window
 * attributes so that the frame will match the windows position.
 * If multiplier parameter is ApplyGravity when we set the window
 * attributes so that the window will match the frames position.
 *
 * @param multiplier ApplyGravity or RemoveGravity
 */
void WaWindow::Gravitate(int multiplier) {
    switch (size.win_gravity) {
        case NorthWestGravity:
            attrib.x += multiplier * border_w * 2;
        case NorthEastGravity:
            attrib.x -= multiplier * border_w;
        case NorthGravity:
            attrib.y += multiplier * border_w;
            if (title_w) attrib.y += multiplier * (title_w + border_w);
            break;
        case SouthWestGravity:
            attrib.x += multiplier * border_w * 2;
        case SouthEastGravity:
            attrib.x -= multiplier * border_w;
        case SouthGravity:
            attrib.y -= multiplier * border_w;
            if (handle_w) attrib.y -= multiplier * (handle_w + border_w);
            break;
        case CenterGravity:
            attrib.x += multiplier * (border_w / 2);
            attrib.y += multiplier * (border_w / 2);
            if (title_w) attrib.y += multiplier * ((title_w + border_w) / 2);
            break;
        case StaticGravity:
            break;
    }
}

/**
 * @fn    InitPosition(void)
 * @brief Sets window position
 *
 * Initializes position for the window.
 */
void WaWindow::InitPosition(void) {
    if (size.min_width > attrib.width) attrib.width = size.min_width;
    if (size.min_height > attrib.height) attrib.height = size.min_height;
    restore_max.x = attrib.x;
    restore_max.y = attrib.y;
    restore_max.width = attrib.width;
    restore_shade = restore_max.height = attrib.height;
    restore_max.misc0 = restore_max.misc1 = 0;
    old_attrib.x = old_attrib.y = old_attrib.height = old_attrib.width =
        - 0xffff;
}

/**
 * @fn    MapWindow(void)
 * @brief Map window
 *
 * Map client window and all child windows.
 */
void WaWindow::MapWindow(void) {
    XGrabServer(display);
    if (validateclient(id)) {
        XMapWindow(display, id);
        RedrawWindow();
    } else DELETED;
    XUngrabServer(display);
    if (flags.handle) {
        XMapRaised(display, grip_l->id);
        XMapRaised(display, handle->id);
        XMapRaised(display, grip_r->id);
    } else {
        XUnmapWindow(display, grip_l->id);
        XUnmapWindow(display, handle->id);
        XUnmapWindow(display, grip_r->id);
    }
    if (flags.title) {
        XMapRaised(display, title->id);
        XMapRaised(display, label->id);
        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit)
            XMapRaised(display, (*bit)->id);
    } else {
        XUnmapWindow(display, title->id);
        XUnmapWindow(display, label->id);
        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit)
            XUnmapWindow(display, (*bit)->id);
    }
    if (desktop_mask & (1L << wascreen->current_desktop->number))
        XMapWindow(display, frame->id);
    else {
        hidden = true;
    }
    mapped = true;
}

/**
 * @fn    Show(void)
 * @brief Show window
 *
 * Map frame window if not mapped.
 */
void WaWindow::Show(void) {
    if (hidden && mapped) {
        XMapWindow(display, frame->id);
        hidden = false;
    }
}

/**
 * @fn    Hide(void)
 * @brief Hide window
 *
 * Unmap frame window if mapped.
 */
void WaWindow::Hide(void) {
    if (! hidden) {
        XUnmapWindow(display, frame->id);
        hidden = true;
    }
}

/**
 * @fn    UpdateAllAttributes(void)
 * @brief Updates all window attrubutes
 *
 * Updates all positions and sizes of all windows in the frame.
 */
void WaWindow::UpdateAllAttributes(void) {
    Gravitate(RemoveGravity);
    border_w = (flags.border * wascreen->wstyle.border_width);
    title_w  = (flags.title  * wascreen->wstyle.title_height);
    handle_w = (flags.handle * wascreen->wstyle.handle_width);
    Gravitate(ApplyGravity);
    
    frame->attrib.x = attrib.x - border_w;
    frame->attrib.y = attrib.y - border_w;
    if (flags.title) frame->attrib.y -= title_w + border_w;
    frame->attrib.width = attrib.width;
    frame->attrib.height = attrib.height;
    if (flags.title) frame->attrib.height += title_w + border_w;
    if (flags.handle) frame->attrib.height += handle_w + border_w;

    XSetWindowBorderWidth(display, frame->id, border_w);
    if (! flags.shaded) 
        XResizeWindow(display, frame->id, frame->attrib.width,
                      frame->attrib.height);
    
    XMoveWindow(display, frame->id, frame->attrib.x, frame->attrib.y);
            
    if (flags.title) {
        title->attrib.x = - border_w;
        title->attrib.y = - border_w;
        title->attrib.width  = attrib.width;
        title->attrib.height = title_w;
        XSetWindowBorderWidth(display, title->id, border_w);
        XMoveResizeWindow(display, title->id, title->attrib.x,
                          title->attrib.y, title->attrib.width,
                          title->attrib.height);
                
        label->attrib.x = label->g_x;
        label->attrib.y = 2;
        label->attrib.width = attrib.width + label->g_x2 - label->g_x;
        if (label->attrib.width < 1) label->attrib.width = 1;
        label->attrib.height = title_w - 4;
        XMoveResizeWindow(display, label->id, label->attrib.x,
                          label->attrib.y, label->attrib.width,
                          label->attrib.height);

        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit) {
            (*bit)->attrib.x = ((*bit)->g_x > 0)? (*bit)->g_x:
                (attrib.width + (*bit)->g_x - (title_w - 4));
            (*bit)->attrib.y = 2;
            (*bit)->attrib.width = title_w - 4;
            (*bit)->attrib.height = title_w - 4;
            XMoveResizeWindow(display, (*bit)->id, (*bit)->attrib.x,
                              (*bit)->attrib.y, (*bit)->attrib.width,
                              (*bit)->attrib.height);
        }
        DrawTitlebar(true);
    }
    if (flags.handle) {
        handle->attrib.x = 25;
        handle->attrib.y = frame->attrib.height - handle_w - border_w;
        handle->attrib.width = attrib.width - 50 -
            border_w * 2;
        if (handle->attrib.width < 1) handle->attrib.width = 1;
        handle->attrib.height = wascreen->wstyle.handle_width;
        XSetWindowBorderWidth(display, handle->id, border_w);
        XMoveResizeWindow(display, handle->id, handle->attrib.x,
                          handle->attrib.y, handle->attrib.width,
                          handle->attrib.height);
                
        grip_l->attrib.x = - border_w;
        grip_l->attrib.y = frame->attrib.height - handle_w - border_w;
        grip_l->attrib.width  = 25;
        grip_l->attrib.height = wascreen->wstyle.handle_width;
        XSetWindowBorderWidth(display, grip_l->id, border_w);
        XMoveResizeWindow(display, grip_l->id, grip_l->attrib.x,
                          grip_l->attrib.y, grip_l->attrib.width,
                          grip_l->attrib.height);
        
        grip_r->attrib.x = attrib.width - 25 - border_w;
        grip_r->attrib.y = frame->attrib.height - handle_w - border_w;
        grip_r->attrib.width  = 25;
        grip_r->attrib.height = wascreen->wstyle.handle_width;
        XSetWindowBorderWidth(display, grip_r->id, border_w);
        XMoveResizeWindow(display, grip_r->id, grip_r->attrib.x,
                          grip_r->attrib.y, grip_r->attrib.width,
                          grip_r->attrib.height);
        DrawHandlebar(true);
    }

    XGrabServer(display);
    if (validateclient(id)) {
        if (flags.title) XMoveWindow(display, id, 0, title_w + border_w);
        else XMoveWindow(display, id, 0, title_w);
    } else DELETED;
    XUngrabServer(display);

    int m_x, m_y, m_w, m_h;
    if (flags.max) {
        m_x = restore_max.x;
        m_y = restore_max.y;
        m_w = restore_max.width;
        m_h = restore_max.height;
        flags.max = false;
        _Maximize(restore_max.misc0, restore_max.misc1);
        restore_max.x = m_x;
        restore_max.y = m_y;
        restore_max.width = m_w;
        restore_max.height = m_h;
    }
    else
        RedrawWindow();

#ifdef SHAPE    
    Shape();
#endif // SHAPE
}

/**
 * @fn    RedrawWindow(bool force_if_viewable)
 * @brief Redraws Window
 *
 * Redraws the window at it's correct position with it's correct size.
 * We only redraw those things that need to be redrawn.
 *
 * @param force_if_viewable Force redraw if window is viewable
 */
void WaWindow::RedrawWindow(bool force_if_viewable) {
    Bool move = false, resize = false;

    if (old_attrib.x != attrib.x) {
        frame->attrib.x = attrib.x - border_w;
        
        old_attrib.x = attrib.x;

        move = true;
    }
    if (old_attrib.y != attrib.y) {
        frame->attrib.y = attrib.y - border_w;
        if (flags.title) frame->attrib.y -= title_w + border_w;
        old_attrib.y = attrib.y;
            
        move = true;
    }
    if (old_attrib.width != attrib.width) {
        frame->attrib.width = attrib.width;
        old_attrib.width = attrib.width;

        resize = true;

        if (flags.title) {
            title->attrib.width = attrib.width;
            label->attrib.width = attrib.width + label->g_x2 - label->g_x;
            if (label->attrib.width < 1) label->attrib.width = 1;

            list<WaChildWindow *>::iterator bit = buttons.begin();
            for (; bit != buttons.end(); ++bit) {
                (*bit)->attrib.x = ((*bit)->g_x > 0)? (*bit)->g_x:
                    (attrib.width + (*bit)->g_x - (title_w - 4));
                XMoveResizeWindow(display, (*bit)->id, (*bit)->attrib.x,
                                  (*bit)->attrib.y, (*bit)->attrib.width,
                                  (*bit)->attrib.height);
            }
            XResizeWindow(display, title->id, title->attrib.width,
                          title->attrib.height);
            XResizeWindow(display, label->id, label->attrib.width,
                          label->attrib.height);
            
#ifdef XFT
            if (wascreen->config.db) {
                Region region = XCreateRegion();
                XRectangle xrect;
                xrect.x = label->g_x;
                xrect.y = 2;
                xrect.width = label->attrib.width;
                xrect.height = label->attrib.height;
                XUnionRectWithRegion(&xrect, region, region);
                XftDrawSetClip(title->xftdraw, region);
                XDestroyRegion(region);
            }
#endif // XFT
            
            if (! force_if_viewable) DrawTitlebar();
        }
        if (flags.handle) {
            handle->attrib.width = attrib.width - 50 - border_w * 2;
            if (handle->attrib.width < 1) handle->attrib.width = 1;
            grip_r->attrib.x = attrib.width - 25 - border_w;

            XMoveWindow(display, grip_r->id, grip_r->attrib.x,
                        grip_r->attrib.y);
            XResizeWindow(display, handle->id, handle->attrib.width,
                          handle->attrib.height);
            
            if (! force_if_viewable) DrawHandlebar();
        }
    }
    if (old_attrib.height != attrib.height) {
        frame->attrib.height = attrib.height;
        if (flags.title) frame->attrib.height += title_w + border_w;
        if (flags.handle) frame->attrib.height += handle_w + border_w;
        old_attrib.height = attrib.height;
        
        resize = true;
        if (flags.handle) {
            handle->attrib.y = frame->attrib.height - handle_w - border_w;
            grip_l->attrib.y = frame->attrib.height - handle_w - border_w;
            grip_r->attrib.y = frame->attrib.height - handle_w - border_w;

            XMoveWindow(display, handle->id, handle->attrib.x,
                        handle->attrib.y);
            XMoveWindow(display, grip_l->id, grip_l->attrib.x,
                        grip_l->attrib.y);
            XMoveWindow(display, grip_r->id, grip_r->attrib.x,
                        grip_r->attrib.y);
        }
    }
    if (move) {
        if (flags.max) {
            restore_max.misc0 = wascreen->v_x + frame->attrib.x;
            restore_max.misc1 = wascreen->v_y + frame->attrib.y;
            net->SetWmState(this);
        }
        XMoveWindow(display, frame->id, frame->attrib.x, frame->attrib.y);
        
#ifdef RENDER
        if (! resize && ! force_if_viewable) {
            if (! wascreen->config.lazy_trans) {
                render_if_opacity = true;
                DrawTitlebar();
                DrawHandlebar();
                render_if_opacity = false;
            }
        }
#endif // RENDER

    }
    if (force_if_viewable) {
        DrawTitlebar();
        DrawHandlebar();
    }
    if (resize) {
        if (flags.max && (old_attrib.width != attrib.width ||
                          ! flags.shaded)) {
            flags.max = false;
            net->SetWmState(this);
            if (title_w) {
                list<WaChildWindow *>::iterator bit = buttons.begin();
                for (; bit != buttons.end(); ++bit) {
                    if ((*bit)->bstyle->cb == ShadeCBoxType) (*bit)->Render();
                }
            }
            wascreen->UpdateCheckboxes(MaxCBoxType);
        }
        XGrabServer(display);
        if (validateclient(id)) {    
            if (flags.shaded)
                XResizeWindow(display, id, attrib.width, restore_shade);
            else
                XResizeWindow(display, id, attrib.width, attrib.height);
            XResizeWindow(display, frame->id, frame->attrib.width,
                          frame->attrib.height);
        } else DELETED;
        XUngrabServer(display);

#ifdef SHAPE
        Shape();
#endif // SHAPE
        
    }
    if ((move || resize) && (! flags.shaded) && (! dontsend)) {
        
#ifdef RENDER
        if (wascreen->config.lazy_trans) {
            render_if_opacity = true;
            DrawTitlebar();
            DrawHandlebar();
            render_if_opacity = false;
        }
#endif // RENDER

        net->SetVirtualPos(this);
        SendConfig();
    }
}

/**
 * @fn    ReparentWin(void)
 * @brief Reparents the window into the frame
 *
 * Sets the input mask for the managed window so that it will report
 * FocusChange and PropertyChange events. Then we reparent the window into
 * our frame and activates needed grab buttons.
 */
void WaWindow::ReparentWin(void) {
    XSetWindowAttributes attrib_set;
    
    XGrabServer(display);
    if (validateclient(id)) {
        XSelectInput(display, id, NoEventMask);
        XSetWindowBorderWidth(display, id, 0);
        XReparentWindow(display, id, frame->id, 0, title_w + border_w);
        XChangeSaveSet(display, id, SetModeInsert);
        XFlush(display);

        attrib_set.event_mask =  
            PropertyChangeMask | StructureNotifyMask | FocusChangeMask;
        attrib_set.do_not_propagate_mask =
            ButtonPressMask | ButtonReleaseMask | ButtonMotionMask; 
        
        XChangeWindowAttributes(display, id, CWEventMask | CWDontPropagate,
                                &attrib_set);
        
        
#ifdef SHAPE
        XRectangle *dummy = NULL;
        int n, order;
        if (waimea->shape) {
            XShapeSelectInput(display, id, ShapeNotifyMask);
            dummy = XShapeGetRectangles(display, id, ShapeBounding, &n,
                                        &order);
            if (n > 1) shaped = true;
        }
        XFree(dummy);
#endif // SHAPE
        
    } else DELETED;
    XUngrabServer(display);
}

/**
 * @fn    UpdateGrabs(void)
 * @brief Update window grabs
 *
 * Updates passive window grabs for the window.
 */
void WaWindow::UpdateGrabs(void) {
    XGrabServer(display);
    if (validateclient_mapped(id)) {
        XUngrabButton(display, AnyButton, AnyModifier, id);
        XUngrabKey(display, AnyKey, AnyModifier, id);
        list<WaAction *>::iterator it = actionlist->begin();
        for (; it != actionlist->end(); ++it) {
            if ((*it)->type == ButtonPress || (*it)->type == ButtonRelease ||
                (*it)->type == DoubleClick) {
                XGrabButton(display, (*it)->detail ? (*it)->detail: AnyButton,
                            AnyModifier, id, true, ButtonPressMask |
                            ButtonReleaseMask | ButtonMotionMask,
                            GrabModeSync, GrabModeSync, None, None);     
            } else if ((*it)->type == KeyPress || (*it)->type == KeyRelease) {
                XGrabKey(display, (*it)->detail ? (*it)->detail: AnyKey,
                         AnyModifier, id, true, GrabModeSync, GrabModeSync);
            }
        }
    } else DELETED;
    XUngrabServer(display);
}


#ifdef SHAPE
/**
 * @fn    Shape(void)
 * @brief Set Shape of frame window
 *
 * Shapes frame window after shape of client.
 */
void WaWindow::Shape(void) {
    int n;
    XRectangle xrect[2];
        
    if (shaped) {
        XGrabServer(display);
        if (validateclient(id)) {
            XShapeCombineShape(display, frame->id, ShapeBounding,
                               border_w, title_w + border_w, id,
                               ShapeBounding, ShapeSet);
            n = 0;
            if (title_w) {
                xrect[n].x = -border_w;
                xrect[n].y = -border_w;
                xrect[n].width = attrib.width + border_w * 2;
                xrect[n].height = title_w + border_w * 2;
                n++;
            }
            if (handle_w) {
                xrect[n].x = -border_w;
                xrect[n].y = attrib.height + title_w;
                if (title_w) xrect[n].y += border_w;
                xrect[n].width = attrib.width + border_w * 2;
                xrect[n].height = handle_w + border_w * 2;
                n++;
            }
            XShapeCombineRectangles(display, frame->id, ShapeBounding, 0,
                                    0, xrect, n, ShapeUnion, Unsorted);
        } else DELETED;
        XUngrabServer(display);
    }
}
#endif // SHAPE


/**
 * @fn    SendConfig(void)
 * @brief Sends ConfigureNotify to window
 *
 * Sends a ConfigureNotify event to the window with all the current 
 * window attributes.
 */
void WaWindow::SendConfig(void) {
    XConfigureEvent ce;

    ce.type              = ConfigureNotify;
    ce.event             = id;
    ce.window            = id;
    ce.x                 = attrib.x;
    ce.y                 = attrib.y;
    ce.width             = attrib.width;
    ce.border_width      = 0;
    ce.above             = frame->id;
    ce.override_redirect = false;

    if (flags.shaded)
        ce.height = restore_shade;
    else
        ce.height = attrib.height;

    XGrabServer(display);
    if (validateclient(id)) { 
        XSendEvent(display, id, true, NoEventMask, (XEvent *)&ce);
        XSendEvent(display, wascreen->id, false,
                   StructureNotifyMask, (XEvent *)&ce);
    }
    else DELETED;
    XUngrabServer(display);
}

/**
 * @fn    CreateOutline(void)
 * @brief Creates window outline
 *
 * Creates four windows used for displaying an outline when doing
 * non opaque moving and resizing of the window.
 */
void WaWindow::CreateOutline(void) {
    XSetWindowAttributes attrib_set;
    
    int create_mask = CWOverrideRedirect | CWBackPixel | CWEventMask |
        CWColormap;
    attrib_set.background_pixel = wascreen->wstyle.outline_color.getPixel();
    attrib_set.colormap = wascreen->colormap;
    attrib_set.override_redirect = true;
    attrib_set.event_mask = NoEventMask;
    
    o_west = XCreateWindow(display, wascreen->id, 0, 0, 1, 1, 0,
                           screen_number, CopyFromParent, wascreen->visual,
                           create_mask, &attrib_set);
    o_east = XCreateWindow(display, wascreen->id, 0, 0, 1, 1, 0,
                           screen_number, CopyFromParent, wascreen->visual,
                           create_mask, &attrib_set);
    o_north = XCreateWindow(display, wascreen->id, 0, 0, 1, 1, 0,
                            screen_number, CopyFromParent, wascreen->visual,
                            create_mask, &attrib_set);
    o_south = XCreateWindow(display, wascreen->id, 0, 0, 1, 1, 0,
                            screen_number, CopyFromParent, wascreen->visual,
                            create_mask, &attrib_set);
    wascreen->always_on_top_list.push_back(o_west);
    wascreen->always_on_top_list.push_back(o_east);
    wascreen->always_on_top_list.push_back(o_north);
    wascreen->always_on_top_list.push_back(o_south);
    XMapWindow(display, o_west);
    XMapWindow(display, o_east);
    XMapWindow(display, o_north);
    XMapWindow(display, o_south);
    wascreen->WaRaiseWindow(0);
}

/**
 * @fn    DestroyOutline(void)
 * @brief Destroys window outline
 *
 * Destorys the four outline windows.
 */
void WaWindow::DestroyOutline(void) {
    wascreen->always_on_top_list.remove(o_west);
    wascreen->always_on_top_list.remove(o_east);
    wascreen->always_on_top_list.remove(o_north);
    wascreen->always_on_top_list.remove(o_south);
    XDestroyWindow(display, o_west);
    XDestroyWindow(display, o_east);
    XDestroyWindow(display, o_north);
    XDestroyWindow(display, o_south);
}

/**
 * @fn    DrawOutline(int x, int y, int width, int height)
 * @brief Draw window outline
 *
 * Draws an outer line for a window with the parameters given.
 * 
 * @param x The x position
 * @param y The y position
 * @param width The width
 * @param height The height
 */
void WaWindow::DrawOutline(int x, int y, int width, int height) {
    int bw = (border_w) ? border_w: 2;
    
    XResizeWindow(display, o_west, bw, bw * 2 + title_w + handle_w + height +
                  border_w * 2);
    XResizeWindow(display, o_east, bw, bw * 2 + title_w + handle_w + height +
                  border_w * 2);
    XResizeWindow(display, o_north, width + bw * 2, bw);
    XResizeWindow(display, o_south, width + bw * 2, bw);

    XMoveWindow(display, o_west, x - bw, y - title_w - border_w - bw);
    XMoveWindow(display, o_east, x + width, y - title_w - border_w - bw);
    XMoveWindow(display, o_north, x - bw, y - title_w - border_w - bw);
    XMoveWindow(display, o_south, x - bw, y + height + handle_w + border_w);
}

/**
 * @fn    DrawTitlebar(bool force)
 * @brief Draw window titlebar
 *
 * Renders titlebar pixmaps and draws titlebar foreground.
 *
 * @param force True if rendering should be forced
 */
void WaWindow::DrawTitlebar(bool force) {
    if (force || (title_w &&
                  ((attrib.x + attrib.width) > 0 &&
                   attrib.x < wascreen->width) &&
                  ((attrib.y - border_w) > 0 &&
                   (attrib.y - border_w - title_w) < wascreen->height))) {
        title->Render();
        label->Render();
        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit)
            (*bit)->Render();
    }
}

/**
 * @fn    DrawHandlebar(bool force)
 * @brief Draw window handlebar
 *
 * Renders handlebar pixmaps and draws handlebar foreground.
 *
 * @param force True if rendering should be forced
 */
void WaWindow::DrawHandlebar(bool force) {
    if (force || (handle_w &&
                  ((attrib.x + attrib.width) > 0 &&
                   attrib.x < wascreen->width) &&
                  (attrib.y + attrib.height + border_w + handle_w) > 0 &&
                  (attrib.y + attrib.height + border_w) < wascreen->height)) {
        handle->Render();
        grip_r->Render();
        grip_l->Render();
    }
}

/**
 * @fn    FocusWin(void)
 * @brief Sets window to have the look of a focused window
 *
 * Sets window decoration pointers to point so that window decorations will
 * represent a focused window. Redraws needed graphics.
 */
void WaWindow::FocusWin(void) {
    if (has_focus) return;
    has_focus = true;
    if (title_w) DrawTitlebar(true);
    if (handle_w) DrawHandlebar(true);
}

/**
 * @fn    UnFocusWin(void)
 * @brief Sets window to have the look of a unfocused window
 *
 * Sets window decoration pointers to point so that window decorations will
 * represent an unfocused window. Redraws needed graphics.
 */
void WaWindow::UnFocusWin(void) {
    if (! has_focus) return;
    has_focus = false;
    if (title_w)  DrawTitlebar(true);
    if (handle_w) DrawHandlebar(true);
}

/**     
 * @fn    ButtonPressed(int type)
 * @brief Titlebar buttonpress
 *      
 * Performes button press animation on one of the titlebar buttons.
 *      
 * @param button WaChildWindow pointer to button that should be pressed
 */
void WaWindow::ButtonPressed(WaChildWindow *button) {
    XEvent e;
    bool in_window = true;
    
    if (waimea->eh->move_resize != EndMoveResizeType) return;

    XUngrabButton(display, AnyButton, AnyModifier, id);
    XUngrabKey(display, AnyKey, AnyModifier, id);

    button->pressed = true;
    button->Render();
    for (;;) {
        XMaskEvent(display, ButtonReleaseMask | EnterWindowMask |
                   LeaveWindowMask, &e);
        switch (e.type) {
            case EnterNotify:
                in_window = true;
                button->pressed = true;
                button->Render();
                break;
            case LeaveNotify:
                button->pressed = false;
                button->Render();
                in_window = false;
                break;
            case ButtonRelease:
                button->pressed = false;
                button->Render();
                if (in_window) XPutBackEvent(display, &e);
                UpdateGrabs();
                waimea->eh->move_resize = EndMoveResizeType;
                return;
        }
    }
}

/**
 * @fn    IncSizeCheck(int width, int height, int *n_w, int *n_h)
 * @brief Calculate increasement sizes
 *
 * Given a new width and height this functions calculates if the windows
 * increasement sizes allows a resize of the window. The n_w parameter 
 * is used for returning allowed width and the n_h parameter is used for
 * returning allowed height.
 *
 * @param width Width we want to resize to
 * @param height Height we want to resize to
 * @param n_w Return of allowed width
 * @param n_h Return of allowed height
 *
 * @return True if a resize is allowed otherwise false
 */
bool WaWindow::IncSizeCheck(int width, int height, int *n_w, int *n_h) {
    bool resize = false;

    *n_w = attrib.width;
    *n_h = attrib.height;
    if ((width >= (attrib.width + size.width_inc)) ||
        (width <= (attrib.width - size.width_inc)) ||
        attrib.width == width) {
        if (width >= size.min_width && width <= size.max_width) {
            resize = true;
            if (size.width_inc == 1)
                *n_w = width;
            else
                *n_w = width - ((width - size.base_width) % size.width_inc);
        }
    }
    if ((height <= -(handle_w + border_w * 2)) && title_w) {
        if (! flags.shaded) {
            flags.shaded = true;
            restore_shade = attrib.height;
            net->SetWmState(this);
            if (title_w) {
                list<WaChildWindow *>::iterator bit = buttons.begin();
                for (; bit != buttons.end(); ++bit)
                    if ((*bit)->bstyle->cb == ShadeCBoxType) (*bit)->Render();
            }
            wascreen->UpdateCheckboxes(ShadeCBoxType);
        }
        *n_h = -(handle_w + border_w);
        if (handle_w) *n_h -= border_w;
        return resize;
    }
    if ((height >= (attrib.height + size.height_inc)) ||
        (height <= (attrib.height - size.height_inc)) ||
        attrib.height == height) {
        if ((height < 1) && (size.min_height <= 1) && title_w) {
            resize = true;
            if (! flags.shaded) {
                flags.shaded = true;
                restore_shade = attrib.height;
                net->SetWmState(this);
                if (title_w) {
                    list<WaChildWindow *>::iterator bit = buttons.begin();
                    for (; bit != buttons.end(); ++bit)
                        if ((*bit)->bstyle->cb == ShadeCBoxType)
                            (*bit)->Render();
                }
                wascreen->UpdateCheckboxes(ShadeCBoxType);
            }
            if (size.height_inc == 1)
                *n_h = height;
            else
                *n_h = height -
                    ((height - size.base_height) % size.height_inc);
        }
        else if (height >= size.min_height && height <= size.max_height) {
            resize = true;
            if (flags.shaded) {
                flags.shaded = false;
                net->SetWmState(this);
                if (title_w) {
                    list<WaChildWindow *>::iterator bit = buttons.begin();
                    for (; bit != buttons.end(); ++bit)
                        if ((*bit)->bstyle->cb == ShadeCBoxType)
                            (*bit)->Render();
                    
                }
                wascreen->UpdateCheckboxes(ShadeCBoxType);
            }
            if (size.height_inc == 1)
                *n_h = height;
            else
                *n_h = height -
                    ((height - size.base_height) % size.height_inc);
        }
    }
    return resize;
}

/**
 * @fn    Raise(XEvent *, WaAction *)
 * @brief Raises the window
 *
 * Raises the window to the top of the display stack and redraws window 
 * foreground.
 */
void WaWindow::Raise(XEvent *, WaAction *) {
    if (flags.forcedatbottom) return;
    if (! flags.alwaysontop && ! flags.alwaysatbottom) {
        wascreen->wa_list_stacking.remove(this);
        wascreen->wa_list_stacking.push_front(this);
        if (! transients.empty()) {
            list<Window>::iterator it = transients.begin();
            for (;it != transients.end(); ++it) {
                WaWindow *ww = (WaWindow *) waimea->FindWin(*it, WindowType);
                if (ww) {
                    wascreen->wa_list_stacking.remove(ww);
                    wascreen->wa_list_stacking.push_front(ww);
                }
                else {
                    transients.erase(it);
                    it = transients.begin();
                }
            }
        }
        wascreen->WaLowerWindow((Window) 0);
        net->SetClientListStacking(wascreen);
    }
}

/**
 * @fn    Lower(XEvent *, WaAction *)
 * @brief Lowers the window
 *
 * Lowers the window to the bottom of the display stack
 */
void WaWindow::Lower(XEvent *, WaAction *) {
    if (flags.forcedatbottom) return;
    if (! flags.alwaysontop && ! flags.alwaysatbottom) {
        wascreen->wa_list_stacking.remove(this);
        wascreen->wa_list_stacking.push_back(this);
        wascreen->WaLowerWindow(frame->id);
        net->SetClientListStacking(wascreen);
    }
}

/**
 * @fn    Focus(bool vis)
 * @brief Sets input focus to the window
 *
 * Gives window keyboard input focus. If vis parameter is true we make sure
 * the window is viewable.
 *
 * @param vis True if we should make sure the window is viewable
 */
void WaWindow::Focus(bool vis) {
    int newvx, newvy, x, y;
    XEvent e;
    if (! flags.focusable || ((! vis) && hidden)) return;
    
    if (mapped) {
        if (vis) {
            if (! (desktop_mask &
                   (1L << wascreen->current_desktop->number))) {
                list<Desktop *>::iterator dit =
                    wascreen->desktop_list.begin();
                for (; dit != wascreen->desktop_list.end(); dit++)
                    if (desktop_mask & (1L << (*dit)->number)) {
                        wascreen->GoToDesktop((*dit)->number);
                        break;
                    }
            }
            if (attrib.x >= wascreen->width ||
                attrib.y >= wascreen->height ||
                (attrib.x + attrib.width) <= 0 ||
                (attrib.y + attrib.height) <= 0) {
                x = wascreen->v_x + attrib.x;
                y = wascreen->v_y + attrib.y;
                newvx = (x / wascreen->width) * wascreen->width;
                newvy = (y / wascreen->height) * wascreen->height;
                wascreen->MoveViewportTo(newvx, newvy);
                XSync(display, false);
                while (XCheckTypedEvent(display, EnterNotify, &e));
            }
        }
        XInstallColormap(display, attrib.colormap);
        XGrabServer(display);
        if (validateclient_mapped(id)) {
            XSetInputFocus(display, id, RevertToPointerRoot, CurrentTime);
        } else DELETED;
        XUngrabServer(display);
    } else
        want_focus = true;
}

/**
 * @fn    Move(XEvent *e)
 * @brief Moves the window
 *
 * Moves the window through displaying an outline of the window while dragging
 * the mouse.
 *
 * @param e XEvent causing start of move
 */
void WaWindow::Move(XEvent *e, WaAction *) {
    XEvent event, *map_ev;
    int px, py, nx, ny, i;
    list<XEvent *> *maprequest_list;
    bool started = false;
    Window w;
    unsigned int ui;
    
    XQueryPointer(display, wascreen->id, &w, &w, &px, &py, &i, &i, &ui);

    if (waimea->eh->move_resize != EndMoveResizeType) return;
    nx = attrib.x;
    ny = attrib.y;
    waimea->eh->move_resize = MoveType;
    move_resize = true;

    if (e->type == MapRequest) {
        nx = attrib.x = px + border_w;
        ny = attrib.y = py + title_w + border_w;
        CreateOutline();
        DrawOutline(nx, ny, attrib.width, attrib.height);
        started = true;
    }
    maprequest_list = new list<XEvent *>;
    XGrabServer(display);
    if (validateclient(id)) {
        if (XGrabPointer(display, (mapped && !hidden) ? id: wascreen->id, true,
                         ButtonReleaseMask | ButtonPressMask |
                         PointerMotionMask | EnterWindowMask |
                         LeaveWindowMask, GrabModeAsync, GrabModeAsync,
                         None, waimea->move_cursor, CurrentTime)
            != GrabSuccess) {
            move_resize = false;
            waimea->eh->move_resize = EndMoveResizeType;
            return;
        }
        if (XGrabKeyboard(display, (mapped && !hidden) ? id: wascreen->id,
                          true, GrabModeAsync, GrabModeAsync, CurrentTime)
            != GrabSuccess) {
            move_resize = false;
            waimea->eh->move_resize = EndMoveResizeType;
            return;
        }
    } else DELETED;
    XUngrabServer(display);
    for (;;) {
        waimea->eh->EventLoop(waimea->eh->moveresize_return_mask, &event);
        switch (event.type) {
            case MotionNotify:
                while (XCheckTypedWindowEvent(display, event.xmotion.window,
                                              MotionNotify, &event));
                nx += event.xmotion.x_root - px;
                ny += event.xmotion.y_root - py;
                px  = event.xmotion.x_root;
                py  = event.xmotion.y_root;
                if (! started) {
                    CreateOutline();
                    started = true;
                }
                DrawOutline(nx, ny, attrib.width, attrib.height);
                break;
            case LeaveNotify:
            case EnterNotify:
                if (wascreen->west->id == event.xcrossing.window ||
                    wascreen->east->id == event.xcrossing.window ||
                    wascreen->north->id == event.xcrossing.window ||
                    wascreen->south->id == event.xcrossing.window) {
                    waimea->eh->HandleEvent(&event);
                } else if (event.type == LeaveNotify) {
                    int cx, cy;
                    XQueryPointer(display, wascreen->id, &w, &w, &cx, &cy, &i,
                                  &i, &ui);
                    nx += cx - px;
                    ny += cy - py;
                    px = cx;
                    py = cy;
                    if (! started) {
                        CreateOutline();
                        started = true;
                    }
                    DrawOutline(nx, ny, attrib.width, attrib.height);
                }
                break;
            case DestroyNotify:
            case UnmapNotify:
                if ((((event.type == UnmapNotify)? event.xunmap.window:
                      event.xdestroywindow.window) == id)) {
                    while (! maprequest_list->empty()) {
                        XPutBackEvent(display, maprequest_list->front());
                        delete maprequest_list->front();
                        maprequest_list->pop_front();
                    }
                    delete maprequest_list;
                    XPutBackEvent(display, &event);
                    if (started) DestroyOutline();
                    XUngrabKeyboard(display, CurrentTime);
                    XUngrabPointer(display, CurrentTime);
                    waimea->eh->move_resize = EndMoveResizeType;
                    move_resize = false;
                    return;
                }
                waimea->eh->EvUnmapDestroy(&event);
                break;
            case ConfigureRequest:
                if (event.xconfigurerequest.window != id)
                    waimea->eh->EvConfigureRequest(&event.xconfigurerequest);
                break;
            case MapRequest:
                map_ev = new XEvent;
                *map_ev = event;
                maprequest_list->push_front(map_ev); break;
            case ButtonPress:
            case ButtonRelease:
                event.xbutton.window = id;
            case KeyPress:
            case KeyRelease:
                if (event.type == KeyPress || event.type == KeyRelease)
                    event.xkey.window = id;
                waimea->eh->HandleEvent(&event);
                DrawOutline(nx, ny, attrib.width, attrib.height);
                if (waimea->eh->move_resize != EndMoveResizeType) break;
                if (started) DestroyOutline();
                attrib.x = nx;
                attrib.y = ny;
                RedrawWindow();
                while (! maprequest_list->empty()) {
                    XPutBackEvent(display, maprequest_list->front());
                    delete maprequest_list->front();
                    maprequest_list->pop_front();
                }
                delete maprequest_list;
                move_resize = false;
                XUngrabKeyboard(display, CurrentTime);
                XUngrabPointer(display, CurrentTime);
                return;
        }
    }
}

/**
 * @fn    MoveOpaque(XEvent *e, WaAction *)
 * @brief Moves the window
 *
 * Moves the window using the opaque moving process, which means that the
 * window is moved after the mouse pointers every motion event.
 *
 * @param e XEvent causing start of move
 */
void WaWindow::MoveOpaque(XEvent *e, WaAction *) {
    XEvent event, *map_ev;
    int sx, sy, px, py, nx, ny, i;
    list<XEvent *> *maprequest_list;
    Window w;
    unsigned int ui;

    if (waimea->eh->move_resize != EndMoveResizeType) return;
    sx = nx = attrib.x;
    sy = ny = attrib.y;
    waimea->eh->move_resize = MoveOpaqueType;
    move_resize = true;

    XQueryPointer(display, wascreen->id, &w, &w, &px, &py, &i, &i, &ui);

    if (e->type == MapRequest) {
        nx = attrib.x = px + border_w;
        ny = attrib.y = py + title_w + border_w;
        RedrawWindow();
        net->SetState(this, NormalState);
        net->SetVirtualPos(this);
    }
    dontsend = true;
    maprequest_list = new list<XEvent *>;
    XGrabServer(display);
    if (validateclient(id)) {
        if (XGrabPointer(display, (mapped && !hidden) ? id: wascreen->id, true,
                         ButtonReleaseMask | ButtonPressMask |
                         PointerMotionMask | EnterWindowMask |
                         LeaveWindowMask, GrabModeAsync, GrabModeAsync,
                         None, waimea->move_cursor, CurrentTime)
            != GrabSuccess) {
            move_resize = false;
            waimea->eh->move_resize = EndMoveResizeType;
            return;
        }
        if (XGrabKeyboard(display, (mapped && !hidden) ? id: wascreen->id,
                          true, GrabModeAsync, GrabModeAsync, CurrentTime)
            != GrabSuccess) {
            move_resize = false;
            waimea->eh->move_resize = EndMoveResizeType;
            return;
        }
    } else DELETED;
    XUngrabServer(display);
    for (;;) {
        waimea->eh->EventLoop(waimea->eh->moveresize_return_mask, &event);
        switch (event.type) {
            case MotionNotify:
                while (XCheckTypedWindowEvent(display, event.xmotion.window,
                                              MotionNotify, &event));
                nx += event.xmotion.x_root - px;
                ny += event.xmotion.y_root - py;
                px = event.xmotion.x_root;
                py = event.xmotion.y_root;
                attrib.x = nx;
                attrib.y = ny;
                RedrawWindow();
                break;
            case LeaveNotify:
            case EnterNotify:
                if (wascreen->west->id == event.xcrossing.window ||
                    wascreen->east->id == event.xcrossing.window ||
                    wascreen->north->id == event.xcrossing.window ||
                    wascreen->south->id == event.xcrossing.window) {
                    waimea->eh->HandleEvent(&event);
                } else if (event.type == LeaveNotify) {
                    int cx, cy;
                    XQueryPointer(display, wascreen->id, &w, &w, &cx, &cy, &i,
                                  &i, &ui);
                    nx += cx - px;
                    ny += cy - py;
                    px = cx;
                    py = cy;
                    attrib.x = nx;
                    attrib.y = ny;
                    RedrawWindow();
                }
                break;
            case DestroyNotify:
            case UnmapNotify:
                if ((((event.type == UnmapNotify)? event.xunmap.window:
                      event.xdestroywindow.window) == id)) {
                    while (! maprequest_list->empty()) {
                        XPutBackEvent(display, maprequest_list->front());
                        delete maprequest_list->front();
                        maprequest_list->pop_front();
                    }
                    delete maprequest_list;
                    XPutBackEvent(display, &event);
                    XUngrabKeyboard(display, CurrentTime);
                    XUngrabPointer(display, CurrentTime);
                    waimea->eh->move_resize = EndMoveResizeType;
                    dontsend = move_resize = false;
                    return;
                }
                waimea->eh->EvUnmapDestroy(&event);
                break;
            case ConfigureRequest:
                if (event.xconfigurerequest.window != id)
                    waimea->eh->EvConfigureRequest(&event.xconfigurerequest);
                break;
            case MapRequest:
                map_ev = new XEvent;
                *map_ev = event;
                maprequest_list->push_front(map_ev); break;
            case ButtonPress:
            case ButtonRelease:
                event.xbutton.window = id;
            case KeyPress:
            case KeyRelease:
                if (event.type == KeyPress || event.type == KeyRelease)
                    event.xkey.window = id;
                waimea->eh->HandleEvent(&event);
                if (waimea->eh->move_resize != EndMoveResizeType) break;
                if (attrib.x != sx || attrib.y != sy) {

#ifdef RENDER
                    if (wascreen->config.lazy_trans) {
                        render_if_opacity = true;
                        DrawTitlebar();
                        DrawHandlebar();
                        render_if_opacity = false;
                    }
#endif // RENDER

                    SendConfig();
                    net->SetVirtualPos(this);
                }
                while (! maprequest_list->empty()) {
                    XPutBackEvent(display, maprequest_list->front());
                    delete maprequest_list->front();
                    maprequest_list->pop_front();
                }
                delete maprequest_list;
                dontsend = move_resize = false;
                XUngrabKeyboard(display, CurrentTime);
                XUngrabPointer(display, CurrentTime);
                return;
        }
    }
}

/**
 * @fn    Resize(XEvent *e, int how)
 * @brief Resizes the window
 *
 * Resizes the window through displaying a outline of the window while dragging
 * the mouse. If how parameter is RightType when the window is resized in 
 * south-east direction and if how parameter is LeftType the resize is
 * being performed in south-west direction.
 *
 * @param e XEvent causing start of resize
 */
void WaWindow::Resize(XEvent *e, int how) {
    XEvent event, *map_ev;
    int px, py, width, height, n_w, n_h, o_w, o_h, n_x, o_x, i;
    list<XEvent *> *maprequest_list;
    bool started = false;
    Window w;
    unsigned int ui;
    
    XQueryPointer(display, wascreen->id, &w, &w, &px, &py, &i, &i, &ui);

    if (waimea->eh->move_resize != EndMoveResizeType) return;
    n_x    = o_x = attrib.x;
    width  = n_w = o_w = attrib.width;
    height = n_h = o_h = attrib.height;
    waimea->eh->move_resize = ResizeType;
    move_resize = true;

    if (e->type == MapRequest) {
        if (how > 0) n_x = attrib.x = px - attrib.width - border_w * 2;
        else n_x = attrib.x = px;
        attrib.y = py - attrib.height - title_w - border_w * 4;
        CreateOutline();
        DrawOutline(n_x, attrib.y, n_w, n_h);
        started = true;
    }    
    maprequest_list = new list<XEvent *>;
    XGrabServer(display);
    if (validateclient(id)) {
        if (XGrabPointer(display, (mapped && !hidden) ? id: wascreen->id, true,
                         ButtonReleaseMask | ButtonPressMask |
                         PointerMotionMask | EnterWindowMask |
                         LeaveWindowMask, GrabModeAsync, GrabModeAsync,
                         None, (how > 0) ? waimea->resizeright_cursor:
                         waimea->resizeleft_cursor, CurrentTime)
            != GrabSuccess) {
            move_resize = false;
            waimea->eh->move_resize = EndMoveResizeType;
            return;
        }
        if (XGrabKeyboard(display, (mapped && !hidden) ? id: wascreen->id,
                          true, GrabModeAsync, GrabModeAsync, CurrentTime)
            != GrabSuccess) {
            move_resize = false;
            waimea->eh->move_resize = EndMoveResizeType;
            return;
        }
    } else DELETED;
    XUngrabServer(display);
    for (;;) {
        waimea->eh->EventLoop(waimea->eh->moveresize_return_mask, &event);
        switch (event.type) {
            case MotionNotify:
                while (XCheckTypedWindowEvent(display, event.xmotion.window,
                                              MotionNotify, &event));
                width  += (event.xmotion.x_root - px) * how;
                height += event.xmotion.y_root - py;
                px = event.xmotion.x_root;
                py = event.xmotion.y_root;
                if (IncSizeCheck(width, height, &n_w, &n_h)) {
                    if (how == WestType) n_x -= n_w - o_w;
                    if (! started) {
                         CreateOutline();
                         started = true;
                    }
                    o_x = n_x;
                    o_w = n_w;
                    o_h = n_h;
                    DrawOutline(n_x, attrib.y, n_w, n_h);
                }
                break;
            case LeaveNotify:
            case EnterNotify:
                if (wascreen->west->id == event.xcrossing.window ||
                    wascreen->east->id == event.xcrossing.window ||
                    wascreen->north->id == event.xcrossing.window ||
                    wascreen->south->id == event.xcrossing.window) {
                    int old_vx = wascreen->v_x;
                    int old_vy = wascreen->v_y;
                    waimea->eh->HandleEvent(&event);
                    px -= (wascreen->v_x - old_vx);
                    py -= (wascreen->v_y - old_vy);
                    n_x = attrib.x;
                    if (how == WestType) n_x -= n_w - attrib.width;
                    DrawOutline(n_x, attrib.y, n_w, n_h);
                } else if (event.type == LeaveNotify) {
                    int cx, cy;
                    XQueryPointer(display, wascreen->id, &w, &w, &cx, &cy, &i,
                                  &i, &ui);
                    width  += (cx - px) * how;
                    height += cy - py;
                    px = cx;
                    py = cy;
                    if (IncSizeCheck(width, height, &n_w, &n_h)) {
                        if (how == WestType) n_x -= n_w - o_w;
                        if (! started) {
                            CreateOutline();
                            started = true;
                        }
                        o_x = n_x;
                        o_w = n_w;
                        o_h = n_h;
                        DrawOutline(n_x, attrib.y, n_w, n_h);
                    }
                }
                break;
            case DestroyNotify:
            case UnmapNotify:
                if ((((event.type == UnmapNotify)? event.xunmap.window:
                      event.xdestroywindow.window) == id)) {
                    while (! maprequest_list->empty()) {
                        XPutBackEvent(display, maprequest_list->front());
                        delete maprequest_list->front();
                        maprequest_list->pop_front();
                    }
                    delete maprequest_list;
                    XPutBackEvent(display, &event);
                    if (started) DestroyOutline();
                    XUngrabKeyboard(display, CurrentTime);
                    XUngrabPointer(display, CurrentTime);
                    waimea->eh->move_resize = EndMoveResizeType;
                    move_resize = false;
                    return;
                }
                waimea->eh->EvUnmapDestroy(&event);
                break;
            case ConfigureRequest:
                if (event.xconfigurerequest.window != id)
                    waimea->eh->EvConfigureRequest(&event.xconfigurerequest);
                break;
            case MapRequest:
                map_ev = new XEvent;
                *map_ev = event;
                maprequest_list->push_front(map_ev); break;
            case ButtonPress:
            case ButtonRelease:
                event.xbutton.window = id;
            case KeyPress:
            case KeyRelease:
                if (event.type == KeyPress || event.type == KeyRelease)
                    event.xkey.window = id;
                waimea->eh->HandleEvent(&event);
                if (waimea->eh->move_resize != EndMoveResizeType) break;
                if (started) DestroyOutline();
                attrib.width = n_w;
                attrib.height = n_h;
                attrib.x = n_x;
                RedrawWindow();
                while (! maprequest_list->empty()) {
                    XPutBackEvent(display, maprequest_list->front());
                    delete maprequest_list->front();
                    maprequest_list->pop_front();
                }
                delete maprequest_list;
                move_resize = false;
                XUngrabKeyboard(display, CurrentTime);
                XUngrabPointer(display, CurrentTime);
                return;
        }
    }
}

/**
 * @fn    ResizeOpaque(XEvent *e, int how)
 * @brief Resizes the window
 *
 * Resizes the window using the opaque resizing process, which means that 
 * the window is resized after the mouse pointers every motion event. 
 * If how parameter is RightType when the window is resized in 
 * south-east direction and if how parameter is LeftType the resize is
 * being performed in south-west direction.
 *
 * @param e XEvent causing start of resize
 */
void WaWindow::ResizeOpaque(XEvent *e, int how) {
    XEvent event, *map_ev;
    int px, py, width, height, n_w, n_h, i, sw, sh;
    list<XEvent *> *maprequest_list;
    Window w;
    unsigned int ui;
    
    XQueryPointer(display, wascreen->id, &w, &w, &px, &py, &i, &i, &ui);

    if (waimea->eh->move_resize != EndMoveResizeType) return;
    dontsend = true;
    sw = width  = n_w = attrib.width;
    sh = height = n_h = attrib.height;
    waimea->eh->move_resize = ResizeOpaqueType;
    move_resize = true;
    
    if (e->type == MapRequest) {
        if (how > 0) attrib.x = px - attrib.width - border_w * 2;
        else attrib.x = px;
        attrib.y = py - attrib.height - title_w - border_w * 4;
        RedrawWindow();
        net->SetState(this, NormalState);
        net->SetVirtualPos(this);
    }
    
    maprequest_list = new list<XEvent *>;
    XGrabServer(display);
    if (validateclient(id)) {
        if (XGrabPointer(display, (mapped && !hidden) ? id: wascreen->id, true,
                         ButtonReleaseMask | ButtonPressMask |
                         PointerMotionMask | EnterWindowMask |
                         LeaveWindowMask, GrabModeAsync, GrabModeAsync,
                         None, (how > 0) ? waimea->resizeright_cursor:
                         waimea->resizeleft_cursor, CurrentTime)
            != GrabSuccess) {
            move_resize = false;
            waimea->eh->move_resize = EndMoveResizeType;
            return;
        }
        if (XGrabKeyboard(display, (mapped && !hidden) ? id: wascreen->id,
                          true, GrabModeAsync, GrabModeAsync, CurrentTime)
            != GrabSuccess) {
            move_resize = false;
            waimea->eh->move_resize = EndMoveResizeType;
            return;
        }
    } else DELETED;
    XUngrabServer(display);
    for (;;) {
        waimea->eh->EventLoop(waimea->eh->moveresize_return_mask, &event);
        switch (event.type) {
            case MotionNotify:
                while (XCheckTypedWindowEvent(display, event.xmotion.window,
                                              MotionNotify, &event));
                width  += (event.xmotion.x_root - px) * how;
                height += event.xmotion.y_root - py;
                px = event.xmotion.x_root;
                py = event.xmotion.y_root;
                if (IncSizeCheck(width, height, &n_w, &n_h)) {
                    if (how == WestType) attrib.x -= n_w - attrib.width;
                    attrib.width  = n_w;
                    attrib.height = n_h;
                    RedrawWindow();
                }
                break;
            case LeaveNotify:
            case EnterNotify:
                if (wascreen->west->id == event.xcrossing.window ||
                    wascreen->east->id == event.xcrossing.window ||
                    wascreen->north->id == event.xcrossing.window ||
                    wascreen->south->id == event.xcrossing.window) {
                    int old_vx = wascreen->v_x;
                    int old_vy = wascreen->v_y;
                    waimea->eh->HandleEvent(&event);
                    px -= (wascreen->v_x - old_vx);
                    py -= (wascreen->v_y - old_vy);
                } else if (event.type == LeaveNotify) {
                    int cx, cy;
                    XQueryPointer(display, wascreen->id, &w, &w, &cx, &cy, &i,
                                  &i, &ui);
                    width  += (cx - px) * how;
                    height += cy - py;
                    px = cx;
                    py = cy;
                    if (IncSizeCheck(width, height, &n_w, &n_h)) {
                        if (how == WestType)
                            attrib.x -= n_w - attrib.width;
                        attrib.width  = n_w;
                        attrib.height = n_h;
                        RedrawWindow();
                    }
                }
                break;
            case DestroyNotify:
            case UnmapNotify:
                if ((((event.type == UnmapNotify)? event.xunmap.window:
                      event.xdestroywindow.window) == id)) {
                    while (! maprequest_list->empty()) {
                        XPutBackEvent(display, maprequest_list->front());
                        delete maprequest_list->front();
                        maprequest_list->pop_front();
                    }
                    delete maprequest_list;
                    XPutBackEvent(display, &event);
                    XUngrabKeyboard(display, CurrentTime);
                    XUngrabPointer(display, CurrentTime);
                    waimea->eh->move_resize = EndMoveResizeType;
                    dontsend = move_resize = false;
                    return;
                }
                waimea->eh->EvUnmapDestroy(&event);
                break;
            case ConfigureRequest:
                if (event.xconfigurerequest.window != id)
                    waimea->eh->EvConfigureRequest(&event.xconfigurerequest);
                break;
            case MapRequest:
                map_ev = new XEvent;
                *map_ev = event;
                maprequest_list->push_front(map_ev); break;
            case ButtonPress:
            case ButtonRelease:
                event.xbutton.window = id;
            case KeyPress:
            case KeyRelease:
                if (event.type == KeyPress || event.type == KeyRelease)
                    event.xkey.window = id;
                waimea->eh->HandleEvent(&event);
                width = attrib.width;
                height = attrib.height;
                if (waimea->eh->move_resize != EndMoveResizeType) break;
                if (attrib.width != sw || attrib.height != sh) {
                    SendConfig();
                    net->SetVirtualPos(this);
                }
                while (! maprequest_list->empty()) {
                    XPutBackEvent(display, maprequest_list->front());
                    delete maprequest_list->front();
                    maprequest_list->pop_front();
                }
                delete maprequest_list;
                dontsend = move_resize = false;
                XUngrabKeyboard(display, CurrentTime);
                XUngrabPointer(display, CurrentTime);
                return;
        }
    }
}

/**
 * @fn    EndMoveResize(XEvent *e, WaAction *)
 * @brief Ends move/resize
 *
 * Ends moving and resizing process.
 */
void WaWindow::EndMoveResize(XEvent *, WaAction *) {
    waimea->eh->move_resize = EndMoveResizeType;
}

/**
 * @fn    _Maximize(int x, int y)
 * @brief Maximize the window
 *
 * Maximizes the window so that the window with it's decorations fills up
 * the hole workarea.
 *
 * @param x Virtual x pos where window should be maximized, ignored if negative
 * @param y Virtual y pos where window should be maximized, ignored if negative
 */
void WaWindow::_Maximize(int x, int y) {
    int n_w, n_h, new_width, new_height;

    if (flags.max) return;

    int workx, worky, workw, workh;
    wascreen->GetWorkareaSize(&workx, &worky, &workw, &workh);
    
    new_width = workw - (flags.border * border_w * 2);
    new_height = workh - (flags.border * border_w * 2) -
        title_w - handle_w - (border_w * flags.title) -
        (border_w * flags.handle);

    restore_max.width = attrib.width;
    restore_max.height = attrib.height;
    int rest_x = attrib.x;
    int rest_y = attrib.y;
    
    if (flags.shaded) {
        restore_max.height = restore_shade;
        restore_shade = new_height;
        new_height = attrib.height;
    }
    if (IncSizeCheck(new_width, new_height, &n_w, &n_h)) {
        attrib.x = workx;
        attrib.y = worky;
        restore_max.x = rest_x - attrib.x;
        restore_max.y = rest_y - attrib.y;
        if (x >= 0 && y >= 0) {
            attrib.x = (x - wascreen->v_x);
            attrib.y = (y - wascreen->v_y);
            restore_max.misc0 = x;
            restore_max.misc1 = y;
        } else {
            restore_max.misc0 = wascreen->v_x + attrib.x;
            restore_max.misc1 = wascreen->v_y + attrib.y;
        }
        attrib.x += border_w;
        attrib.y += title_w + border_w + (border_w * flags.title);
        attrib.width = n_w;
        attrib.height = n_h;
        RedrawWindow();
        flags.max = true;
        
        if (title_w) {
            list<WaChildWindow *>::iterator bit = buttons.begin();
            for (; bit != buttons.end(); ++bit)
                if ((*bit)->bstyle->cb == MaxCBoxType) (*bit)->Render();
        }
        net->SetWmState(this);
        wascreen->UpdateCheckboxes(MaxCBoxType);
    }
}

/**
 * @fn    UnMaximize(XEvent *, WaAction *)
 * @brief Unmaximize the window
 *
 * Restores size and position of maximized window.
 */
void WaWindow::UnMaximize(XEvent *, WaAction *) {
    int n_w, n_h, rest_height, tmp_shade_height = 0;
    
    if (flags.max) {
        if (flags.shaded) {
            rest_height = attrib.height;
            tmp_shade_height = restore_max.height;
        }
        else rest_height = restore_max.height;
        if (IncSizeCheck(restore_max.width, rest_height, &n_w, &n_h)) {
            attrib.x = restore_max.x + (restore_max.misc0 - wascreen->v_x);
            attrib.y = restore_max.y + (restore_max.misc1 - wascreen->v_y);
            attrib.width = n_w;
            attrib.height = n_h;
            flags.max = false;
            RedrawWindow();
            if (flags.shaded) restore_shade = tmp_shade_height;
            if (title_w) {
                list<WaChildWindow *>::iterator bit = buttons.begin();
                for (; bit != buttons.end(); ++bit)
                    if ((*bit)->bstyle->cb == MaxCBoxType) (*bit)->Render();
            }
            net->SetWmState(this);
            wascreen->UpdateCheckboxes(MaxCBoxType);
        }
    }
}

/**
 * @fn    ToggleMaximize(XEvent *, WaAction *)
 * @brief Maximizes or unmaximize window
 *
 * If window isn't maximized this function maximizes it and if it is already
 * maximized then function will unmaximized window.
 */
void WaWindow::ToggleMaximize(XEvent *, WaAction *) {
    if (! flags.max) Maximize(NULL, NULL);
    else UnMaximize(NULL, NULL);
}

/**
 * @fn    Close(XEvent *, WaAction *)
 * @brief Close the window
 *
 * Sends a delete message to the client window. A normal running X window
 * should accept this event and destroy itself. 
 */
void WaWindow::Close(XEvent *, WaAction *) {
    XEvent ev;

    ev.type = ClientMessage;
    ev.xclient.window = id;
    ev.xclient.message_type = XInternAtom(display, "WM_PROTOCOLS", false);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = XInternAtom(display, "WM_DELETE_WINDOW", false);
    ev.xclient.data.l[1] = CurrentTime;

    XGrabServer(display);
    if (validateclient(id))
        XSendEvent(display, id, false, NoEventMask, &ev);
    else DELETED;
    XUngrabServer(display);

}

/**
 * @fn    Kill(XEvent *, WaAction *)
 * @brief Kill the window
 *
 * Tells the the X server to remove the window from the screen through
 * killing the process that created it.
 */
void WaWindow::Kill(XEvent *, WaAction *) {
    XGrabServer(display);
    if (validateclient(id))
        XKillClient(display, id);
    else DELETED;
    XUngrabServer(display);
}

/**
 * @fn    CloseKill(XEvent *e, WaAction *ac)
 * @brief Close/Kill the window
 *
 * Checks if the window will accept a delete message. If it will, then we
 * use that method for closing the window otherwise we use the kill method.
 *
 * @param e XEvent causing close/kill of window
 * @param ac WaAction object
 */
void WaWindow::CloseKill(XEvent *e, WaAction *ac) {
    int i, n;
    bool close = false;
    Atom *protocols;
    Atom del_atom = XInternAtom(display, "WM_DELETE_WINDOW", false);

    XGrabServer(display);
    if (validateclient(id)) {
        if (XGetWMProtocols(display, id, &protocols, &n)) {
            for (i = 0; i < n; i++) if (protocols[i] == del_atom) close = true;
            XFree(protocols);
        }
    } else DELETED;
    XUngrabServer(display);
    if (close) Close(e, ac);
    else Kill(e, ac);
}

/**
 * @fn    MenuMap(XEvent *e, WaAction *ac)
 * @brief Maps a menu
 *
 * Links the window to the menu and maps it at the position of the button
 * event causing mapping. If menu is already mapped nothing is done.
 *
 * @param ac WaAction object
 * @param bool True if we should focus first item in menu
 */
void WaWindow::MenuMap(XEvent *, WaAction *ac, bool focus) {
    Window w;
    int i, x, y, exp;
    unsigned int ui;
    WaMenu *menu = wascreen->GetMenuNamed(ac->param);

    if (! menu) return;
    if (waimea->eh->move_resize != EndMoveResizeType) return;

    int workx, worky, workw, workh;
    wascreen->GetWorkareaSize(&workx, &worky, &workw, &workh);
    
    if (XQueryPointer(display, wascreen->id, &w, &w, &x, &y, &i, &i, &ui)) {
        if (menu->tasksw) menu->Build(wascreen);
        menu->wf = id;
        menu->ftype = MenuWFuncMask;
        list<WaMenuItem *>::iterator it = menu->item_list.begin();
        for (exp = 0; it != menu->item_list.end(); ++it)
            exp += (*it)->ExpandAll(this);
        if (exp) menu->Build(wascreen);
        if ((y + menu->height + wascreen->mstyle.border_width * 2) > 
            (unsigned int) (workh + worky))
            y -= (menu->height + wascreen->mstyle.border_width * 2);
        if ((x + menu->width + wascreen->mstyle.border_width * 2) > 
            (unsigned int) (workw + workx))
            x -= (menu->width + wascreen->mstyle.border_width * 2);
        menu->Map(x, y);
        if (focus) menu->FocusFirst();
    }
}

/**
 * @fn    MenuRemap(XEvent *e)
 * @brief Remaps a menu
 *
 * Links the window to the menu and maps it at the position of the button
 * event causing mapping. If the window is already mapped then we just move
 * it to the new position.
 *
 * @param ac WaAction object
 * @param bool True if we should focus first item in menu
 */
void WaWindow::MenuRemap(XEvent *, WaAction *ac, bool focus) {
    Window w;
    int i, x, y, exp;
    unsigned int ui;
    WaMenu *menu = wascreen->GetMenuNamed(ac->param);

    if (! menu) return;
    if (menu->dynamic && menu->mapped) {
        menu->Unmap(menu->has_focus);
        if (! (menu = wascreen->CreateDynamicMenu(ac->param))) return;
    }
    if (waimea->eh->move_resize != EndMoveResizeType) return;

    int workx, worky, workw, workh;
    wascreen->GetWorkareaSize(&workx, &worky, &workw, &workh);
    
    if (XQueryPointer(display, wascreen->id, &w, &w, &x, &y, &i, &i, &ui)) {
        if (menu->tasksw) menu->Build(wascreen);
        menu->wf = id;
        menu->ftype = MenuWFuncMask;
        list<WaMenuItem *>::iterator it = menu->item_list.begin();
        for (exp = 0; it != menu->item_list.end(); ++it)
            exp += (*it)->ExpandAll(this);
        if (exp) menu->Build(wascreen);
        if ((y + menu->height + wascreen->mstyle.border_width * 2) > 
            (unsigned int) (workh + worky))
            y -= (menu->height + wascreen->mstyle.border_width * 2);
        if ((x + menu->width + wascreen->mstyle.border_width * 2) > 
            (unsigned int) (workw + workx))
            x -= (menu->width + wascreen->mstyle.border_width * 2);
        menu->ignore = true;
        menu->ReMap(x, y);
        menu->ignore = false;
        if (focus) menu->FocusFirst();
    }
}

/**
 * @fn    MenuUnmap(XEvent *, WaAction *ac, bool focus)
 * @brief Unmaps a menu
 *
 * Unmaps a menu and all its submenus.
 *
 * @param ac WaAction object
 * @param bool True if we should focus root item
 */
void WaWindow::MenuUnmap(XEvent *, WaAction *ac, bool focus) {
    WaMenu *menu = wascreen->GetMenuNamed(ac->param);
    
    if (! menu) return;
    if (waimea->eh->move_resize != EndMoveResizeType) return;
    
    menu->Unmap(focus);
    menu->UnmapSubmenus(focus);
}

/**
 * @fn    Shade(XEvent *, WaAction *)
 * @brief Shade window
 *
 * Resizes window to so that only the titlebar is shown. Remembers previous
 * height of window that will be restored when unshading.
 */
void WaWindow::Shade(XEvent *, WaAction *) {
    int n_w, n_h;
    
    if (IncSizeCheck(attrib.width, -(handle_w + border_w * 2), &n_w, &n_h)) {
        attrib.width = n_w;
        attrib.height = n_h;
        RedrawWindow();
        net->SetWmState(this);
        if (title_w) {
            list<WaChildWindow *>::iterator bit = buttons.begin();
            for (; bit != buttons.end(); ++bit)
                if ((*bit)->bstyle->cb == ShadeCBoxType) (*bit)->Render();
        }
        wascreen->UpdateCheckboxes(ShadeCBoxType);
    }
}

/**
 * @fn    UnShade(XEvent *, WaAction *)
 * @brief Unshade window
 *
 * Restores height of shaded window.
 */
void WaWindow::UnShade(XEvent *, WaAction *) {
    if (flags.shaded) {
        attrib.height = restore_shade;
        RedrawWindow();
        flags.shaded = false;
        net->SetWmState(this);
        if (title_w) {
            list<WaChildWindow *>::iterator bit = buttons.begin();
            for (; bit != buttons.end(); ++bit)
                if ((*bit)->bstyle->cb == ShadeCBoxType) (*bit)->Render();
        }
        wascreen->UpdateCheckboxes(ShadeCBoxType);
    }
}

/**
 * @fn    ToggleShade(XEvent *, WaAction *)
 * @brief Shades window or unshades it
 *
 * If window isn't shaded this function will shade it and if it is already
 * shaded function unshades it.
 */
void WaWindow::ToggleShade(XEvent *, WaAction *) {
    if (flags.shaded) UnShade(NULL, NULL);
    else Shade(NULL, NULL);
}

/**
 * @fn    Sticky(XEvent *, WaAction *)
 * @brief Makes window sticky
 *
 * Sets the sticky flag to true. This makes viewport moving functions
 * to ignore this window.
 */
void WaWindow::Sticky(XEvent *, WaAction *) {
    flags.sticky = true;
    net->SetWmState(this);
    if (title_w) {
        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit)
            if ((*bit)->bstyle->cb == StickCBoxType) (*bit)->Render();
    }
    wascreen->UpdateCheckboxes(StickCBoxType);
}

/**
 * @fn    UnSticky(XEvent *, WaAction *)
 * @brief Makes window normal
 *
 * Sets the sticky flag to false. This makes viewport moving functions
 * treat this window as a normal window.
 */
void WaWindow::UnSticky(XEvent *, WaAction *) {
    flags.sticky = false;
    net->SetWmState(this);
    if (title_w) {
        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit)
            if ((*bit)->bstyle->cb == StickCBoxType) (*bit)->Render();
    }
    wascreen->UpdateCheckboxes(StickCBoxType);
}

/**
 * @fn    ToggleSticky(XEvent *, WaAction *)
 * @brief Toggles sticky flag
 *
 * Inverts the sticky flag.
 */
void WaWindow::ToggleSticky(XEvent *, WaAction *) {
    flags.sticky = !flags.sticky;
    net->SetWmState(this);
    if (title_w) {
        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit)
            if ((*bit)->bstyle->cb == StickCBoxType) (*bit)->Render();
    }
    wascreen->UpdateCheckboxes(StickCBoxType);
}

/**
 * @fn    TaskSwitcher(XEvent *, WaAction *)
 * @brief Maps task switcher menu
 *
 * Maps task switcher menu at middle of screen and sets input focus to
 * first window in list.
 */
void WaWindow::TaskSwitcher(XEvent *, WaAction *) {
    if (waimea->eh->move_resize != EndMoveResizeType) return;

    int workx, worky, workw, workh;
    wascreen->GetWorkareaSize(&workx, &worky, &workw, &workh);

    wascreen->window_menu->Build(wascreen);
    wascreen->window_menu->ReMap(workx + (workw / 2 -
                                          wascreen->window_menu->width / 2),
                                 worky + (workh / 2 -
                                          wascreen->window_menu->height / 2));
    wascreen->window_menu->FocusFirst();
}

/**
 * @fn    PreviousTask(XEvent *, WaAction *)
 * @brief Switches to previous task
 *
 * Switches to the previous focused window.
 */
void WaWindow::PreviousTask(XEvent *, WaAction *) {
    if (waimea->eh->move_resize != EndMoveResizeType) return;
    list<WaWindow *>::iterator it = wascreen->wawindow_list.begin();
    it++;
    (*it)->Raise(NULL, NULL);
    (*it)->FocusVis(NULL, NULL);
}

/**
 * @fn    NextTask(XEvent *, WaAction *)
 * @brief Switches to next task
 *
 * Switches to the window that haven't had focus for longest time.
 */
void WaWindow::NextTask(XEvent *, WaAction *) {
    if (waimea->eh->move_resize != EndMoveResizeType) return;
    wascreen->wawindow_list.back()->Raise(NULL, NULL);
    wascreen->wawindow_list.back()->FocusVis(NULL, NULL);
}

/**
 * @fn    DecorTitleOn(XEvent *, WaAction *)
 * @brief Turn on title decoration
 *
 * Turn on titlebar decorations for the window.
 */
void WaWindow::DecorTitleOn(XEvent *, WaAction *) {
    if (flags.title) return;
    
    flags.title = true;
    flags.all = flags.title && flags.handle && flags.border;
    UpdateAllAttributes();
    MapWindow();
    net->SetWmState(this);
    if (title_w) {
        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit)
            if ((*bit)->bstyle->cb == TitleCBoxType ||
                (flags.all && (*bit)->bstyle->cb == AllCBoxType))
                (*bit)->Render();
    }
    wascreen->UpdateCheckboxes(TitleCBoxType);
    if (flags.all) wascreen->UpdateCheckboxes(AllCBoxType);
}

/**
 * @fn    DecorHandleOn(XEvent *, WaAction *)
 * @brief Turn on handle decoration
 *
 * Turn on handlebar decorations for the window.
 */
void WaWindow::DecorHandleOn(XEvent *, WaAction *) {
    if (flags.handle) return;
    
    flags.handle = true;
    flags.all = flags.title && flags.handle && flags.border;
    UpdateAllAttributes();
    MapWindow();
    net->SetWmState(this);
    if (title_w) {
        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit)
            if ((*bit)->bstyle->cb == TitleCBoxType ||
                (flags.all && (*bit)->bstyle->cb == AllCBoxType))
                (*bit)->Render();
    }
    wascreen->UpdateCheckboxes(HandleCBoxType);
    if (flags.all) wascreen->UpdateCheckboxes(AllCBoxType);
}

/**
 * @fn    DecorBorderOn(XEvent *, WaAction *)
 * @brief Turn on border decoration
 *
 * Turn on border decorations for the window.
 */
void WaWindow::DecorBorderOn(XEvent *, WaAction *) {
    if (flags.border) return;
    
    flags.border = true;
    flags.all = flags.title && flags.handle && flags.border;
    UpdateAllAttributes();
    MapWindow();
    net->SetWmState(this);
    if (title_w) {
        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit)
            if ((*bit)->bstyle->cb == BorderCBoxType ||
                (flags.all && (*bit)->bstyle->cb == AllCBoxType))
                (*bit)->Render();
    }
    wascreen->UpdateCheckboxes(BorderCBoxType);
    if (flags.all) wascreen->UpdateCheckboxes(AllCBoxType);
}

/**
 * @fn    DecorAllOn(XEvent *, WaAction *)
 * @brief Turn on all decorations
 *
 * Turn on all decorations for the window.
 */
void WaWindow::DecorAllOn(XEvent *, WaAction *) {
    if (flags.all) return;

    flags.all = true;
    flags.border = true;
    flags.title = true;
    flags.handle = true;
    UpdateAllAttributes();
    MapWindow();
    net->SetWmState(this);
    if (title_w) {
        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit)
            if ((*bit)->bstyle->cb == TitleCBoxType ||
                (*bit)->bstyle->cb == HandleCBoxType ||
                (*bit)->bstyle->cb == BorderCBoxType ||
                (*bit)->bstyle->cb == AllCBoxType)
                (*bit)->Render();
    }
    wascreen->UpdateCheckboxes(TitleCBoxType);
    wascreen->UpdateCheckboxes(HandleCBoxType);
    wascreen->UpdateCheckboxes(BorderCBoxType);
    wascreen->UpdateCheckboxes(AllCBoxType);
}

/**
 * @fn    DecorTitleOff(XEvent *, WaAction *)
 * @brief Turn off title decoration
 *
 * Turn off title decorations for the window.
 */
void WaWindow::DecorTitleOff(XEvent *, WaAction *) {
    if (flags.shaded || ! flags.title) return;
    
    flags.title = false;
    flags.all = false;
    UpdateAllAttributes();
    MapWindow();
    net->SetWmState(this);
    if (title_w) {
        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit)
            if ((*bit)->bstyle->cb == TitleCBoxType ||
                (flags.all && (*bit)->bstyle->cb == AllCBoxType))
                (*bit)->Render();
    }
    wascreen->UpdateCheckboxes(TitleCBoxType);
    wascreen->UpdateCheckboxes(AllCBoxType);
}

/**
 * @fn    DecorHandleOff(XEvent *, WaAction *)
 * @brief Turn off hendle decoration
 *
 * Turn off handlebar decorations for the window.
 */
void WaWindow::DecorHandleOff(XEvent *, WaAction *) {
    if (! flags.handle) return;
    
    flags.handle = false;
    flags.all = false;
    UpdateAllAttributes();
    MapWindow();
    net->SetWmState(this);
    if (title_w) {
        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit)
            if ((*bit)->bstyle->cb == HandleCBoxType ||
                (flags.all && (*bit)->bstyle->cb == AllCBoxType))
                (*bit)->Render();
    }
    wascreen->UpdateCheckboxes(HandleCBoxType);
    wascreen->UpdateCheckboxes(AllCBoxType);
}

/**
 * @fn    DecorBorderOff(XEvent *, WaAction *)
 * @brief Turn off border decoration
 *
 * Turn off border decorations for the window.
 */
void WaWindow::DecorBorderOff(XEvent *, WaAction *) {
    if (! flags.border) return;
    
    flags.border = false;
    flags.all = false;
    UpdateAllAttributes();
    MapWindow();
    net->SetWmState(this);
    if (title_w) {
        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit)
            if ((*bit)->bstyle->cb == BorderCBoxType ||
                (flags.all && (*bit)->bstyle->cb == AllCBoxType))
                (*bit)->Render();
    }
    wascreen->UpdateCheckboxes(BorderCBoxType);
    wascreen->UpdateCheckboxes(AllCBoxType);
}

/**
 * @fn    DecorAllOff(XEvent *, WaAction *)
 * @brief Turn off all decorations
 *
 * Turn off all decorations for the window.
 */
void WaWindow::DecorAllOff(XEvent *, WaAction *) {
    if (flags.shaded || ! flags.all) return;

    flags.all = false;
    flags.border = false;
    flags.title = false;
    flags.handle = false;
    UpdateAllAttributes();
    MapWindow();
    net->SetWmState(this);
    if (title_w) {
        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit)
            if ((*bit)->bstyle->cb == TitleCBoxType ||
                (*bit)->bstyle->cb == HandleCBoxType ||
                (*bit)->bstyle->cb == BorderCBoxType ||
                (*bit)->bstyle->cb == AllCBoxType)
                (*bit)->Render();
    }
    wascreen->UpdateCheckboxes(TitleCBoxType);
    wascreen->UpdateCheckboxes(HandleCBoxType);
    wascreen->UpdateCheckboxes(BorderCBoxType);
    wascreen->UpdateCheckboxes(AllCBoxType);
}

/**
 * @fn    DecorTitleToggle(XEvent *, WaAction *)
 * @brief Toggle title decoration
 *
 * Toggle titlebar decorations for the window.
 */
void WaWindow::DecorTitleToggle(XEvent *, WaAction *) {
    if (flags.title) DecorTitleOff(NULL, NULL);
    else DecorTitleOn(NULL, NULL);
}

/**
 * @fn    DecorHandleToggle(XEvent *, WaAction *)
 * @brief Toggle handle decoration
 *
 * Toggle handlebar decorations for the window.
 */
void WaWindow::DecorHandleToggle(XEvent *, WaAction *) {
    if (flags.handle) DecorHandleOff(NULL, NULL);
    else DecorHandleOn(NULL, NULL);
}

/**
 * @fn    DecorBorderToggle(XEvent *, WaAction *)
 * @brief Toggle border decoration
 *
 * Toggle border decorations for the window.
 */
void WaWindow::DecorBorderToggle(XEvent *, WaAction *) {
    if (flags.border) DecorBorderOff(NULL, NULL);
    else DecorBorderOn(NULL, NULL);
}

/**
 * @fn    AlwaysontopOn(XEvent *, WaAction *)
 * @brief Make window always on top
 *
 * Add window to list of always on top windows and update always on top
 * windows.
 */
void WaWindow::AlwaysontopOn(XEvent *, WaAction *) {
    if (flags.forcedatbottom) return;
    flags.alwaysontop = true;
    flags.alwaysatbottom = false;
    wascreen->wa_list_stacking.remove(this);
    wascreen->wawindow_list_stacking_aab.remove(this);
    wascreen->wawindow_list_stacking_aot.push_back(this);
    wascreen->WaRaiseWindow(0);
    net->SetWmState(this);
    if (title_w) {
        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit)
            if ((*bit)->bstyle->cb == AOTCBoxType ||
                (*bit)->bstyle->cb == AABCBoxType)
                (*bit)->Render();
    }
    wascreen->UpdateCheckboxes(AOTCBoxType);
    wascreen->UpdateCheckboxes(AABCBoxType);
    net->SetClientListStacking(wascreen);
}

/**
 * @fn    AlwaysatbottomOn(XEvent *, WaAction *)
 * @brief Make window always at bottom
 *
 * Add window to list of always at bottom windows and update always at bottom
 * windows.
 */
void WaWindow::AlwaysatbottomOn(XEvent *, WaAction *) {
    if (flags.forcedatbottom) return;
    flags.alwaysontop = false;
    flags.alwaysatbottom = true;
    wascreen->wa_list_stacking.remove(this);
    wascreen->wawindow_list_stacking_aot.remove(this);
    wascreen->wawindow_list_stacking_aab.push_back(this);
    wascreen->WaLowerWindow(frame->id);
    net->SetWmState(this);
    if (title_w) {
        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit)
            if ((*bit)->bstyle->cb == AOTCBoxType ||
                (*bit)->bstyle->cb == AABCBoxType)
                (*bit)->Render();
    }
    wascreen->UpdateCheckboxes(AOTCBoxType);
    wascreen->UpdateCheckboxes(AABCBoxType);
    net->SetClientListStacking(wascreen);
}

/**
 * @fn    AlwaysontopOff(XEvent *, WaAction *)
 * @brief Make window not always on top
 *
 * Removes window from list of always on top windows.
 */
void WaWindow::AlwaysontopOff(XEvent *, WaAction *) {
    if (flags.forcedatbottom) return;
    flags.alwaysontop = false;
    wascreen->wawindow_list_stacking_aot.remove(this);
    wascreen->wa_list_stacking.push_front(this);
    wascreen->WaRaiseWindow(0);
    net->SetWmState(this);
    if (title_w) {
        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit)
            if ((*bit)->bstyle->cb == AOTCBoxType)
                (*bit)->Render();
    }
    wascreen->UpdateCheckboxes(AOTCBoxType);
    net->SetClientListStacking(wascreen);
}

/**
 * @fn    AlwaysatbottomOff(XEvent *, WaAction *)
 * @brief Make window not always at bottom
 *
 * Removes window from list of always at bottom windows.
 */
void WaWindow::AlwaysatbottomOff(XEvent *, WaAction *) {
    if (flags.forcedatbottom) return;
    flags.alwaysatbottom = false;
    wascreen->wawindow_list_stacking_aab.remove(this);
    wascreen->wa_list_stacking.push_back(this);
    wascreen->WaLowerWindow(frame->id);
    net->SetWmState(this);
    if (title_w) {
        list<WaChildWindow *>::iterator bit = buttons.begin();
        for (; bit != buttons.end(); ++bit)
            if ((*bit)->bstyle->cb == AABCBoxType)
                (*bit)->Render();
    }
    wascreen->UpdateCheckboxes(AABCBoxType);
    net->SetClientListStacking(wascreen);
}

/**
 * @fn    AlwaysontopToggle(XEvent *, WaAction *)
 * @brief Toggle always on top flag
 *
 * If window is always on top we removed it from always on top list, or if
 * window isn't always on top we add it to always on top list.
 */
void WaWindow::AlwaysontopToggle(XEvent *, WaAction *) {
    if (flags.alwaysontop) AlwaysontopOff(NULL, NULL);
    else AlwaysontopOn(NULL, NULL);
}

/**
 * @fn    AlwaysatbottomToggle(XEvent *, WaAction *)
 * @brief Toggle always at bottom flag
 *
 * If window is always at bottom we removed it from always at bottom list,
 * or if window isn't always at bottom we add it to always at bottom list.
 */
void WaWindow::AlwaysatbottomToggle(XEvent *, WaAction *) {
    if (flags.alwaysatbottom) AlwaysatbottomOff(NULL, NULL);
    else AlwaysatbottomOn(NULL, NULL);
}


/**
 * @fn    AcceptConfigRequestOn(XEvent *, WaAction *)
 * @brief Accept ConfigureRequests from this window
 *
 * Sets flag so the configure request events from this window are handled as
 * usual.
 */
void WaWindow::AcceptConfigRequestOn(XEvent *, WaAction *) {
    ign_config_req = false;
}

/**
 * @fn    AcceptConfigRequestOff(XEvent *, WaAction *)
 * @brief Don't accept ConfigureRequests from this window
 *
 * Sets flag so the configure request events from this window are ignored.
 */
void WaWindow::AcceptConfigRequestOff(XEvent *, WaAction *) {
    ign_config_req = true;
}

/**
 * @fn    AcceptConfigRequestToggle(XEvent *, WaAction *)
 * @brief Toggle ign_config_req flag
 *
 * Toggles configure request ignore flag.
 */
void WaWindow::AcceptConfigRequestToggle(XEvent *, WaAction *) {
    ign_config_req = !ign_config_req;
}

/**
 * @fn    MoveResize(XEvent *e, WaAction *ac)
 * @brief Moves and resizes window
 *
 * X geomtry string as WaAction parameter. Moves and resizes window to
 * the size and position in the actual screen area specified by X geometry
 * string.
 *
 * @param e XEvent causing function call
 * @param ac WaAction object
 */
void WaWindow::MoveResize(XEvent *e, WaAction *ac) {
    int x, y, geometry;
    unsigned int width, height;
    
    if (waimea->eh->move_resize != EndMoveResizeType || ! ac->param) return;
    width = attrib.width;
    height = attrib.height;

    geometry = XParseGeometry(ac->param, &x, &y, &width, &height);
    IncSizeCheck(width, height, &attrib.width, &attrib.height);
    
    if (geometry & XValue) {
        if (geometry & XNegative)
            attrib.x = wascreen->width + x - attrib.width;
        else attrib.x = x;
    }
    if (geometry & YValue) {
        if (geometry & YNegative)
            attrib.y = wascreen->height + y - attrib.height;
        else attrib.y = y;
    }

    RedrawWindow();
}

/**
 * @fn    MoveResizeVirtual(XEvent *e, WaAction *ac)
 * @brief Moves and resizes window
 *
 * X geomtry string as WaAction parameter. Moves and resizes window to
 * the size and position in the virtual screen area specified by X geometry
 * string.
 *
 * @param e XEvent causing function call
 * @param ac WaAction object
 */
void WaWindow::MoveResizeVirtual(XEvent *e, WaAction *ac) {
    int x, y, geometry;
    unsigned int width, height;
    
    if (waimea->eh->move_resize != EndMoveResizeType || ! ac->param) return;
    width = attrib.width;
    height = attrib.height;

    geometry = XParseGeometry(ac->param, &x, &y, &width, &height);
    IncSizeCheck(width, height, &attrib.width, &attrib.height);
    
    if (geometry & XValue) {
        if (geometry & XNegative)
            attrib.x = ((wascreen->v_xmax + wascreen->width) +
                        x - attrib.width) - wascreen->v_x;
        else attrib.x = x - wascreen->v_x;
    }
    if (geometry & YValue) {
        if (geometry & YNegative)
            attrib.y = ((wascreen->v_ymax + wascreen->height) +
                        y - attrib.height) - wascreen->v_y;
        else attrib.y = y - wascreen->v_y;
    }

    RedrawWindow();
}

/**
 * @fn    MoveWindowToPointer(XEvent *e, WaAction *)
 * @brief Moves window to pointer position
 *
 * Moves window to mouse pointer position and makes sure that it isn't
 * moved outside screen.
 *
 * @param e XEvent causing function call
 */
void WaWindow::MoveWindowToPointer(XEvent *e, WaAction *) {
    int total_h = border_w * 2;
    if (title_w) total_h += border_w;
    if (handle_w) total_h += border_w;
    total_h += attrib.height;

    attrib.x = e->xbutton.x_root - attrib.width / 2;
    attrib.y = e->xbutton.y_root - attrib.height / 2;
    
    if (attrib.x + border_w * 2 + attrib.width > wascreen->width)
        attrib.x = wascreen->width - attrib.width - border_w;
    else if (attrib.x < 0) attrib.x = border_w;
    
    if (attrib.y + total_h > wascreen->height)
        attrib.y = wascreen->height - handle_w - border_w - attrib.height -
            ((handle_w)? border_w: 0);
    else if (attrib.y < 0)
        attrib.y = title_w + border_w + ((title_w)? border_w: 0);

    RedrawWindow();
}

/**
 * @fn    MoveWindowToSmartPlace(XEvent *, WaAction *)
 * @brief Moves window to smart position
 *
 * Moves window using Smart Placement algorithm.
 */
void WaWindow::MoveWindowToSmartPlace(XEvent *, WaAction *) {
    int temp_h, temp_w;
    Gravitate(RemoveGravity);
    int workx, worky, workw, workh;
    wascreen->GetWorkareaSize(&workx, &worky, &workw, &workh);
    int test_x = attrib.x - workx;
    int test_y = attrib.y - worky - 1;
    int loc_ok = False, tw, tx, ty, th;
    int bw = flags.border * border_w;
    int titleh = title_w + flags.title * bw;
    int handleh = handle_w + flags.handle * bw;
    temp_h = attrib.height + bw * 2 + titleh + handleh;
    temp_w = attrib.width + bw * 2;

    while (((test_y + temp_h) < workh) && !loc_ok) {
        test_x = 0;
        while (((test_x + temp_w) < workw) && !loc_ok) {
            loc_ok = True;
            list<WaWindow *>::iterator it = wascreen->wawindow_list.begin();
            for (; it != wascreen->wawindow_list.end() &&
                     (loc_ok == True); it++) {
                if ((*it != this) && ((*it)->flags.tasklist) &&
                    ((*it)->desktop_mask &
                     (1L << wascreen->current_desktop->number)) &&
                    ((((*it)->attrib.x + (*it)->attrib.width) > 0 &&
                      (*it)->attrib.x < workw) && 
                     (((*it)->attrib.y + (*it)->attrib.height) > 0 &&
                      (*it)->attrib.y < workh))) {
                    bw = (*it)->flags.border * (*it)->border_w;
                    titleh = (*it)->title_w + (*it)->flags.title * bw;
                    handleh = (*it)->handle_w + (*it)->flags.handle * bw;
                    
                    th = (*it)->attrib.height + bw * 2 + titleh + handleh;
                    tw = (*it)->attrib.width + bw * 2;

                    (*it)->Gravitate(RemoveGravity);
                    tx = (*it)->attrib.x - workx - 1;
                    ty = (*it)->attrib.y - worky - 1;
                    (*it)->Gravitate(ApplyGravity);

                    if ((tx < (test_x + temp_w)) &&
                        ((tx + tw) > test_x) &&
                        (ty < (test_y + temp_h)) &&
                        ((ty + th) > test_y)) {
                        loc_ok = False;
                        test_x = tx + tw;
                    }
                }
            }
            test_x += 1;
        }
        test_y += 1;
    }
    if (loc_ok != False) {
        attrib.x = test_x + workx - 1;
        attrib.y = test_y + worky;
        Gravitate(ApplyGravity);
        RedrawWindow();
    }
    else
        Gravitate(ApplyGravity);
}

/**
 * @fn    DesktopMask(XEvent *, WaAction *ac)
 * @brief Set desktop mask
 *
 * Sets the mask for in which desktops the window should be a member.
 *
 * @param ac WaAction object
 */
void WaWindow::DesktopMask(XEvent *, WaAction *ac) {
    if (ac->param) {
        if (! strncasecmp("all", ac->param, 3))
            desktop_mask = (1L << 16) - 1;
        else {
            desktop_mask = 0;
            char *token = strtok(ac->param, " \t");
            while (token) {
                unsigned int desk = (unsigned int) atoi(token);
                if (desk < wascreen->config.desktops)
                    desktop_mask |= (1L << desk);
                token = strtok(NULL, " \t");
            }
        }
        if (desktop_mask == 0) desktop_mask = (1L << 0);
        
        if (desktop_mask & (1L << wascreen->current_desktop->number))
            Show();
        else
            Hide();

        net->SetDesktop(this);
        net->SetDesktopMask(this);
    }
}

/**
 * @fn    JoinDesktop(XEvent *, WaAction *ac)
 * @brief Join desktop
 *
 * Join window to desktop specified by action parameter.
 *
 * @param ac WaAction object
 */
void WaWindow::JoinDesktop(XEvent *, WaAction *ac) {
    if (ac->param) {
        unsigned int desk = (unsigned int) atoi(ac->param);
        if (desk < wascreen->config.desktops) {
            desktop_mask |= (1L << desk);
            if (desktop_mask & (1L << wascreen->current_desktop->number))
                Show();
            net->SetDesktop(this);
            net->SetDesktopMask(this);
        }
    }
}

/**
 * @fn    PartDesktop(XEvent *, WaAction *ac)
 * @brief Part desktop
 *
 * Part window from desktop specified by action parameter.
 *
 * @param ac WaAction object
 */
void WaWindow::PartDesktop(XEvent *, WaAction *ac) {
    if (ac->param) {
        unsigned int desk = (unsigned int) atoi(ac->param);
        if (desk < wascreen->config.desktops) {
            long int new_mask = desktop_mask & ~(1L << desk);
            if (new_mask) {
                desktop_mask = new_mask;
                if (! (desktop_mask &
                       (1L << wascreen->current_desktop->number))) 
                    Hide();
                net->SetDesktop(this);
                net->SetDesktopMask(this);
            }
        }
    }
}

/**
 * @fn    PartCurrentDesktop(XEvent *, WaAction *)
 * @brief Part current desktop
 *
 * Part window from current desktop if window is part of another desktop.
 */
void WaWindow::PartCurrentDesktop(XEvent *, WaAction *) {
    long int new_mask = desktop_mask &
        ~(1L << wascreen->current_desktop->number);
        if (new_mask) {
            desktop_mask = new_mask;
            Hide();
            net->SetDesktop(this);
            net->SetDesktopMask(this);
        }
}

/**
 * @fn    JoinAllDesktops(XEvent *, WaAction *)
 * @brief Join all desktops
 *
 * Join window to all desktops.
 */
void WaWindow::JoinAllDesktops(XEvent *, WaAction *) {
    desktop_mask = (1L << 16) - 1;
    Show();
    net->SetDesktop(this);
    net->SetDesktopMask(this);
}

/**
 * @fn    PartAllDesktopsExceptCurrent(XEvent *, WaAction *)
 * @brief Part all desktops
 *
 * Part window from all desktops except current desktop.
 */
void WaWindow::PartAllDesktopsExceptCurrent(XEvent *, WaAction *) {
    desktop_mask = (1L << wascreen->current_desktop->number);
    Show();
    net->SetDesktop(this);
    net->SetDesktopMask(this);
}

/**
 * @fn    PartCurrentJoinDesktop(XEvent *, WaAction *ac)
 * @brief Part current desktop and join desktop
 *
 * Part current desktop and join desktop specified by parameter.
 *
 * @param ac WaAction object
 */
void WaWindow::PartCurrentJoinDesktop(XEvent *, WaAction *ac) {
    if (ac->param) {
        unsigned int desk = (unsigned int) atoi(ac->param);
        if (desk < wascreen->config.desktops) {
            desktop_mask = desktop_mask &
                ~(1L << wascreen->current_desktop->number);
            desktop_mask |= (1L << desk);
            if (desktop_mask & (1L << wascreen->current_desktop->number))
                Show();
            else Hide();
            net->SetDesktop(this);
            net->SetDesktopMask(this);
        }
    }
}

/**
 * @fn    EvAct(XEvent *e, EventDetail *ed, list<WaAction *> *acts,
 *              int etype)
 * @brief Calls WaWindow function
 *
 * Tries to match an occurred X event with the actions in an action list.
 * If we have a match then we execute that action.
 *
 * @param e X event that have occurred
 * @param ed Event details
 * @param acts List with actions to match event with
 * @param etype Type of window event occurred on
 */
void WaWindow::EvAct(XEvent *e, EventDetail *ed, list<WaAction *> *acts,
                     int etype) {
    XEvent fev;
    bool replay = false, wait_release = false, match = false;
    
    list<WaAction *>::iterator it = acts->begin();
    if (waimea->eh->move_resize != EndMoveResizeType)
        ed->mod |= MoveResizeMask;
    else if (etype == WindowType) {
        if (ed->type == ButtonPress) {
            for (; it != acts->end(); ++it) {
                if ((*it)->type == ButtonRelease &&
                    (*it)->detail == ed->detail &&
                    (! ((*it)->mod & MoveResizeMask)))
                    wait_release = match = true;
            }
        }
        else if (ed->type == KeyPress) {
            for (; it != acts->end(); ++it) {
                if ((*it)->type == KeyRelease &&
                    (*it)->detail == ed->detail &&
                    (! ((*it)->mod & MoveResizeMask))) {
                    wait_release = match = true;
                    XAutoRepeatOff(display);
                }
            }
        }
    }
    it = acts->begin();
    for (; it != acts->end(); ++it) {
        if (eventmatch(*it, ed)) {
            match = true;
            XAutoRepeatOn(display);
            if ((*it)->replay && ! wait_release) replay = true;
            if ((*it)->delay.tv_sec || (*it)->delay.tv_usec) {
                Interrupt *i = new Interrupt(*it, e, id);                
                waimea->timer->AddInterrupt(i);
            } else {
                if ((*it)->exec)
                    waexec((*it)->exec, wascreen->displaystring);
                else
                    ((*this).*((*it)->winfunc))(e, *it);
            }
        }
    }
    if (waimea->eh->move_resize != EndMoveResizeType) {
        if (deleted) delete this;
        return;
    }
    
    XSync(display, false);
    while (XCheckTypedEvent(display, FocusOut, &fev))
        waimea->eh->EvFocus(&fev.xfocus);
    while (XCheckTypedEvent(display, FocusIn, &fev))
        waimea->eh->EvFocus(&fev.xfocus);
    switch (etype) {
        case WindowType:
            if (ed->type == ButtonPress || ed->type == ButtonRelease ||
                ed->type == DoubleClick) {
                if (replay || ! match)
                    XAllowEvents(display, ReplayPointer, e->xbutton.time);
                else
                    XAllowEvents(display, AsyncPointer, e->xbutton.time);
            }
            else if (ed->type == KeyPress || ed->type == KeyRelease) {
                if (replay || ! match)
                    XAllowEvents(display, ReplayKeyboard, e->xbutton.time);
                else
                    XAllowEvents(display, AsyncKeyboard, e->xbutton.time);
            }
            else if (ed->type == MapRequest && ! mapped) {
                net->SetState(this, NormalState);
                net->SetVirtualPos(this);
            }
            break;
    }
    if (deleted) delete this;
}


/**
 * @fn    WaChildWindow(WaWindow *wa_win, Window parent, int bw, int type) :
 *        WindowObject(0, type)
 * @brief Constructor for WaChildWindow class
 *
 * Creates a child window, could be of one of these types: FrameType,
 * TitleType, LabelType, HandleType, CButtonType, IButtonType, MButtonType,
 * LGripType, RGripType.
 *
 * @param wa_win WaWindow who wish to use the child window
 * @param parent Parent window to child window
 * @param bw Border width
 * @param type Type of window
 */
WaChildWindow::WaChildWindow(WaWindow *wa_win, Window parent, int type) :
    WindowObject(0, type) {
    XSetWindowAttributes attrib_set;
    
    wa = wa_win;
    wascreen = wa->wascreen;
    display = wa->display;
    ic = wascreen->ic;

    pressed = false;
    int create_mask = CWOverrideRedirect | CWBorderPixel | CWEventMask |
        CWColormap;
    attrib_set.border_pixel = wa->wascreen->wstyle.border_color.getPixel();
    attrib_set.colormap = wa->wascreen->colormap;
    attrib_set.override_redirect = true;
    attrib_set.event_mask = ButtonPressMask | ButtonReleaseMask |
        EnterWindowMask | LeaveWindowMask;
    attrib.x = 0;
    attrib.y = 0;
    attrib.width  = 1;
    attrib.height = 1;
    
    switch (type) {
        case FrameType:
            attrib_set.event_mask |= SubstructureRedirectMask;
            create_mask |= CWBackPixmap;
            attrib_set.background_pixmap = ParentRelative;
            attrib.x = wa->attrib.x - wa->border_w;
            attrib.y = wa->attrib.y - wa->title_w - wa->border_w * 2;
            attrib.width = wa->attrib.width;
            attrib.height = wa->attrib.height + wa->title_w + wa->handle_w +
                wa->border_w * 2;
            break;
        case LabelType:
            f_texture = &wascreen->wstyle.l_focus;
            u_texture = &wascreen->wstyle.l_unfocus;
            attrib_set.event_mask |= ExposureMask;
            break;
        case TitleType:
            f_texture = &wascreen->wstyle.t_focus;
            u_texture = &wascreen->wstyle.t_unfocus;
            break;
        case HandleType:
            f_texture = &wascreen->wstyle.h_focus;
            u_texture = &wascreen->wstyle.h_unfocus;
            break;
        case ButtonType:
            attrib_set.event_mask |= ExposureMask;
            break;
        case LGripType:
            f_texture = &wascreen->wstyle.g_focus;
            u_texture = &wascreen->wstyle.g_unfocus;
            create_mask |= CWCursor;
            attrib_set.cursor = wa->waimea->resizeleft_cursor;
            break;
        case RGripType:
            f_texture = &wascreen->wstyle.g_focus;
            u_texture = &wascreen->wstyle.g_unfocus;
            create_mask |= CWCursor;
            attrib_set.cursor = wa->waimea->resizeright_cursor;
            break;
    }
    id = XCreateWindow(display, parent, attrib.x, attrib.y,
                       attrib.width, attrib.height, 0, CopyFromParent,
                       CopyFromParent, CopyFromParent, create_mask,
                       &attrib_set);    

#ifdef XFT
    if (type == LabelType || type == TitleType) {
        xftdraw = XftDrawCreate(display, (Drawable) id,
                                wascreen->visual, wascreen->colormap);
    }
#endif // XFT
    
    wa->waimea->window_table.insert(make_pair(id, this));
}

/**
 * @fn    ~WaChildWindow()
 * @brief Destructor for WaChildWindow class
 *
 * Destroys the window and removes it from the window_table hash_map.
 */
WaChildWindow::~WaChildWindow(void) {
    
#ifdef XFT
    if (type == LabelType) XftDrawDestroy(xftdraw);
#endif // XFT
    
    wa->waimea->window_table.erase(id);
    XDestroyWindow(display, id);
}

/**
 * @fn    Render(void)
 * @brief Render WaChildWindow background
 *
 * Renders WaChildWindow background pixmap for the current window state.
 */
void WaChildWindow::Render(void) {
    bool done = false;
    WaTexture *texture = (wa->has_focus)? f_texture: u_texture;
    Pixmap pixmap = None;

#ifdef RENDER
    Pixmap xpixmap;
    int pos_x = wa->attrib.x + attrib.x + wa->border_w;
    int pos_y = wa->attrib.y - wa->title_w + attrib.y;
    if (texture->getOpacity()) {
        xpixmap = XCreatePixmap(wascreen->pdisplay, wascreen->id,
                                attrib.width, attrib.height,
                                wascreen->screen_depth);
    } else if (wa->render_if_opacity && IsDrawable()) return;
#endif // RENDER
    
    switch (type) {
        case ButtonType: {
            bool flag = false;
            done = true;
            switch (bstyle->cb) {
                case MaxCBoxType: flag = wa->flags.max; break;
                case ShadeCBoxType: flag = wa->flags.shaded; break;
                case StickCBoxType: flag = wa->flags.sticky; break;
                case TitleCBoxType: flag = wa->flags.title; break;
                case HandleCBoxType: flag = wa->flags.handle; break;
                case BorderCBoxType: flag = wa->flags.border; break;
                case AllCBoxType: flag = wa->flags.all; break;
                case AOTCBoxType: flag = wa->flags.alwaysontop; break;
                case AABCBoxType: flag = wa->flags.alwaysatbottom; break;
            }
            if (flag) {
                pixmap = (pressed) ? bstyle->p_pressed2:
                    ((wa->has_focus)? bstyle->p_focused2:
                     bstyle->p_unfocused2);
                texture = (pressed) ? &bstyle->t_pressed2:
                    ((wa->has_focus)? &bstyle->t_focused2:
                     &bstyle->t_unfocused2);
            } else {
                pixmap = (pressed) ? bstyle->p_pressed:
                    ((wa->has_focus)? bstyle->p_focused:
                     bstyle->p_unfocused);
                texture = (pressed) ? &bstyle->t_pressed:
                    ((wa->has_focus)? &bstyle->t_focused:
                     &bstyle->t_unfocused);
            }

#ifdef RENDER
            if (texture->getOpacity())
                pixmap = ic->xrender(pixmap, attrib.width, attrib.height,
                                     texture, wascreen->xrootpmap_id, pos_x,
                                     pos_y, xpixmap);
#endif // RENDER
                
        } break;
        case LGripType:
        case RGripType:
            done = true;
#ifdef RENDER
            if (texture->getOpacity())
                pixmap = ic->xrender((wa->has_focus)? wascreen->fgrip:
                                     wascreen->ugrip,
                                     attrib.width, attrib.height,
                                     texture, wascreen->xrootpmap_id, pos_x,
                                     pos_y, xpixmap);
            else
#endif // RENDER
                pixmap = (wa->has_focus)? wascreen->fgrip: wascreen->ugrip;
            
            break;
    }
    if (! done) {
        if (texture->getTexture() == (WaImage_Flat | WaImage_Solid)) {
            pixmap = None;
#ifdef RENDER
            if (texture->getOpacity())
                pixmap = ic->xrender(None, attrib.width, attrib.height,
                                     texture, wascreen->xrootpmap_id, pos_x,
                                     pos_y, xpixmap);
#endif // RENDER
            
        } else
            pixmap = ic->renderImage(attrib.width,
                                     attrib.height, texture
                                      
#ifdef RENDER
                                     , wascreen->xrootpmap_id, pos_x, pos_y,
                                     xpixmap
#endif // RENDER
                                      
                                     );
    }
    
    if (pixmap) {
        if (wascreen->config.db) Draw((Drawable) pixmap);
        else XSetWindowBackgroundPixmap(display, id, pixmap);
        
#ifdef PIXMAP
        if (
            
#ifdef RENDER        
            (! texture->getOpacity()) &&
#endif // RENDER
            
            (texture->getTexture() & WaImage_Pixmap)) {
            XSync(display, false);
            imlib_context_push(*texture->getContext());
            imlib_free_pixmap_and_mask(pixmap);
            imlib_context_pop();
            pixmap = None;
        }
#endif // PIXMAP
        
    }
    else {
        if (wascreen->config.db) Draw((Drawable) 2);
        else XSetWindowBackground(display, id,
                                  texture->getColor()->getPixel());
    }

    if (! wascreen->config.db) Draw();

#ifdef RENDER
    if (pixmap && texture->getOpacity()) {
        XSync(display, false);
        XFreePixmap(wascreen->pdisplay, pixmap);
    }
#endif // RENDER
    
}

/**
 * @fn    Draw(Drawable drawable)
 * @brief Draw text/decorations
 *
 * Draws text in window title window and button graphics for button windows.
 * If drawable is other then NULL then we draw on this instead.
 *
 * @param drawable Drawable to draw on
 */
void WaChildWindow::Draw(Drawable drawable) {
    int x = 0, y = 0, length, text_w;
    
    if (! drawable) XClearWindow(display, id);
    switch (type) {
        case TitleType:
            if (! drawable) return;
            if (wa->label->IsDrawable()) {
                if (drawable == (Drawable) 2) {
                    WaTexture *texture = (wa->has_focus)? f_texture: u_texture;
                    XSetWindowBackground(display, id,
                                         texture->getColor()->getPixel());
                } else
                    XSetWindowBackgroundPixmap(display, id, drawable);
                XClearWindow(display, id);
                return;
            }
            x = wa->label->g_x;
            y = 2;
        case LabelType: {
            if (type == LabelType && drawable) {
                if (drawable == ParentRelative) {
                    if (wa->title->IsDrawable()) {
                        XSetWindowBackgroundPixmap(display, id, drawable);
                        XClearWindow(display, id);
                        return;
                    }
                    drawable = 0;
                }
            }
            Pixmap p_tmp;
            if (drawable) {
                p_tmp = XCreatePixmap(display, wascreen->id,
                                      attrib.width, attrib.height,
                                      wascreen->screen_depth);
                if (drawable == (Drawable) 2) {
                    XGCValues values;
                    WaTexture *texture = (wa->has_focus)? f_texture: u_texture;
                    values.foreground = texture->getColor()->getPixel();
                    GC gc = XCreateGC(display, wascreen->id, GCForeground,
                                      &values);
                    XFillRectangle(display, p_tmp, gc, 0, 0, attrib.width,
                                   attrib.height);
                    XFreeGC(display, gc);
                } else {
                    GC gc = DefaultGC(display, wascreen->screen_number);
                    XCopyArea(display, drawable, p_tmp, gc, 0, 0, attrib.width,
                              attrib.height, 0, 0);
                }
            }
            length = strlen(wa->name);
            WaFont *wafont = (wa->has_focus)? &wascreen->wstyle.wa_font:
                &wascreen->wstyle.wa_font_u;
            text_w = wafont->Width(display, wa->name, length);
    
            if (text_w > (wa->label->attrib.width - 10)) x += 2;
            else {
                switch (wascreen->wstyle.justify) {
                    case LeftJustify: x += 2; break;
                    case CenterJustify:
                        x += (wa->label->attrib.width / 2) - (text_w / 2);
                        break;
                    case RightJustify:
                        x += (wa->label->attrib.width - text_w) - 2;
                        break;
                }
            }

            y += wascreen->wstyle.y_pos;

#ifdef    XFT
            if (drawable) XftDrawChange(xftdraw, p_tmp);
#endif // XFT            
            
            wafont->Draw(display, (drawable)? p_tmp : id,
                         
#ifdef    XFT
                     xftdraw,
#endif // XFT
                 
                     x, y, wa->name, length);

            if (drawable) {
                XSetWindowBackgroundPixmap(display, id, p_tmp);
                XClearWindow(display, id);
                XFreePixmap(display, p_tmp);
            }
        } break;
        case ButtonType: {
            if (drawable) {
                if (drawable == (Drawable) 2) {
                    WaTexture *texture = (wa->has_focus)? f_texture: u_texture;
                    XSetWindowBackground(display, id,
                                         texture->getColor()->getPixel());
                } else
                    XSetWindowBackgroundPixmap(display, id, drawable);
                XClearWindow(display, id);
            }
            GC *gc;
            if (bstyle->fg) {
                bool flag = false;
                switch (bstyle->cb) {
                    case MaxCBoxType: flag = wa->flags.max; break;
                    case ShadeCBoxType: flag = wa->flags.shaded; break;
                    case StickCBoxType: flag = wa->flags.sticky; break;
                    case TitleCBoxType: flag = wa->flags.title; break;
                    case HandleCBoxType: flag = wa->flags.handle; break;
                    case BorderCBoxType: flag = wa->flags.border; break;
                    case AllCBoxType: flag = wa->flags.all; break;
                    case AOTCBoxType: flag = wa->flags.alwaysontop; break;
                    case AABCBoxType: flag = wa->flags.alwaysatbottom; break;
                }
                if (flag) {
                    gc = (pressed) ? &bstyle->g_pressed2:
                        ((wa->has_focus)? &bstyle->g_focused2:
                         &bstyle->g_unfocused2);
                }
                else {
                    gc = (pressed) ? &bstyle->g_pressed:
                        ((wa->has_focus)? &bstyle->g_focused:
                         &bstyle->g_unfocused);
                }                    
                
                switch (bstyle->cb) {
                    case ShadeCBoxType:
                        XDrawRectangle(display, id, *gc, 2, 3,
                                       wa->title_w - 9, 2);
                        break;
                    case CloseCBoxType:
                        XDrawLine(display, id, *gc, 2, 2, wa->title_w - 7,
                                  wa->title_w - 7);
                        XDrawLine(display, id, *gc, 2, wa->title_w - 7,
                                  wa->title_w - 7, 2);
                        break;
                    case MaxCBoxType:
                        if (wa->flags.max) {
                            int w = (2*(wa->title_w - 8))/3;
                            int h = (2*(wa->title_w - 8))/3 - 1;
                            int y = (wa->title_w - 8) - h + 1;
                            int x = (wa->title_w - 8) - w + 1;
                            XDrawRectangle(display, id, *gc, 2, y, w,h);
                            XDrawLine(display, id, *gc, 2, y + 1, 2 + w,
                                      y + 1);
                            XDrawLine(display, id, *gc, x, 2, x + w, 2);
                            XDrawLine(display, id, *gc, x, 3, x + w, 3);
                            XDrawLine(display, id, *gc, x, 2, x, y);
                            XDrawLine(display, id, *gc, x + w, 2, x + w,
                                      2 + h);
                            XDrawLine(display, id, *gc, 2 + w, 2 + h, x + w,
                                      2 + h);
                        } else {
                            XDrawRectangle(display, id, *gc, 2, 2,
                                           wa->title_w - 9, wa->title_w - 9);
                            XDrawLine(display, id, *gc, 2, 3, wa->title_w - 8,
                                      3);
                        }
                        break;
                    default:
                        XFillRectangle(display, id, *gc, 4, 4,
                                       wa->title_w - 11, wa->title_w - 11);
                }
            }
        } break;
        default:
            if (drawable) {
                if (drawable == (Drawable) 2) {
                    WaTexture *texture = (wa->has_focus)? f_texture: u_texture;
                    XSetWindowBackground(display, id,
                                         texture->getColor()->getPixel());
                } else
                    XSetWindowBackgroundPixmap(display, id, drawable);
                XClearWindow(display, id);
            }
    }
}

/**
 * @fn    IsDrawable(void)
 * @brief Check if drawable
 *
 * Checks if if window is double buffer drawable (if it will have a pixmap
 * background).
 */
bool WaChildWindow::IsDrawable(void) {
    WaTexture *texture = (wa->has_focus)? f_texture:
        u_texture;
    if (texture->getTexture() & WaImage_ParentRelative)
        return false;
    else
        return true;
}


/**
 * Wrapper functions
 */
void WaWindow::ViewportMove(XEvent *e, WaAction *wa) {
    wascreen->ViewportMove(e, wa);
}
void WaWindow::ViewportRelativeMove(XEvent *e, WaAction *wa) {
    wascreen->ViewportRelativeMove(e, wa);
}
void WaWindow::ViewportFixedMove(XEvent *e, WaAction *wa) {
    wascreen->ViewportFixedMove(e, wa);
}
void WaWindow::MoveViewportLeft(XEvent *, WaAction *) {
    wascreen->MoveViewport(WestDirection);
}
void WaWindow::MoveViewportRight(XEvent *, WaAction *) {
    wascreen->MoveViewport(EastDirection);
}
void WaWindow::MoveViewportUp(XEvent *, WaAction *) {
    wascreen->MoveViewport(NorthDirection);
}
void WaWindow::MoveViewportDown(XEvent *, WaAction *) {
    wascreen->MoveViewport(SouthDirection);
}
void WaWindow::PointerRelativeWarp(XEvent *e, WaAction *ac) {
    wascreen->PointerRelativeWarp(e, ac);
}
void WaWindow::PointerFixedWarp(XEvent *e, WaAction *ac) {
    wascreen->PointerFixedWarp(e, ac);
}
void WaWindow::GoToDesktop(XEvent *, WaAction *ac) {
    if (ac->param) wascreen->GoToDesktop((unsigned int) atoi(ac->param));
}
void WaWindow::NextDesktop(XEvent *, WaAction *) {
    wascreen->NextDesktop(NULL, NULL);
}
void WaWindow::PreviousDesktop(XEvent *, WaAction *) {
    wascreen->PreviousDesktop(NULL, NULL);
}
void WaWindow::Restart(XEvent *e, WaAction *ac) {
    wascreen->Restart(e, ac);
}
void WaWindow::Exit(XEvent *e, WaAction *ac) {
    wascreen->Exit(e, ac);
}

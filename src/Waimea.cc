/** -*- Mode: C++ -*-
 *
 * @file   Waimea.cc
 * @author David Reveman <c99drn@cs.umu.se>
 * @date   02-May-2001 00:48:03
 *
 * @brief Implementation of Waimea and WindowObject classes
 *
 * Waimea Window Manager.
 *
 * Copyright (C) David Reveman. All rights reserved.
 *
 */

#include "Waimea.hh"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


/** 
 * @fn    WindowObject(Window win_id, int win_type)
 * @brief Constructor for WindowObject class
 *
 * Creates a simple window object with a window id and type value. All windows
 * we want to be able to search for should inherit this class.
 *
 * @param win_id Resource ID of window
 * @param win_type Type of WindowObject
 */
WindowObject::WindowObject(Window win_id, int win_type) {
    id = win_id;
    type = win_type;
}


Waimea *waimea;
char **argv;
hash_map<Window, WindowObject *> *windows;
char *errfunc;

/**
 * @fn    Waimea(char **av)
 * @brief Constructor for Waimea class
 *
 * Here we open a connection to the display and set signal and xerror
 * handler functions. A list for all WaWindow is created and one list for all
 * WaMenus. An hash_map is also created for effective window searching. Then
 * we load the configuration file, menu file and actions file. We create one
 * WaScreen for the displays default screen and an eventhandler for handling
 * all events.
 *
 * @param av Vector of command line arguments
 * @param options Parsed command line options
 */
Waimea::Waimea(char **av, struct waoptions *options) {
    struct sigaction act;    

    argv = av;
    XSetErrorHandler((XErrorHandler) xerrorhandler);
    if (! (display = XOpenDisplay(options->display))) {
        cerr << "Error: Can't open display: " << options->display << endl;
        exit(1);
    }
    waimea = this;
    
    signal(SIGTERM, signalhandler);
    signal(SIGINT, signalhandler);
    signal(SIGHUP, signalhandler);
    signal(SIGCHLD, signalhandler);
    
    windows = window_table = new hash_map<Window, WindowObject *>;
    always_on_top_list = new list<Window>;
    always_at_bottom_list = new list<Window>;
    wawindow_list = new list<WaWindow *>;
    wawindow_list_map_order = new list<WaWindow *>;
    wawindow_list_stacking = new list<WaWindow *>;
    wawindow_list_stacking_aot = new list<WaWindow *>;
    wawindow_list_stacking_aab = new list<WaWindow *>;
    wamenu_list = new list<WaMenu *>;
    
    session_cursor = XCreateFontCursor(display, XC_left_ptr);
    move_cursor = XCreateFontCursor(display, XC_fleur);
    resizeleft_cursor = XCreateFontCursor(display, XC_ll_angle);
    resizeright_cursor = XCreateFontCursor(display, XC_lr_angle);

    rh = new ResourceHandler(this, options);
    rh->LoadConfig();
    rh->LoadMenus();
    taskswitch = new TaskSwitcher();
    wamenu_list->push_back(taskswitch);
    rh->LoadActions(this);

    net = new NetHandler(this);
    wascreen = new WaScreen(display, DefaultScreen(display), this);

    WaRaiseWindow((Window) 0);
    eh = new EventHandler(this);
}

/**
 * @fn    ~Waimea(void)
 * @brief Destructor for Waimea class
 *
 * Deletes all WaWindows, all WaScreens and all WaMenus. Closes the 
 * connection to the display.
 */
Waimea::~Waimea(void) {
    net->SetClientList(wascreen);
    net->SetClientListStacking(wascreen);
    delete eh;
    LISTCLEAR(wamenu_list);
    LISTCLEAR2(wawindow_list);
    delete wawindow_list_map_order;
    delete wawindow_list_stacking;
    delete wawindow_list_stacking_aot;
    delete wawindow_list_stacking_aab;
    delete wascreen;
    delete net;
    delete rh;
    delete window_table;

    XSync(display, False);
    XCloseDisplay(display);
}

/**
 * @fn    WaRaiseWindow(Window win)
 * @brief Raise window
 *
 * Raises a window in the display stack keeping alwaysontop windows, always
 * on top. To update the stacking order so that alwaysontop windows are on top
 * of all other windows we call this function with zero as win argument. 
 *
 * @param win Window to Raise, or 0 for alwaysontop windows update
 */
void Waimea::WaRaiseWindow(Window win) {
    int i;
    
    if (always_on_top_list->size()) {
        Window *stack = new Window[always_on_top_list->size() + ((win)? 1: 0)];

        list<Window>::iterator it = always_on_top_list->begin();
        for (i = 0; it != always_on_top_list->end(); ++it) {
            stack[i++] = *it;
        }
        if (win) stack[i++] = win;
        
        XGrabServer(display);
        if ((! win) || validateclient(win)) {
            XRaiseWindow(display, stack[0]);
            XRestackWindows(display, stack, i);
        }
        XUngrabServer(display);
        
        delete [] stack;
    } else
        if (win) {
            XGrabServer(display);
            if (validateclient(win))
                XRaiseWindow(display, win);
            XUngrabServer(display);
        }
}

/**
 * @fn    WaLowerWindow(Window win)
 * @brief Lower window
 *
 * Lower a window in the display stack keeping alwaysatbottom windows, always
 * at the bottom. To update the stacking order so that alwaysatbottom windows
 * are at the bottom of all other windows we call this function with zero as
 * win argument. 
 *
 * @param win Window to Lower, or 0 for alwaysatbottom windows update
 */
void Waimea::WaLowerWindow(Window win) {
    int i;
    
    if (always_at_bottom_list->size()) {
        Window *stack = new Window[always_at_bottom_list->size() +
                                  ((win)? 1: 0)];
        i = 0;
        if (win) stack[i++] = win;
                
        list<Window>::reverse_iterator it = always_at_bottom_list->rbegin();
        for (; it != always_at_bottom_list->rend(); ++it) {
            stack[i++] = *it;
        }
        
        XGrabServer(display);
        if ((! win) || validateclient(win)) {
            XLowerWindow(display, stack[0]);
            XRestackWindows(display, stack, i);
        }
        XUngrabServer(display);
        
        delete [] stack;
    } else
        if (win) {
            XGrabServer(display);
            if (validateclient(win))
                XLowerWindow(display, win);
            XUngrabServer(display);
        }
}

/**
 * @fn    UpdateCheckboxes(int type)
 * @brief Updates menu checkboxes
 *
 * Redraws all checkbox menu items to make sure they are synchronized with
 * their given flag.
 *
 * @param type Type of checkboxes to update
 */
void Waimea::UpdateCheckboxes(int type) {
    list<WaMenuItem *>::iterator miit;
    list<WaMenu *>::iterator mit = wamenu_list->begin();
    for (; mit != wamenu_list->end(); ++mit) {
        miit = (*mit)->item_list->begin();
        for (; miit != (*mit)->item_list->end(); ++miit) {
            if ((*miit)->cb == type) (*miit)->DrawFg();
        }
    }
}

/**
 * @fn    validateclient(Window id)
 * @brief Validates a window
 *
 * Checks if the event queue holds any DestroyNotify or UnmapNotify events
 * for the window we want to validate. If non of these events are found in the
 * event queue when the window is valid.
 *
 * @param id Resource ID used for window validation
 *
 * @return True if window is valid, otherwise False
 */
Bool validateclient(Window id) {
    XSync(waimea->display, False);
    
    XEvent e;
    if (XCheckTypedWindowEvent(waimea->display, id, DestroyNotify, &e) ||
        XCheckTypedWindowEvent(waimea->display, id, UnmapNotify, &e)) {
        XPutBackEvent(waimea->display, &e);
        return False;
    }
    return True;
}

/**
 * @fn    waexec(const char *command, char *displaystring)
 * @brief Executes a command line
 *
 * Executes a command line in the 'sh' shell.
 *
 * @param format Command line to execute
 * @param displaystring Displaystring to export prior to execution
 */
void waexec(const char *command, char *displaystring) {
    if (! fork()) {
        setsid();
        putenv(displaystring);
        execl("/bin/sh", "/bin/sh", "-c", command, NULL);
        exit(0);
    }
}

/**
 * @fn    xerrorhandler(Display *d, XErrorEvent *e)
 * @brief X error handler function
 *
 * Prints error message then a X error occurs.
 *
 * @param d X display 
 * @param e X error event
 *
 * @return always 0
 */
int xerrorhandler(Display *d, XErrorEvent *e) {
    char buff[128];
    hash_map<Window, WindowObject *>::iterator it;
   
    XGetErrorDatabaseText(d, "XlibMessage", "XError", "", buff, 128);
    cerr << buff;
    XGetErrorText(d, e->error_code, buff, 128);
    cerr << ":  " << buff << endl;
    XGetErrorDatabaseText(d, "XlibMessage", "MajorCode", "%d", buff, 128);
    cerr << "  ";
    fprintf(stderr, buff, e->request_code);
    sprintf(buff, "%d", e->request_code);
    XGetErrorDatabaseText(d, "XRequest", buff, "%d", buff, 128);
    cerr << " (" << buff << ")" << endl;
    XGetErrorDatabaseText(d, "XlibMessage", "MinorCode", "%d", buff, 128);
    cerr << "  ";
    fprintf(stderr, buff, e->minor_code);
    cerr << endl;
    XGetErrorDatabaseText(d, "XlibMessage", "ResourceID", "%d", buff, 128);
    cerr << "  ";
    fprintf(stderr, buff, e->resourceid);
    if (((it = windows->find(e->resourceid)) != waimea->window_table->end()) &&
        (((*it).second)->type == WindowType))
        cerr << " (" << ((WaWindow *) (*it).second)->name << ")";
    cerr << endl;
    
    return 0;
}

/**
 * @fn    wmrunningerror(Display *d, XErrorEvent *)
 * @brief X error handler function
 *
 * X error handler function used when setting input mask of root window.
 * If X error occurs another window manager is running on that screen.
 * This function displays error message and ends program execution.
 *
 * @param d X display
 *
 * @return never returns
 */
int wmrunningerror(Display *d, XErrorEvent *) {
    cerr << "Error: another window manager is already running on display" <<
        DisplayString(d) << endl;
    exit(1);
}

/**
 * @fn    signalhandler(int signal)
 * @brief Signal handler function
 *
 * When one of the signals we handle arrives this function is called. Depending
 * on what type of signal we received we do something, ex. restart, exit.
 *
 * @param sig The signal we received
 */
void signalhandler(int sig) {
    int status;
    
    switch(sig) {
        case SIGINT:
        case SIGTERM:
        case SIGHUP:
            cout << "signal: " << sig << " caught. ";
            quit(EXIT_SUCCESS);
            break;
        case SIGCHLD:
            waitpid(-1, &status, WNOHANG | WUNTRACED);
            signal(SIGCHLD, signalhandler);
            break;
        default:
            quit(EXIT_FAILURE);
    }
}

/**
 * @fn    restart(void)   
 * @brief Restarts program
 *
 * Deletes the waimea object and restarts window manager.
 */
void restart(void) {
    cout << "restarting." << endl;
    delete waimea;
    execvp(argv[0], argv);
    perror("restart");
    exit(EXIT_FAILURE);
}

/**
 * @fn    quit(void)   
 * @brief Ends program execution
 *
 * Deletes the waimea object and then ends program execution.
 *
 * @param status Return status
 */
void quit(int status) {
    cout << "shutting down." << endl;
    delete waimea;
    exit(status);
}

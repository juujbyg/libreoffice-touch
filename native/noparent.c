/* LD_PRELOAD shim that turns LibreOffice's GTK3 dialogs into phone-style sheets on Lomiri.
 *
 * Lomiri/Mir places a dialog (a transient child window) as if it were 640x480, about 220 px
 * from the left, and never moves it again, so bigger dialogs run off the screen. A window with
 * no parent is maximised to the screen instead, unless its size is fixed, in which case it
 * sits in the top-left corner (Tip of the Day did that).
 *
 * So no dialog gets a parent. Each one is made resizable and maximised, and its contents are
 * laid out inside that full-screen window just before it is first shown:
 *  - small dialogs become a card centred on a plain grey backdrop (CSS classes lo-sheet, lo-card);
 *  - large ones fill the screen (lo-fill);
 *  - ones bigger than the screen also get their contents in a scrolled area, with the dialog
 *    buttons kept outside it so they stay visible.
 * LibreOffice also asks for a minimum window size worked out from its toolbars; at phone scale
 * that can be wider than the screen, and Lomiri then cannot maximise even the document window.
 * Those minimums are capped at the screen size. Lomiri places a document window that is not
 * maximised as if it were 640x480 too, so document windows are always maximised.
 * Other parentless windows are maximised the same way; if their contents are bigger than the
 * screen (Base's Database Wizard) they scroll with their button row kept visible, and a
 * GtkAssistant's step list is hidden.
 * The look comes from the LibreOfficePhone GTK theme. LO_SHIM_DEBUG=1 logs each decision. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

typedef struct { int width, height; } Req;
typedef struct { int x, y, width, height; } Rect;
typedef size_t GType;
typedef struct _GList { void *data; struct _GList *next, *prev; } GList;

static void *lib(const char *name) { return dlopen(name, RTLD_LAZY | RTLD_NOLOAD); }
static void *gtk(const char *s) { return dlsym(lib("libgtk-3.so.0"), s); }
static void *gdk(const char *s) { return dlsym(lib("libgdk-3.so.0"), s); }
static void *gob(const char *s) { return dlsym(lib("libgobject-2.0.so.0"), s); }
static void *glib(const char *s) { return dlsym(lib("libglib-2.0.so.0"), s); }
#define CALL(lib, ret, name, args) ((ret(*)args)lib(#name))

static const char KEY[] = "lo-shim-parent", DONE[] = "lo-shim-done", HOOKED[] = "lo-shim-hooked";
static int debug = -1;
/* Room for the panel at the top of the screen and Lomiri's title bar on the dialog. */
enum { MARGIN_W = 16, MARGIN_H = 190 };

static void real_set(void *w, void *p) {
    static void (*f)(void *, void *);
    if (!f) f = dlsym(RTLD_NEXT, "gtk_window_set_transient_for");
    f(w, p);
}
static void *get(void *o, const char *k) { return CALL(gob, void *, g_object_get_data, (void *, const char *))(o, k); }
static void set(void *o, const char *k, void *v) { CALL(gob, void, g_object_set_data, (void *, const char *, void *))(o, k, v); }
static int is_a(void *o, const char *type_fn) {
    GType (*tf)(void) = gtk(type_fn);
    return tf && CALL(gob, int, g_type_check_instance_is_a, (void *, GType))(o, tf());
}

static void screen_area(Rect *a) {
    a->width = a->height = 0;
    void *disp = CALL(gdk, void *, gdk_display_get_default, (void))();
    void *mon = disp ? CALL(gdk, void *, gdk_display_get_monitor, (void *, int))(disp, 0) : NULL;
    if (mon) CALL(gdk, void, gdk_monitor_get_workarea, (void *, Rect *))(mon, a);
}

/* Move `child` out of `from` into `to`, keeping its expand/fill packing when both are boxes. */
static void move_child(void *child, void *from, void *to) {
    int expand = 1, fill = 1, padding = 0, box = is_a(from, "gtk_box_get_type");
    if (box) CALL(gtk, void, gtk_container_child_get, (void *, void *, ...))(from, child, "expand", &expand, "fill", &fill, "padding", &padding, NULL);
    CALL(gob, void *, g_object_ref, (void *))(child);
    CALL(gtk, void, gtk_container_remove, (void *, void *))(from, child);
    CALL(gtk, void, gtk_box_pack_start, (void *, void *, int, int, unsigned))(to, child, expand, fill, (unsigned)padding);
    CALL(gob, void, g_object_unref, (void *))(child);
}

static void wrap_in_scroller(void *w) {
    void *top = CALL(gtk, void *, gtk_bin_get_child, (void *))(w);
    if (!top) return;
    void *sw = CALL(gtk, void *, gtk_scrolled_window_new, (void *, void *))(NULL, NULL);
    CALL(gtk, void, gtk_scrolled_window_set_policy, (void *, int, int))(sw, 1, 1); /* AUTOMATIC */
    void *inner = CALL(gtk, void *, gtk_box_new, (int, int))(1, 0);           /* VERTICAL */
    CALL(gtk, void, gtk_container_add, (void *, void *))(sw, inner);
    void *action = NULL;
    if (is_a(w, "gtk_dialog_get_type") && is_a(top, "gtk_box_get_type")) {
        /* GtkDialog: scroll everything except the button row. */
        action = CALL(gtk, void *, gtk_dialog_get_action_area, (void *))(w);
        GList *kids = CALL(gtk, GList *, gtk_container_get_children, (void *))(top);
        for (GList *l = kids; l; l = l->next)
            if (l->data != action) move_child(l->data, top, inner);
        CALL(glib, void, g_list_free, (GList *))(kids);
        CALL(gtk, void, gtk_box_pack_start, (void *, void *, int, int, unsigned))(top, sw, 1, 1, 0);
        CALL(gtk, void, gtk_box_reorder_child, (void *, void *, int))(top, sw, 0);
    } else {
        move_child(top, w, inner);
        CALL(gtk, void, gtk_container_add, (void *, void *))(w, sw);
    }
    CALL(gtk, void, gtk_widget_show_all, (void *))(sw);
}

static void add_class(void *widget, const char *cls) {
    void *ctx = CALL(gtk, void *, gtk_widget_get_style_context, (void *))(widget);
    CALL(gtk, void, gtk_style_context_add_class, (void *, const char *))(ctx, cls);
}

/* Entries in a header bar (the Save dialog's file name) ask for desktop widths: let them shrink
 * to fit the screen and then stretch across the room between the buttons. */
static void shrink_entries(void *widget, void *unused) {
    (void)unused;
    if (is_a(widget, "gtk_entry_get_type")) {
        CALL(gtk, void, gtk_entry_set_width_chars, (void *, int))(widget, 8);
        CALL(gtk, void, gtk_widget_set_hexpand, (void *, int))(widget, 1); /* fill the space it has */
        return;
    }
    if (is_a(widget, "gtk_container_get_type"))
        CALL(gtk, void, gtk_container_forall, (void *, void *, void *))(widget, (void *)shrink_entries, NULL);
}

/* Lay the dialog out once, before it is mapped. */
static void prepare(void *w, int unused) {
    (void)unused;
    if (get(w, DONE) || !get(w, KEY)) return;
    set(w, DONE, (void *)1);
    Rect a; screen_area(&a);
    if (a.width <= 0) {
        if (debug) fprintf(stderr, "lo-shim: %p no screen size yet\n", w);
        return;
    }
    void *bar = CALL(gtk, void *, gtk_window_get_titlebar, (void *))(w);
    Req min, nat;
    if (bar) {
        CALL(gtk, void, gtk_widget_get_preferred_size, (void *, Req *, Req *))(bar, &min, &nat);
        if (min.width > a.width * 2 / 3) shrink_entries(bar, NULL);
    }
    CALL(gtk, void, gtk_widget_get_preferred_size, (void *, Req *, Req *))(w, &min, &nat);
    int maxw = a.width - MARGIN_W, maxh = a.height - MARGIN_H;
    const char *what;
    if (min.width > maxw || min.height > maxh) {
        wrap_in_scroller(w);
        what = "scrolled sheet";
    } else if (nat.width > a.width - 80 || nat.height > a.height * 60 / 100) {
        what = "full sheet";
    } else {
        what = "centred card";
    }
    void *top = CALL(gtk, void *, gtk_bin_get_child, (void *))(w);
    if (top) {
        if (what[0] == 'c') {
            CALL(gtk, void, gtk_widget_set_halign, (void *, int))(top, 3); /* GTK_ALIGN_CENTER */
            CALL(gtk, void, gtk_widget_set_valign, (void *, int))(top, 3);
            add_class(top, "lo-card");
        } else add_class(top, "lo-fill");
    }
    add_class(w, "lo-sheet");
    CALL(gtk, void, gtk_window_set_resizable, (void *, int))(w, 1);
    CALL(gtk, void, gtk_window_set_default_size, (void *, int, int))(w, a.width, a.height);
    CALL(gtk, void, gtk_window_maximize, (void *))(w);
    if (debug) {
        Req m2, n2, bm = {0, 0}, bn = {0, 0};
        CALL(gtk, void, gtk_widget_get_preferred_size, (void *, Req *, Req *))(w, &m2, &n2);
        if (bar) CALL(gtk, void, gtk_widget_get_preferred_size, (void *, Req *, Req *))(bar, &bm, &bn);
        fprintf(stderr, "lo-shim: %p nat %dx%d min %dx%d screen %dx%d -> %s; now min %dx%d, titlebar min %d nat %d\n",
                w, nat.width, nat.height, min.width, min.height, a.width, a.height, what,
                m2.width, m2.height, bm.width, bn.width);
    }
}

static void on_realize(void *w, void *unused) { (void)unused; prepare(w, 1); }

void gtk_window_set_transient_for(void *window, void *parent) {
    if (debug < 0) debug = getenv("LO_SHIM_DEBUG") != NULL;
    set(window, KEY, parent);
    if (!parent) { real_set(window, NULL); return; }
    if (CALL(gtk, int, gtk_widget_get_realized, (void *))(window)) return; /* already on screen: leave it */
    if (get(window, HOOKED)) return;
    set(window, HOOKED, (void *)1);
    /* The backdrop stays opaque: Lomiri hides the document window behind a maximised dialog,
     * so a translucent one showed the phone's wallpaper instead of the document. */
    CALL(gob, unsigned long, g_signal_connect_data, (void *, const char *, void *, void *, void *, int))(
        window, "realize", (void *)on_realize, NULL, NULL, 1 /* G_CONNECT_AFTER */);
}

static void on_frame_realize(void *w, void *unused);
static void before_show(void *w) {
    if (!w) return;
    if (debug < 0) debug = getenv("LO_SHIM_DEBUG") != NULL;
    if (debug > 0 && is_a(w, "gtk_window_get_type") && !get(w, "lo-shim-seen")) {
        set(w, "lo-shim-seen", (void *)1);
        fprintf(stderr, "lo-shim: show %p type %s parent-key %p\n", w,
                CALL(gob, const char *, g_type_name, (size_t))(**(size_t **)w), get(w, KEY));
    }
    if (get(w, KEY) && !get(w, DONE) && !CALL(gtk, int, gtk_widget_get_realized, (void *))(w)) { prepare(w, 1); return; }
    if (get(w, KEY) || get(w, "lo-shim-new") || !is_a(w, "gtk_window_get_type")) return;
    /* A window built from a .ui file with no parent (Base's Database Wizard, other wizards):
     * not a document window (those come from gtk_window_new), so lay it out like a dialog. */
    if (CALL(gtk, int, gtk_window_get_window_type, (void *))(w) != 0 /* popup */ ||
        CALL(gtk, int, gtk_widget_get_realized, (void *))(w)) return;
    if (is_a(w, "gtk_assistant_get_type")) { on_frame_realize(w, NULL); return; }
    set(w, KEY, (void *)1); /* marks it as one of ours; no real parent is ever set */
    prepare(w, 1);
}
void gtk_widget_realize(void *w) {
    static void (*f)(void *); if (!f) f = dlsym(RTLD_NEXT, "gtk_widget_realize");
    before_show(w); f(w);
}
void gtk_widget_show(void *w) {
    static void (*f)(void *); if (!f) f = dlsym(RTLD_NEXT, "gtk_widget_show");
    before_show(w); f(w);
}
void gtk_widget_show_all(void *w) {
    static void (*f)(void *); if (!f) f = dlsym(RTLD_NEXT, "gtk_widget_show_all");
    before_show(w); f(w);
}
/* LibreOffice's single-instance socket goes in /tmp, or /var/tmp if /tmp isn't writable, or else in
 * the folder named by its OSL_SOCKET_PATH setting. Inside the app sandbox both are denied, but
 * access() only checks file permissions, so LibreOffice would pick /tmp, fail to create the socket and
 * exit with code 1. With LO_SHIM_PRIVATE_TMP set (by the launcher), both report "not writable" here,
 * and the socket goes to OSL_SOCKET_PATH (the app's folder in XDG_RUNTIME_DIR). */
int access(const char *path, int mode) {
    static int (*f)(const char *, int);
    static int private_tmp = -1;
    if (!f) f = dlsym(RTLD_NEXT, "access");
    if (private_tmp < 0) private_tmp = getenv("LO_SHIM_PRIVATE_TMP") != NULL;
    if (private_tmp && (mode & W_OK) && (!strcmp(path, "/tmp") || !strcmp(path, "/var/tmp"))) {
        errno = EACCES;
        return -1;
    }
    return f(path, mode);
}

/* Message boxes with no parent (e.g. "Document in Use", asked before the document window exists)
 * are run with gtk_dialog_run, which shows them inside GTK, out of reach of the hooks above. */
int gtk_dialog_run(void *w) {
    static int (*f)(void *); if (!f) f = dlsym(RTLD_NEXT, "gtk_dialog_run");
    if (debug < 0) debug = getenv("LO_SHIM_DEBUG") != NULL;
    if (debug) fprintf(stderr, "lo-shim: dialog_run %p\n", w);
    before_show(w); return f(w);
}
void gtk_window_present(void *w) {
    static void (*f)(void *); if (!f) f = dlsym(RTLD_NEXT, "gtk_window_present");
    before_show(w); f(w);
}
void gtk_window_present_with_time(void *w, unsigned t) {
    static void (*f)(void *, unsigned); if (!f) f = dlsym(RTLD_NEXT, "gtk_window_present_with_time");
    before_show(w); f(w, t);
}

/* GdkGeometry from gdk/gdkwindow.h */
typedef struct {
    int min_width, min_height, max_width, max_height, base_width, base_height, width_inc, height_inc;
    double min_aspect, max_aspect;
    int win_gravity;
} Geometry;
enum { HINT_MIN_SIZE = 1 << 1, HINT_MAX_SIZE = 1 << 2 };

void gtk_window_set_geometry_hints(void *w, void *geometry_widget, Geometry *g, int mask) {
    static void (*f)(void *, void *, Geometry *, int);
    if (!f) f = dlsym(RTLD_NEXT, "gtk_window_set_geometry_hints");
    if (debug < 0) debug = getenv("LO_SHIM_DEBUG") != NULL;
    Rect a; screen_area(&a);
    if (debug && g)
        fprintf(stderr, "lo-shim: %p hints mask %x min %dx%d max %dx%d\n", w, mask, g->min_width, g->min_height,
                g->max_width, g->max_height);
    if (g && a.width > 0 && (mask & HINT_MIN_SIZE)) {
        Geometry c = *g;
        int maxw = a.width, maxh = a.height - MARGIN_H;
        if (c.min_width > maxw) c.min_width = maxw;
        if (c.min_height > maxh) c.min_height = maxh;
        if (debug && (c.min_width != g->min_width || c.min_height != g->min_height))
            fprintf(stderr, "lo-shim: %p minimum %dx%d capped to %dx%d\n", w, g->min_width, g->min_height,
                    c.min_width, c.min_height);
        f(w, geometry_widget, &c, mask);
        return;
    }
    f(w, geometry_widget, g, mask);
}

static void on_map_log(void *w, void *unused) {
    (void)unused;
    Req min, nat;
    CALL(gtk, void, gtk_widget_get_preferred_size, (void *, Req *, Req *))(w, &min, &nat);
    int ww = 0, hh = 0;
    CALL(gtk, void, gtk_window_get_size, (void *, int *, int *))(w, &ww, &hh);
    fprintf(stderr, "lo-shim: window %p mapped: size %dx%d min %dx%d nat %dx%d resizable %d\n", w, ww, hh,
            min.width, min.height, nat.width, nat.height, CALL(gtk, int, gtk_window_get_resizable, (void *))(w));
}

static const char FRAME[] = "lo-shim-frame";
/* Depth-first search, including internal children, for the first widget of a type or style class. */
static void *found;
static const char *want_type, *want_class;
static void find_cb(void *widget, void *unused) {
    (void)unused;
    if (found) return;
    if (want_type && is_a(widget, want_type)) { found = widget; return; }
    if (want_class && CALL(gtk, int, gtk_style_context_has_class, (void *, const char *))(
            CALL(gtk, void *, gtk_widget_get_style_context, (void *))(widget), want_class)) { found = widget; return; }
    if (is_a(widget, "gtk_container_get_type"))
        CALL(gtk, void, gtk_container_forall, (void *, void *, void *))(widget, (void *)find_cb, NULL);
}
static void *find(void *root, const char *type_fn, const char *cls) {
    found = NULL; want_type = type_fn; want_class = cls;
    CALL(gtk, void, gtk_container_forall, (void *, void *, void *))(root, (void *)find_cb, NULL);
    return found;
}

/* Put `child` in a scrolled area at the same place in its parent box. */
static void scroll_in_place(void *child) {
    void *parent = CALL(gtk, void *, gtk_widget_get_parent, (void *))(child);
    if (!parent || !is_a(parent, "gtk_box_get_type")) return;
    int pos = 0, expand = 1, fill = 1, padding = 0;
    CALL(gtk, void, gtk_container_child_get, (void *, void *, ...))(parent, child, "position", &pos, "expand", &expand, "fill", &fill, "padding", &padding, NULL);
    void *sw = CALL(gtk, void *, gtk_scrolled_window_new, (void *, void *))(NULL, NULL);
    CALL(gtk, void, gtk_scrolled_window_set_policy, (void *, int, int))(sw, 1, 1);
    CALL(gob, void *, g_object_ref, (void *))(child);
    CALL(gtk, void, gtk_container_remove, (void *, void *))(parent, child);
    CALL(gtk, void, gtk_container_add, (void *, void *))(sw, child);
    CALL(gob, void, g_object_unref, (void *))(child);
    CALL(gtk, void, gtk_box_pack_start, (void *, void *, int, int, unsigned))(parent, sw, 1, 1, (unsigned)padding);
    CALL(gtk, void, gtk_box_reorder_child, (void *, void *, int))(parent, sw, pos);
    CALL(gtk, void, gtk_widget_show, (void *))(sw);
}

static void fit_assistant(void *w) {
    void *side = find(w, NULL, "sidebar");
    if (side) CALL(gtk, void, gtk_widget_hide, (void *))(side);
    Rect a; screen_area(&a);
    Req min, nat;
    CALL(gtk, void, gtk_widget_get_preferred_size, (void *, Req *, Req *))(w, &min, &nat);
    if (a.width > 0 && (min.width > a.width - MARGIN_W || min.height > a.height - MARGIN_H)) {
        void *pages = find(w, "gtk_notebook_get_type", NULL);
        if (pages) scroll_in_place(pages);
    }
    if (debug) {
        Req m2, n2;
        CALL(gtk, void, gtk_widget_get_preferred_size, (void *, Req *, Req *))(w, &m2, &n2);
        fprintf(stderr, "lo-shim: %p wizard min %dx%d -> %dx%d (sidebar %s)\n", w, min.width, min.height,
                m2.width, m2.height, side ? "hidden" : "not found");
    }
}

/* A document window or wizard (not a dialog, not the fixed-size splash): maximise it before it appears. */
/* Scroll everything in `box` except `keep` (a button row), which stays visible below. */
static void scroll_all_but(void *box, void *keep) {
    void *sw = CALL(gtk, void *, gtk_scrolled_window_new, (void *, void *))(NULL, NULL);
    CALL(gtk, void, gtk_scrolled_window_set_policy, (void *, int, int))(sw, 1, 1);
    void *inner = CALL(gtk, void *, gtk_box_new, (int, int))(
        CALL(gtk, int, gtk_orientable_get_orientation, (void *))(box), 0);
    CALL(gtk, void, gtk_container_add, (void *, void *))(sw, inner);
    GList *kids = CALL(gtk, GList *, gtk_container_get_children, (void *))(box);
    for (GList *l = kids; l; l = l->next)
        if (l->data != keep) move_child(l->data, box, inner);
    CALL(glib, void, g_list_free, (GList *))(kids);
    CALL(gtk, void, gtk_box_pack_start, (void *, void *, int, int, unsigned))(box, sw, 1, 1, 0);
    CALL(gtk, void, gtk_box_reorder_child, (void *, void *, int))(box, sw, 0);
    CALL(gtk, void, gtk_widget_show_all, (void *))(sw);
}

/* Once a parentless window is on screen: if its contents are bigger than the screen (Base's
 * Database Wizard is a plain window built from GTK widgets), make them scroll. */
static void on_frame_map(void *w, void *unused) {
    (void)unused;
    if (get(w, DONE)) return;
    set(w, DONE, (void *)1);
    void *top = CALL(gtk, void *, gtk_bin_get_child, (void *))(w);
    if (!top) return;
    Rect a; screen_area(&a);
    Req min, nat;
    CALL(gtk, void, gtk_widget_get_preferred_size, (void *, Req *, Req *))(top, &min, &nat);
    if (a.width <= 0 || (min.width <= a.width - MARGIN_W && min.height <= a.height - MARGIN_H)) return;
    void *buttons = find(w, "gtk_button_box_get_type", NULL);
    void *box = buttons ? CALL(gtk, void *, gtk_widget_get_parent, (void *))(buttons) : NULL;
    if (box && is_a(box, "gtk_box_get_type")) scroll_all_but(box, buttons);
    else wrap_in_scroller(w);
    if (debug) {
        Req m2, n2;
        CALL(gtk, void, gtk_widget_get_preferred_size, (void *, Req *, Req *))(top, &m2, &n2);
        fprintf(stderr, "lo-shim: %p window contents %dx%d bigger than screen: scrolled (%s), now %dx%d\n", w,
                min.width, min.height, box ? "buttons kept" : "all", m2.width, m2.height);
    }
}

static void on_frame_realize(void *w, void *unused) {
    (void)unused;
    if (get(w, KEY) || get(w, FRAME) || !CALL(gtk, int, gtk_window_get_resizable, (void *))(w)) return;
    set(w, FRAME, (void *)1);
    if (is_a(w, "gtk_assistant_get_type")) { fit_assistant(w); set(w, DONE, (void *)1); }
    else CALL(gob, unsigned long, g_signal_connect_data, (void *, const char *, void *, void *, void *, int))(
        w, "map", (void *)on_frame_map, NULL, NULL, 1);
    CALL(gtk, void, gtk_window_maximize, (void *))(w);
    if (debug) fprintf(stderr, "lo-shim: %p document window maximised\n", w);
}

void *gtk_window_new(int type) {
    static void *(*f)(int);
    if (!f) f = dlsym(RTLD_NEXT, "gtk_window_new");
    if (debug < 0) debug = getenv("LO_SHIM_DEBUG") != NULL;
    void *w = f(type);
    if (w) set(w, "lo-shim-new", (void *)1); /* a document window or popup, not a .ui window */
    if (!w || type != 0 /* GTK_WINDOW_TOPLEVEL */) return w;
    unsigned long (*connect)(void *, const char *, void *, void *, void *, int) = gob("g_signal_connect_data");
    connect(w, "realize", (void *)on_frame_realize, NULL, NULL, 1);
    if (debug) connect(w, "map", (void *)on_map_log, NULL, NULL, 1);
    return w;
}

void gtk_window_unmaximize(void *w) {
    static void (*f)(void *);
    if (!f) f = dlsym(RTLD_NEXT, "gtk_window_unmaximize");
    if (w && get(w, FRAME)) return; /* document windows stay maximised on the phone */
    f(w);
}

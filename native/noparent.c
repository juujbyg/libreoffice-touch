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

static int scroll_to_left(void *sw) {
    void *h = CALL(gtk, void *, gtk_scrolled_window_get_hadjustment, (void *))(sw);
    if (h) CALL(gtk, void, gtk_adjustment_set_value, (void *, double))(h, CALL(gtk, double, gtk_adjustment_get_lower, (void *))(h));
    CALL(gob, void, g_object_unref, (void *))(sw);
    return 0;
}
static void on_scroller_map(void *sw, void *unused) {
    (void)unused;
    /* After LibreOffice has set the focus, which scrolls the viewport to the focused widget. */
    CALL(gob, void *, g_object_ref, (void *))(sw);
    CALL(glib, unsigned, g_timeout_add, (unsigned, int (*)(void *), void *))(150, scroll_to_left, sw);
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
        /* GTK keeps the button box (the action area) inside an "action box" of its own, and that
         * box is the child of `top`: leaving only the button box out moved the buttons into the
         * scrolled area too, where OK could end up past the right edge (Calc's Text Import). */
        action = CALL(gtk, void *, gtk_dialog_get_action_area, (void *))(w);
        void *action_box = action ? CALL(gtk, void *, gtk_widget_get_parent, (void *))(action) : NULL;
        GList *kids = CALL(gtk, GList *, gtk_container_get_children, (void *))(top);
        for (GList *l = kids; l; l = l->next)
            if (l->data != action && l->data != action_box) move_child(l->data, top, inner);
        CALL(glib, void, g_list_free, (GList *))(kids);
        CALL(gtk, void, gtk_box_pack_start, (void *, void *, int, int, unsigned))(top, sw, 1, 1, 0);
        CALL(gtk, void, gtk_box_reorder_child, (void *, void *, int))(top, sw, 0);
    } else {
        move_child(top, w, inner);
        CALL(gtk, void, gtk_container_add, (void *, void *))(w, sw);
    }
    /* Only the new scroller and box: the moved widgets keep their own visibility. show_all here
     * revealed what LibreOffice keeps hidden, such as the Insert Image and Save options that it
     * builds into every Open dialog. */
    CALL(gtk, void, gtk_widget_show, (void *))(inner);
    CALL(gtk, void, gtk_widget_show, (void *))(sw);
    /* A sheet wider than the screen starts at its left edge, where the labels are. Without this
     * the viewport followed the focused widget (often a button at the right) and opened halfway. */
    CALL(gob, unsigned long, g_signal_connect_data, (void *, const char *, void *, void *, void *, int))(
        sw, "map", (void *)on_scroller_map, NULL, NULL, 0);
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
static void file_dialog(void *w);
static void before_show(void *w) {
    if (!w) return;
    if (debug < 0) debug = getenv("LO_SHIM_DEBUG") != NULL;
    if (is_a(w, "gtk_file_chooser_dialog_get_type")) file_dialog(w);
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
    /* Only the new scroller and box: the moved widgets keep their own visibility. show_all here
     * revealed what LibreOffice keeps hidden, such as the Insert Image and Save options that it
     * builds into every Open dialog. */
    CALL(gtk, void, gtk_widget_show, (void *))(inner);
    CALL(gtk, void, gtk_widget_show, (void *))(sw);
    /* A sheet wider than the screen starts at its left edge, where the labels are. Without this
     * the viewport followed the focused widget (often a button at the right) and opened halfway. */
    CALL(gob, unsigned long, g_signal_connect_data, (void *, const char *, void *, void *, void *, int))(
        sw, "map", (void *)on_scroller_map, NULL, NULL, 0);
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

static void trace_window(void *w);
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
    trace_window(w);
    return w;
}

void gtk_window_unmaximize(void *w) {
    static void (*f)(void *);
    if (!f) f = dlsym(RTLD_NEXT, "gtk_window_unmaximize");
    if (w && get(w, FRAME)) return; /* document windows stay maximised on the phone */
    f(w);
}

/* ---- Back arrow --------------------------------------------------------------------------------
 * On the phone, the only way out of a document was the small close button at the right end of the
 * menu bar. Every menu bar (Writer, Calc, Impress, Draw, Math, Base) gets a large back arrow at its
 * left end. It closes the window the way the system would: LibreOffice asks about unsaved changes,
 * and quits after its last window. Once the window is gone (or has turned into LibreOffice's own
 * Start Center), the app's start screen is brought back through the URL dispatcher.
 * LO_SHIM_TEST_BACK=<seconds> presses the arrow by itself, for tests; with LO_SHIM_SNAPSHOT=<file.png>
 * the window is drawn into that file a second before (screen capture doesn't work on the phone). */
static void *gio(const char *s) { return dlsym(lib("libgio-2.0.so.0"), s); }
static void *cairo(const char *s) { return dlsym(lib("libcairo.so.2"), s); }
static const char BACK[] = "lo-shim-back";

static void show_start_screen(void) {
    /* The package from the app id Lomiri gives each app (libreoffice.juujbyg_writer_0.4.0). */
    const char *app = getenv("APP_ID");
    char url[200];
    size_t n = app ? strcspn(app, "_") : 0;
    if (!n || n > 100) return;
    snprintf(url, sizeof url, "appid://%.*s/a-libreoffice/current-user-version", (int)n, app);
    void *bus = CALL(gio, void *, g_bus_get_sync, (int, void *, void **))(2 /* G_BUS_TYPE_SESSION */, NULL, NULL);
    if (!bus) return;
    void *args = CALL(glib, void *, g_variant_new, (const char *, ...))("(ss)", url, "");
    void *err = NULL;
    void *reply = CALL(gio, void *, g_dbus_connection_call_sync, (void *, const char *, const char *, const char *,
        const char *, void *, void *, int, int, void *, void **))(bus, "com.lomiri.URLDispatcher",
        "/com/lomiri/URLDispatcher", "com.lomiri.URLDispatcher", "DispatchURL", args, NULL, 0, 5000, NULL, &err);
    if (reply) CALL(glib, void, g_variant_unref, (void *))(reply);
    if (err) {
        if (debug) fprintf(stderr, "lo-shim: start screen: %s\n", ((struct { unsigned d; int c; char *m; } *)err)->m);
        CALL(glib, void, g_error_free, (void *))(err);
    } else if (debug) fprintf(stderr, "lo-shim: start screen asked for (%s)\n", url);
    CALL(gob, void, g_object_unref, (void *))(bus);
}

static void on_back_window_gone(void *w, void *unused) { (void)w; (void)unused; show_start_screen(); }

static int close_again(void *w) {
    CALL(gtk, void, gtk_window_close, (void *))(w);
    CALL(gob, void, g_object_unref, (void *))(w);
    return 0;
}

static void on_back(void *button, void *unused) {
    (void)unused;
    void *w = CALL(gtk, void *, gtk_widget_get_toplevel, (void *))(button);
    if (!w || !is_a(w, "gtk_window_get_type")) return;
    if (!get(w, BACK)) {
        set(w, BACK, (void *)1);
        CALL(gob, unsigned long, g_signal_connect_data, (void *, const char *, void *, void *, void *, int))(
            w, "destroy", (void *)on_back_window_gone, NULL, NULL, 0);
    }
    if (debug) fprintf(stderr, "lo-shim: back pressed in %p\n", w);
    CALL(gtk, void, gtk_window_close, (void *))(w);
}

/* Closing the last document can leave LibreOffice's Start Center in the same window instead: then
 * close that too (which quits LibreOffice) and show the app's start screen. */
void gtk_window_set_title(void *w, const char *title) {
    static void (*f)(void *, const char *);
    if (!f) f = dlsym(RTLD_NEXT, "gtk_window_set_title");
    f(w, title);
    if (w && title && get(w, BACK) && !strcmp(title, "LibreOffice")) {
        set(w, BACK, NULL);
        show_start_screen();
        CALL(gob, void *, g_object_ref, (void *))(w);
        CALL(glib, unsigned, g_idle_add, (int (*)(void *), void *))(close_again, w);
    }
}

static int snapshot_window(void *w, const char *path);
static int snapshot(void *button) {
    snapshot_window(CALL(gtk, void *, gtk_widget_get_toplevel, (void *))(button), getenv("LO_SHIM_SNAPSHOT"));
    CALL(gob, void, g_object_unref, (void *))(button);
    return 0;
}

static int press_back(void *button) {
    CALL(gtk, void, gtk_button_clicked, (void *))(button);
    CALL(gob, void, g_object_unref, (void *))(button);
    return 0;
}

static int add_back(void *menubar) {
    void *grid = NULL;
    for (void *p = CALL(gtk, void *, gtk_widget_get_parent, (void *))(menubar); p;
         p = CALL(gtk, void *, gtk_widget_get_parent, (void *))(p))
        if (is_a(p, "gtk_grid_get_type")) { grid = p; break; }
    /* LibreOffice's menu bar row: a grid with the menu (in a scrolled area) at column 0 and its
     * close button at column 1. The arrow goes at column -1. */
    if (grid && !get(grid, BACK)) {
        set(grid, BACK, (void *)1);
        void *b = CALL(gtk, void *, gtk_button_new_from_icon_name, (const char *, int))("go-previous-symbolic", 3);
        void *img = CALL(gtk, void *, gtk_button_get_image, (void *))(b);
        if (img) CALL(gtk, void, gtk_image_set_pixel_size, (void *, int))(img, 48);
        add_class(b, "lo-back");
        CALL(gtk, void, gtk_widget_set_tooltip_text, (void *, const char *))(b, "Back to the LibreOffice start screen");
        CALL(gtk, void, gtk_widget_set_valign, (void *, int))(b, 3 /* GTK_ALIGN_CENTER */);
        CALL(gob, unsigned long, g_signal_connect_data, (void *, const char *, void *, void *, void *, int))(
            b, "clicked", (void *)on_back, NULL, NULL, 0);
        CALL(gtk, void, gtk_grid_attach, (void *, void *, int, int, int, int))(grid, b, -1, 0, 1, 1);
        CALL(gtk, void, gtk_widget_show, (void *))(b);
        if (debug) fprintf(stderr, "lo-shim: back arrow added to %p (%s)\n", grid,
                           CALL(gob, const char *, g_type_name_from_instance, (void *))(
                               CALL(gtk, void *, gtk_widget_get_parent, (void *))(grid)));
        const char *test = getenv("LO_SHIM_TEST_BACK");
        if (test && atoi(test) > 1) {
            CALL(gob, void *, g_object_ref, (void *))(b);
            CALL(glib, unsigned, g_timeout_add_seconds, (unsigned, int (*)(void *), void *))((unsigned)atoi(test) - 1, snapshot, b);
        }
        if (test && atoi(test) > 0) {
            CALL(gob, void *, g_object_ref, (void *))(b);
            CALL(glib, unsigned, g_timeout_add_seconds, (unsigned, int (*)(void *), void *))((unsigned)atoi(test), press_back, b);
        }
    }
    CALL(gob, void, g_object_unref, (void *))(menubar);
    return 0; /* G_SOURCE_REMOVE */
}

/* LibreOffice's native menu bar (GtkSalMenu) is made from a menu model; the arrow is added once
 * the menu bar sits in its row. */
void *gtk_menu_bar_new_from_model(void *model) {
    static void *(*f)(void *);
    if (!f) f = dlsym(RTLD_NEXT, "gtk_menu_bar_new_from_model");
    if (debug < 0) debug = getenv("LO_SHIM_DEBUG") != NULL;
    void *m = f(model);
    if (m) {
        CALL(gob, void *, g_object_ref, (void *))(m);
        CALL(glib, unsigned, g_idle_add, (int (*)(void *), void *))(add_back, m);
    }
    return m;
}

/* ---- Open and Save dialogs -------------------------------------------------------------------
 * The sandbox can't read the phone's folders, and bin/office makes HOME the app's documents folder,
 * so GTK's "Home" is the one place LibreOffice can use. The sidebar loses Recent (GTK's list is
 * outside the sandbox) and Other Locations (mounts it can't read). An Open dialog gets a "Phone
 * files…" button: it leaves this suite app's name in $XDG_DATA_HOME/<package>/pick-request and
 * brings up the start screen (start/hub.py, pick_request), which offers Content Hub's picker, copies
 * the file into the documents folder, writes its path to .../picked and brings this app back. The
 * dialog, still open, then selects that file.
 * LO_SHIM_TEST_DIALOG=<seconds> (tests): draws the dialog into LO_SHIM_SNAPSHOT, presses the button
 * without bringing up the start screen, selects the file named by LO_SHIM_TEST_PICKED as if it had
 * been picked, draws the dialog again into <LO_SHIM_SNAPSHOT>.after.png and cancels it. A Save
 * dialog is only drawn into <LO_SHIM_SNAPSHOT>.after.png and cancelled. */
static const char FILES[] = "lo-shim-files", PICKING[] = "lo-shim-picking";

/* $XDG_DATA_HOME/<package>/<name>, and the suite app's name (writer...), from APP_ID. */
static int app_file(char *out, size_t size, const char *name, char *app, size_t app_size) {
    const char *id = getenv("APP_ID"), *data = getenv("XDG_DATA_HOME");
    size_t n = id ? strcspn(id, "_") : 0;
    if (!data || !n || !id[n] || n > 100) return 0;
    size_t m = strcspn(id + n + 1, "_");
    if (!m || m >= app_size) return 0;
    snprintf(app, app_size, "%.*s", (int)m, id + n + 1);
    return snprintf(out, size, "%s/%.*s/%s", data, (int)n, id, name) < (int)size;
}

static int snapshot_window(void *w, const char *path) {
    int ww = CALL(gtk, int, gtk_widget_get_allocated_width, (void *))(w);
    int hh = CALL(gtk, int, gtk_widget_get_allocated_height, (void *))(w);
    if (!path || ww <= 0 || hh <= 0) return 0;
    void *surface = CALL(cairo, void *, cairo_image_surface_create, (int, int, int))(0 /* ARGB32 */, ww, hh);
    void *cr = CALL(cairo, void *, cairo_create, (void *))(surface);
    CALL(gtk, void, gtk_widget_draw, (void *, void *))(w, cr);
    CALL(cairo, int, cairo_surface_write_to_png, (void *, const char *))(surface, path);
    CALL(cairo, void, cairo_destroy, (void *))(cr);
    CALL(cairo, void, cairo_surface_destroy, (void *))(surface);
    fprintf(stderr, "lo-shim: snapshot %dx%d saved\n", ww, hh);
    return 1;
}

static void on_phone_files(void *button, void *unused) {
    (void)unused;
    void *w = CALL(gtk, void *, gtk_widget_get_toplevel, (void *))(button);
    char path[700], app[64];
    if (!app_file(path, sizeof path, "picked", app, sizeof app)) return;
    unlink(path); /* a file picked for an earlier dialog */
    app_file(path, sizeof path, "pick-request", app, sizeof app);
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "lo-shim: phone files: can't write the request\n"); return; }
    fprintf(f, "%s\n", app);
    fclose(f);
    set(w, PICKING, (void *)1);
    if (debug) fprintf(stderr, "lo-shim: phone files asked for %s\n", app);
    if (!getenv("LO_SHIM_TEST_DIALOG")) show_start_screen();
}

/* Back from the start screen: select the file it copied in. Only a file in the documents folder. */
static int select_picked(void *w) {
    char path[700], app[64], picked[1024] = "", folder[700];
    if (get(w, PICKING) && app_file(path, sizeof path, "picked", app, sizeof app)) {
        FILE *f = fopen(path, "r");
        if (f) {
            if (!fgets(picked, sizeof picked, f)) picked[0] = 0;
            fclose(f);
            unlink(path);
            picked[strcspn(picked, "\n")] = 0;
            app_file(folder, sizeof folder, "Documents/", app, sizeof app);
            if (!strncmp(picked, folder, strlen(folder)) && !strstr(picked, "/..") && !strchr(picked + strlen(folder), '/')) {
                set(w, PICKING, NULL);
                CALL(gtk, int, gtk_file_chooser_set_filename, (void *, const char *))(w, picked);
                if (debug) fprintf(stderr, "lo-shim: phone files: selected %s\n", picked);
            }
        }
    }
    CALL(gob, void, g_object_unref, (void *))(w);
    return 0;
}

/* The dialog's app is in front again. The copy's folder listing may still be catching up (the app
 * was suspended while the start screen was up), so the file is selected a little later. */
static void on_dialog_active(void *w, void *pspec, void *unused) {
    (void)pspec; (void)unused;
    if (!get(w, PICKING) || !CALL(gtk, int, gtk_window_is_active, (void *))(w)) return;
    CALL(gob, void *, g_object_ref, (void *))(w);
    CALL(glib, unsigned, g_timeout_add, (unsigned, int (*)(void *), void *))(700, select_picked, w);
}

static int test_after(void *w) {
    char path[600], *chosen = CALL(gtk, char *, gtk_file_chooser_get_filename, (void *))(w);
    fprintf(stderr, "lo-shim: test dialog: chosen %s\n", chosen ? chosen : "(none)");
    CALL(glib, void, g_free, (void *))(chosen);
    const char *snap = getenv("LO_SHIM_SNAPSHOT");
    if (snap) { snprintf(path, sizeof path, "%s.after.png", snap); snapshot_window(w, path); }
    CALL(gtk, void, gtk_dialog_response, (void *, int))(w, -6 /* GTK_RESPONSE_CANCEL */);
    CALL(gob, void, g_object_unref, (void *))(w);
    return 0;
}
static int test_pick(void *w) {
    char path[700], app[64];
    const char *file = getenv("LO_SHIM_TEST_PICKED");
    if (file && app_file(path, sizeof path, "picked", app, sizeof app)) {
        FILE *f = fopen(path, "w");
        if (f) { fprintf(f, "%s\n", file); fclose(f); }
    }
    CALL(gob, void *, g_object_ref, (void *))(w);
    select_picked(w); /* drops that reference */
    CALL(gob, void *, g_object_ref, (void *))(w); /* test_after drops this one */
    CALL(glib, unsigned, g_timeout_add_seconds, (unsigned, int (*)(void *), void *))(2, test_after, w);
    return 0;
}
static int test_press(void *button) {
    void *w = CALL(gtk, void *, gtk_widget_get_toplevel, (void *))(button);
    snapshot_window(w, getenv("LO_SHIM_SNAPSHOT"));
    CALL(gtk, void, gtk_button_clicked, (void *))(button);
    char path[700], app[64], line[64] = "";
    if (app_file(path, sizeof path, "pick-request", app, sizeof app)) {
        FILE *f = fopen(path, "r");
        if (f) { if (!fgets(line, sizeof line, f)) line[0] = 0; fclose(f); }
    }
    fprintf(stderr, "lo-shim: test dialog: request %s", line[0] ? line : "missing\n");
    CALL(glib, unsigned, g_timeout_add_seconds, (unsigned, int (*)(void *), void *))(1, test_pick, w);
    CALL(gob, void, g_object_unref, (void *))(button);
    return 0;
}

/* A sidebar row for the phone's root folder ("Computer"): the sandbox can't read what's in it. The
 * rows have a "uri" property (a GTK-private type, so it's looked up by name), and the sidebar remakes
 * them whenever its places change, so each row is checked as it's added. */
static void hide_root_row(void *list, void *row, void *unused) {
    (void)list; (void)unused;
    if (!CALL(gob, void *, g_object_class_find_property, (void *, const char *))(*(void **)row, "uri")) return;
    char *uri = NULL;
    CALL(gob, void, g_object_get, (void *, const char *, ...))(row, "uri", &uri, NULL);
    if (uri && !strcmp(uri, "file:///")) {
        /* GTK shows each new row with gtk_widget_show_all right after adding it, which no_show_all stops. */
        CALL(gtk, void, gtk_widget_set_no_show_all, (void *, int))(row, 1);
        CALL(gtk, void, gtk_widget_hide, (void *))(row);
    }
    if (debug) fprintf(stderr, "lo-shim: sidebar row %s\n", uri ? uri : "(no uri)");
    CALL(glib, void, g_free, (void *))(uri);
}
static void hide_root_rows(void *row, void *list) { hide_root_row(list, row, NULL); }

static void file_dialog(void *w) {
    if (get(w, FILES)) return;
    set(w, FILES, (void *)1);
    void *sidebar = find(w, "gtk_places_sidebar_get_type", NULL);
    if (sidebar) {
        /* Recent and the trash are outside the sandbox, and Other Locations lists mounts it can't read. */
        CALL(gtk, void, gtk_places_sidebar_set_show_recent, (void *, int))(sidebar, 0);
        CALL(gtk, void, gtk_places_sidebar_set_show_trash, (void *, int))(sidebar, 0);
        CALL(gtk, void, gtk_places_sidebar_set_show_other_locations, (void *, int))(sidebar, 0);
        void *list = find(sidebar, "gtk_list_box_get_type", NULL);
        if (list) {
            CALL(gtk, void, gtk_container_foreach, (void *, void *, void *))(list, (void *)hide_root_rows, list);
            CALL(gob, unsigned long, g_signal_connect_data, (void *, const char *, void *, void *, void *, int))(
                list, "add", (void *)hide_root_row, NULL, NULL, 1 /* G_CONNECT_AFTER */);
        }
    }
    char path[700], app[64];
    const char *test = getenv("LO_SHIM_TEST_DIALOG");
    if (CALL(gtk, int, gtk_file_chooser_get_action, (void *))(w) != 0 /* GTK_FILE_CHOOSER_ACTION_OPEN */) {
        /* Save dialogs have no button; the test only draws them (into <LO_SHIM_SNAPSHOT>.after.png) and cancels. */
        if (test && atoi(test) > 0) {
            CALL(gob, void *, g_object_ref, (void *))(w);
            CALL(glib, unsigned, g_timeout_add_seconds, (unsigned, int (*)(void *), void *))((unsigned)atoi(test), test_after, w);
        }
        return;
    }
    if (!app_file(path, sizeof path, "pick-request", app, sizeof app)) return;
    void *b = CALL(gtk, void *, gtk_button_new_with_label, (const char *))("Phone files…");
    CALL(gtk, void, gtk_widget_set_tooltip_text, (void *, const char *))(b,
        "Pick a file from the phone (Files, Downloads…): it's copied into LibreOffice's documents and selected here");
    add_class(b, "lo-phone-files");
    CALL(gob, unsigned long, g_signal_connect_data, (void *, const char *, void *, void *, void *, int))(
        b, "clicked", (void *)on_phone_files, NULL, NULL, 0);
    void *bar = CALL(gtk, void *, gtk_dialog_get_header_bar, (void *))(w);
    if (bar) CALL(gtk, void, gtk_header_bar_pack_start, (void *, void *))(bar, b);
    else {
        void *area = CALL(gtk, void *, gtk_dialog_get_action_area, (void *))(w);
        CALL(gtk, void, gtk_box_pack_start, (void *, void *, int, int, unsigned))(area, b, 0, 0, 0);
        if (is_a(area, "gtk_button_box_get_type"))
            CALL(gtk, void, gtk_button_box_set_child_secondary, (void *, void *, int))(area, b, 1);
    }
    CALL(gtk, void, gtk_widget_show, (void *))(b);
    CALL(gob, unsigned long, g_signal_connect_data, (void *, const char *, void *, void *, void *, int))(
        w, "notify::is-active", (void *)on_dialog_active, NULL, NULL, 0);
    if (debug) fprintf(stderr, "lo-shim: phone files button added to %p (%s)\n", w, bar ? "header bar" : "button row");
    if (test && atoi(test) > 0) {
        CALL(gob, void *, g_object_ref, (void *))(b);
        CALL(glib, unsigned, g_timeout_add_seconds, (unsigned, int (*)(void *), void *))((unsigned)atoi(test), test_press, b);
    }
}

/* ---- On-screen keyboard -----------------------------------------------------------------------
 * LibreOffice drives Lomiri's keyboard through a GtkIMContext (the Maliit GTK module in lib/). The
 * module shows the keyboard on every focus_in, and LibreOffice calls focus_in whenever its document
 * gets the focus back (a menu or dialog closed, the window focused), so the keyboard popped up after
 * every menu. Here the keyboard comes up only for a tap where the text goes (the page, or a text
 * field), and dragging there puts it away. Swiping the keyboard down (Lomiri's gesture) also hides
 * it: the module then clears the window's focus widget (gtk_window_set_focus NULL), so the next tap
 * gives the focus back to the widget that had it.
 * With LO_SHIM_IM_TRACE=1, or a file ~/.cache/libreoffice.juujbyg/im-trace, every call is logged
 * with a timestamp. */
#include <time.h>
static int im_trace = -1;
static long now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000 + t.tv_nsec / 1000000; }
static int trace_on(void) {
    char path[512]; const char *cache = getenv("XDG_CACHE_HOME"); /* bin/office moves HOME */
    if (getenv("LO_SHIM_IM_TRACE")) return 1;
    snprintf(path, sizeof path, "%s/libreoffice.juujbyg/im-trace", cache ? cache : "");
    return access(path, F_OK) == 0;
}
#define TRACE(...) do { if (im_trace < 0) im_trace = trace_on(); \
    if (im_trace) { fprintf(stderr, "im %ld ", now_ms() % 100000); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static const char CLIENT[] = "lo-shim-client";
static void *kbd_ctx;      /* LibreOffice's input context that last had focus */
static void *kbd_widget;   /* the widget that had the focus before a swipe */
static int kbd_shown;      /* the keyboard is up for kbd_ctx */
static int kbd_forcing;    /* our own focus_in, after a tap: let it through */

static void *toplevel_of(void *ctx) {
    void *gdkwin = ctx ? get(ctx, CLIENT) : NULL, *widget = NULL;
    if (!gdkwin) return NULL;
    CALL(gdk, void, gdk_window_get_user_data, (void *, void **))(gdkwin, &widget);
    return widget ? CALL(gtk, void *, gtk_widget_get_toplevel, (void *))(widget) : NULL;
}
static void forget_widget(void *data, void *gone) { (void)data; if (gone == kbd_widget) kbd_widget = NULL; }

typedef struct { int x, y, width, height; } ImRect;
/* The Maliit module shows the keyboard on every focus_in, and LibreOffice calls focus_in whenever
 * its document gets the focus back: after a menu or a dialog closes, after the window is focused,
 * after the keyboard itself is hidden. So LibreOffice's own focus_in waits, and the keyboard comes up
 * when the user taps where the text goes (on_any_event). */
void gtk_im_context_focus_in(void *c) {
    static void (*f)(void *); if (!f) f = dlsym(RTLD_NEXT, "gtk_im_context_focus_in");
    if (!get(c, CLIENT)) { TRACE("focus_in %p (no window)", c); f(c); return; }
    if (c != kbd_ctx) kbd_shown = 0;
    kbd_ctx = c;
    if (!kbd_forcing && !kbd_shown) { TRACE("focus_in %p waits for a tap", c); return; }
    TRACE("focus_in %p", c); f(c);
    kbd_shown = 1;
}
void gtk_im_context_focus_out(void *c) {
    static void (*f)(void *); if (!f) f = dlsym(RTLD_NEXT, "gtk_im_context_focus_out");
    TRACE("focus_out %p", c); f(c);
    if (c == kbd_ctx) kbd_shown = 0; /* the module hides the keyboard on focus_out */
}
void gtk_im_context_reset(void *c) {
    static void (*f)(void *); if (!f) f = dlsym(RTLD_NEXT, "gtk_im_context_reset");
    TRACE("reset %p", c); f(c);
}
void gtk_im_context_set_client_window(void *c, void *w) {
    static void (*f)(void *, void *); if (!f) f = dlsym(RTLD_NEXT, "gtk_im_context_set_client_window");
    TRACE("client_window %p %p", c, w); f(c, w);
    if (c) set(c, CLIENT, w);
    if (!w && c == kbd_ctx) { kbd_ctx = NULL; kbd_shown = 0; }
}
void gtk_im_context_set_cursor_location(void *c, const ImRect *r) {
    static void (*f)(void *, const ImRect *); if (!f) f = dlsym(RTLD_NEXT, "gtk_im_context_set_cursor_location");
    TRACE("cursor %p %d,%d %dx%d", c, r ? r->x : -1, r ? r->y : -1, r ? r->width : -1, r ? r->height : -1); f(c, r);
}
void gtk_im_context_set_surrounding(void *c, const char *text, int len, int cursor) {
    static void (*f)(void *, const char *, int, int); if (!f) f = dlsym(RTLD_NEXT, "gtk_im_context_set_surrounding");
    TRACE("surrounding %p len=%d cursor=%d", c, len, cursor); f(c, text, len, cursor);
}
/* The Maliit module calls this with NULL when the keyboard is swiped away. */
void gtk_window_set_focus(void *win, void *widget) {
    static void (*f)(void *, void *); if (!f) f = dlsym(RTLD_NEXT, "gtk_window_set_focus");
    void *before = win ? CALL(gtk, void *, gtk_window_get_focus, (void *))(win) : NULL;
    TRACE("window_set_focus %p %p (was %p)", win, widget, before);
    f(win, widget);
    if (!widget && win && kbd_ctx && toplevel_of(kbd_ctx) == win) {
        kbd_shown = 0;
        if (kbd_widget) CALL(gob, void, g_object_weak_unref, (void *, void *, void *))(kbd_widget, (void *)forget_widget, NULL);
        kbd_widget = before;
        if (kbd_widget) CALL(gob, void, g_object_weak_ref, (void *, void *, void *))(kbd_widget, (void *)forget_widget, NULL);
        TRACE("keyboard swiped away");
    }
}

/* Touches anywhere in the process, through an emission hook on GtkWidget::event (it runs even
 * when LibreOffice handles the event). Only touches on the input context's own window count: the
 * document page, or a text field's text area in a dialog. Menus, the menu bar, buttons and lists
 * have windows of their own, so tapping them leaves the keyboard alone. There, dragging (scrolling)
 * puts the keyboard away, and a tap without a drag brings it up. A touch arrives as touch events
 * or as emulated mouse events, so both are watched; each event is seen once per widget it passes
 * through, so only the first sighting counts. */
enum { MOTION = 3, PRESS = 4, RELEASE = 7, TOUCH_BEGIN = 37, TOUCH_UPDATE = 38, TOUCH_END = 39, TOUCH_CANCEL = 40 };
#define DRAG_PX 48
typedef struct { int type; void *window; signed char send_event; unsigned time; double x, y; } EvPointer;
static int touching, dragged;
static double start_y;

static void keyboard_show(void *top) {
    if (kbd_widget && !CALL(gtk, void *, gtk_window_get_focus, (void *))(top))
        CALL(gtk, void, gtk_widget_grab_focus, (void *))(kbd_widget);
    kbd_forcing = 1;
    gtk_im_context_focus_in(kbd_ctx);
    kbd_forcing = 0;
}
static void keyboard_hide(void) {
    gtk_im_context_focus_out(kbd_ctx); /* the Maliit module hides the keyboard on focus_out */
}
static int on_any_event(void *ihint, unsigned n, const void *params, void *data) {
    static const void *last_event; static int last_type; static unsigned last_time;
    (void)ihint; (void)data;
    if (!kbd_ctx || n < 2) return 1;
    const EvPointer *e = CALL(gob, void *, g_value_get_boxed, (const void *))((const char *)params + 24 /* sizeof(GValue) */);
    if (!e || (e->type != MOTION && e->type != PRESS && e->type != RELEASE && e->type < TOUCH_BEGIN) || e->type > TOUCH_CANCEL)
        return 1;
    if (e == last_event && e->type == last_type && e->time == last_time) return 1;
    last_event = e; last_type = e->type; last_time = e->time;
    void *widget = CALL(gob, void *, g_value_get_object, (const void *))(params);
    void *top = widget ? CALL(gtk, void *, gtk_widget_get_toplevel, (void *))(widget) : NULL;
    if (!top || !e->window || e->window != get(kbd_ctx, CLIENT)) return 1;
    switch (e->type) {
    case PRESS: case TOUCH_BEGIN:
        touching = 1; dragged = 0; start_y = e->y;
        break;
    case MOTION: case TOUCH_UPDATE:
        if (touching && !dragged && (e->y - start_y > DRAG_PX || start_y - e->y > DRAG_PX)) {
            dragged = 1;
            if (kbd_shown) { TRACE("drag: keyboard away"); keyboard_hide(); }
        }
        break;
    default: /* release, touch end */
        if (touching && !dragged && !kbd_shown) { TRACE("tap: keyboard up"); keyboard_show(top); }
        touching = 0;
    }
    return 1; /* keep the hook */
}
static void watch_taps(void) {
    static int done;
    if (done) return;
    done = 1;
    unsigned id = CALL(gob, unsigned, g_signal_lookup, (const char *, size_t))("event",
        CALL(gtk, size_t, gtk_widget_get_type, (void))());
    if (id) CALL(gob, unsigned long, g_signal_add_emission_hook, (unsigned, unsigned, void *, void *, void *))(
        id, 0, (void *)on_any_event, NULL, NULL);
}

/* Window events in the trace: size changes (the keyboard shrinks the window), state and focus. */
typedef struct { int type; void *window; signed char send_event; int x, y, width, height; } EvConfigure;
typedef struct { int type; void *window; signed char send_event; int changed, state; } EvState;
typedef struct { int type; void *window; signed char send_event; short in; } EvFocus;
static int on_configure(void *w, EvConfigure *e, void *u) { (void)u; TRACE("configure %p %dx%d at %d,%d", w, e->width, e->height, e->x, e->y); return 0; }
static int on_state(void *w, EvState *e, void *u) { (void)u; TRACE("state %p changed=%#x now=%#x", w, e->changed, e->state); return 0; }
static int on_focus(void *w, EvFocus *e, void *u) { (void)u; TRACE("window_focus %p %s", w, e->in ? "in" : "out"); return 0; }
/* Tests: with snap-request, also print each window's widget tree (type, x, width, min width). */
static void dump_tree(void *w, int depth) {
    Rect r; Req mn, nt;
    CALL(gtk, void, gtk_widget_get_allocation, (void *, Rect *))(w, &r);
    CALL(gtk, void, gtk_widget_get_preferred_size, (void *, Req *, Req *))(w, &mn, &nt);
    fprintf(stderr, "lo-tree %*s%s x=%d w=%d min=%d nat=%d%s\n", depth * 2, "",
            CALL(gob, const char *, g_type_name_from_instance, (void *))(w), r.x, r.width, mn.width, nt.width,
            CALL(gtk, int, gtk_widget_get_visible, (void *))(w) ? "" : " (hidden)");
    if (depth < 9 && is_a(w, "gtk_container_get_type")) {
        GList *kids = CALL(gtk, GList *, gtk_container_get_children, (void *))(w);
        for (GList *l = kids; l; l = l->next) dump_tree(l->data, depth + 1);
        CALL(glib, void, g_list_free, (GList *))(kids);
    }
}

/* Tests (only while tracing, see trace_on): creating ~/.cache/libreoffice.juujbyg/snap-request makes
 * the shim draw every visible window into snap-<n>.png in that folder, then remove the request.
 * Lomiri's screen capture stops working at times; this doesn't depend on it. */
static int snap_check(void *unused) {
    (void)unused;
    char dir[512], req[600], out[600]; const char *cache = getenv("XDG_CACHE_HOME");
    snprintf(dir, sizeof dir, "%s/libreoffice.juujbyg", cache ? cache : "");
    snprintf(req, sizeof req, "%s/snap-request", dir);
    if (access(req, F_OK) != 0) return 1;
    unlink(req);
    GList *all = CALL(gtk, GList *, gtk_window_list_toplevels, (void))(), *l;
    int n = 0;
    for (l = all; l; l = l->next)
        if (CALL(gtk, int, gtk_widget_get_visible, (void *))(l->data)) {
            snprintf(out, sizeof out, "%s/snap-%d.png", dir, n++);
            snapshot_window(l->data, out);
            if (debug > 0) dump_tree(l->data, 0);
        }
    CALL(glib, void, g_list_free, (GList *))(all);
    return 1;
}

static void trace_window(void *w) {
    watch_taps();
    if (im_trace < 0) im_trace = trace_on();
    if (!im_trace) return;
    static int snapping;
    if (!snapping++) CALL(glib, unsigned, g_timeout_add, (unsigned, int (*)(void *), void *))(500, snap_check, NULL);
    unsigned long (*connect)(void *, const char *, void *, void *, void *, int) = gob("g_signal_connect_data");
    connect(w, "configure-event", (void *)on_configure, NULL, NULL, 0);
    connect(w, "window-state-event", (void *)on_state, NULL, NULL, 0);
    connect(w, "focus-in-event", (void *)on_focus, NULL, NULL, 0);
    connect(w, "focus-out-event", (void *)on_focus, NULL, NULL, 0);
}

/* ---- One-finger scrolling -----------------------------------------------------------------------
 * LibreOffice treats a finger on the page like a mouse: dragging selected text, so documents could
 * only be moved with the thin scrollbars. Every event passes through GDK's event handler, which GTK
 * installs with gdk_event_handler_set; the shim wraps that handler. The first finger on a page drawn
 * by LibreOffice (its OOoFixed widget: the document, rulers and toolbars, not GTK's own widgets in
 * dialogs) is held back:
 *  - lifted without moving far: LibreOffice gets the press and release (a tap places the cursor);
 *  - moved more than SLOP px: it becomes smooth scroll events, and LibreOffice never sees the press,
 *    so nothing is selected; lifting while moving fast keeps it gliding (kinetic);
 *  - held still for HOLD ms: LibreOffice gets the press, so press-and-hold then drag selects;
 *  - a second finger: both go to LibreOffice (pinch to zoom), but the first one no longer drives
 *    the pointer, so zooming doesn't select text as well.
 * A touch arrives as touch events (on windows that take them, like the page, which has gestures)
 * or as mouse events emulated from the first touch; both are handled. LO_SHIM_SCROLL_PX sets how
 * many pixels of finger travel make one mouse-wheel step (3 lines in LibreOffice), and
 * LO_SHIM_NO_SCROLL=1 turns all of this off. */
#include <math.h>
enum { SCROLL = 31, SMOOTH = 4, SRC_TOUCHSCREEN = 5, SLOP = 24, HOLD = 450 };
typedef void (*EventFunc)(void *event, void *data);
static EventFunc real_handler;
static void *real_data;
typedef struct { int type; void *window; signed char send_event; unsigned time; double x, y;
    unsigned state; int direction; void *device; double x_root, y_root, delta_x, delta_y; unsigned is_stop : 1; } EvScroll;
typedef struct { int type; void *window; signed char send_event; unsigned time; double x, y; double *axes;
    unsigned state; void *sequence; int emulating_pointer; void *device; double x_root, y_root; } EvTouch;

static enum { T_IDLE, T_PENDING, T_SCROLL, T_PASS, T_PINCH } tstate;
static void *held;        /* the first press, copied, while it is held back */
static void *last_move;   /* a copy of the last finger event: window, device and position for scrolls */
static double sx, sy, lx, ly, vx, vy;
static unsigned last_ms, hold_timer, glide_timer;
static double px_per_step = -1;

static void deliver(void *e) { real_handler(e, real_data); }
static int ev_type(void *e) { return *(int *)e; }
static void *ev_window(void *e) { return ((void **)e)[1]; }
static unsigned ev_time(void *e) { return CALL(gdk, unsigned, gdk_event_get_time, (void *))(e); }
static int coords(void *e, double *x, double *y) {
    return CALL(gdk, int, gdk_event_get_coords, (void *, double *, double *))(e, x, y);
}
static int from_touchscreen(void *e) {
    void *d = CALL(gdk, void *, gdk_event_get_source_device, (void *))(e);
    return d && CALL(gdk, int, gdk_device_get_source, (void *))(d) == SRC_TOUCHSCREEN;
}
/* What a finger event is: 1 first finger down, 2 it moves, 3 it is lifted, 4 another finger down,
 * 0 anything else. */
static int finger(void *e) {
    int t = ev_type(e);
    if (t == TOUCH_BEGIN || t == TOUCH_UPDATE || t == TOUCH_END || t == TOUCH_CANCEL) {
        int primary = ((EvTouch *)e)->emulating_pointer; /* the first finger, which also drives the pointer */
        if (!primary) return t == TOUCH_BEGIN ? 4 : 0;
        return t == TOUCH_BEGIN ? 1 : t == TOUCH_UPDATE ? 2 : 3;
    }
    if (!from_touchscreen(e)) return 0;
    if (t == PRESS) { unsigned b = 0; CALL(gdk, int, gdk_event_get_button, (void *, unsigned *))(e, &b); return b == 1 ? 1 : 0; }
    if (t == MOTION) return 2;
    if (t == RELEASE) return 3;
    return 0;
}
/* A window LibreOffice draws itself (its GtkSalFrame): an event box in a grid, straight in the
 * GtkWindow. That covers the document with its rulers, toolbars and status bar; dialogs built from
 * GTK widgets are left alone, so their lists, sliders and text fields work as usual. */
static int is_type(void *w, const char *name) {
    const char *n = w ? CALL(gob, const char *, g_type_name_from_instance, (void *))(w) : NULL;
    return n && strcmp(n, name) == 0;
}
static int lo_page(void *gdkwin) {
    void *w = NULL;
    if (!gdkwin) return 0;
    CALL(gdk, void, gdk_window_get_user_data, (void *, void **))(gdkwin, &w);
    if (!is_type(w, "GtkEventBox")) return 0;
    void *grid = CALL(gtk, void *, gtk_widget_get_parent, (void *))(w);
    if (!is_type(grid, "GtkGrid")) return 0;
    void *top = CALL(gtk, void *, gtk_widget_get_parent, (void *))(grid);
    return top && is_a(top, "gtk_window_get_type");
}
static void forget(void **e) { if (*e) { CALL(gdk, void, gdk_event_free, (void *))(*e); *e = NULL; } }
static void stop_timer(unsigned *id) { if (*id) { CALL(glib, int, g_source_remove, (unsigned))(*id); *id = 0; } }
static void keep(void **slot, void *e) { forget(slot); *slot = CALL(gdk, void *, gdk_event_copy, (void *))(e); }

/* A smooth scroll by (dx, dy) pixels of finger travel, at the last finger position. */
static void scroll_by(double dx, double dy, int stop) {
    if (!last_move) return;
    if (px_per_step < 0) {
        const char *v = getenv("LO_SHIM_SCROLL_PX");
        px_per_step = v ? atof(v) : 0;
        if (px_per_step <= 0) px_per_step = 60; /* 1:1 with the finger at 192 dpi */
    }
    /* Calc scrolls a row per line, about twice as far as Writer's line: half the speed there. */
    double step = px_per_step;
    void *w = NULL;
    CALL(gdk, void, gdk_window_get_user_data, (void *, void **))(ev_window(last_move), &w);
    void *top = w ? CALL(gtk, void *, gtk_widget_get_toplevel, (void *))(w) : NULL;
    const char *title = top && is_a(top, "gtk_window_get_type") ? CALL(gtk, const char *, gtk_window_get_title, (void *))(top) : NULL;
    if (title && strstr(title, "LibreOffice Calc")) step *= 1.9;
    EvScroll *s = CALL(gdk, void *, gdk_event_new, (int))(SCROLL);
    double x = 0, y = 0, rx = 0, ry = 0;
    coords(last_move, &x, &y);
    CALL(gdk, int, gdk_event_get_root_coords, (void *, double *, double *))(last_move, &rx, &ry);
    s->window = CALL(gob, void *, g_object_ref, (void *))(ev_window(last_move));
    s->send_event = 1;
    s->time = ev_time(last_move);
    s->x = x; s->y = y; s->x_root = rx; s->y_root = ry;
    s->direction = SMOOTH;
    s->delta_x = -dx / step; s->delta_y = -dy / step;
    s->is_stop = stop ? 1 : 0;
    CALL(gdk, void, gdk_event_set_device, (void *, void *))(s, CALL(gdk, void *, gdk_event_get_device, (void *))(last_move));
    CALL(gdk, void, gdk_event_set_source_device, (void *, void *))(s, CALL(gdk, void *, gdk_event_get_source_device, (void *))(last_move));
    TRACE("scroll %.2f %.2f%s", s->delta_x, s->delta_y, stop ? " stop" : "");
    deliver(s);
    CALL(gdk, void, gdk_event_free, (void *))(s);
}
static int glide(void *unused) {
    (void)unused;
    vx *= 0.94; vy *= 0.94;
    if (fabs(vx) < 0.05 && fabs(vy) < 0.05) { glide_timer = 0; scroll_by(0, 0, 1); return 0; }
    scroll_by(vx * 16, vy * 16, 0);
    return 1;
}
static void release_held(void) { if (held) { deliver(held); forget(&held); } }
static int on_hold(void *unused) {
    (void)unused;
    hold_timer = 0;
    if (tstate == T_PENDING) { TRACE("touch: held, to LibreOffice"); release_held(); tstate = T_PASS; }
    return 0;
}

static void shim_handler(void *e, void *data) {
    (void)data;
    int f = finger(e);
    double x = 0, y = 0;
    if (f) coords(e, &x, &y);
    switch (tstate) {
    case T_IDLE:
        if (f == 1 || f == 4) stop_timer(&glide_timer);
        if (f == 1 && lo_page(ev_window(e))) {
            keep(&held, e); keep(&last_move, e);
            sx = lx = x; sy = ly = y; vx = vy = 0;
            last_ms = ev_time(e);
            hold_timer = CALL(glib, unsigned, g_timeout_add, (unsigned, int (*)(void *), void *))(HOLD, on_hold, NULL);
            tstate = T_PENDING;
            TRACE("touch: down, held back");
            return;
        }
        if (f == 1) {
            void *w = NULL; char chain[256] = "";
            if (ev_window(e)) CALL(gdk, void, gdk_window_get_user_data, (void *, void **))(ev_window(e), &w);
            for (int i = 0; w && i < 4; i++) {
                strncat(chain, CALL(gob, const char *, g_type_name_from_instance, (void *))(w), sizeof chain - strlen(chain) - 2);
                strncat(chain, " ", sizeof chain - strlen(chain) - 1);
                w = CALL(gtk, void *, gtk_widget_get_parent, (void *))(w);
            }
            TRACE("touch: not a page: %s", chain);
            tstate = T_PASS;
        }
        break;
    case T_PENDING:
        if (f == 4) {
            stop_timer(&hold_timer);
            if (held && ev_type(held) == TOUCH_BEGIN) ((EvTouch *)held)->emulating_pointer = 0;
            release_held();
            TRACE("touch: pinch");
            tstate = T_PINCH;
            break;
        }
        if (f == 2) {
            if (hypot(x - sx, y - sy) < SLOP) return;
            stop_timer(&hold_timer); forget(&held);
            tstate = T_SCROLL;
            TRACE("touch: scrolling");
            if (kbd_shown) keyboard_hide();
            keep(&last_move, e);
            scroll_by(x - sx, y - sy, 0);
            lx = x; ly = y; last_ms = ev_time(e);
            return;
        }
        if (f == 3) { TRACE("touch: tap"); stop_timer(&hold_timer); release_held(); tstate = T_IDLE; break; }
        break;
    case T_SCROLL:
        if (f == 2) {
            unsigned t = ev_time(e);
            double dt = t > last_ms ? t - last_ms : 1;
            vx = 0.6 * (x - lx) / dt + 0.4 * vx; vy = 0.6 * (y - ly) / dt + 0.4 * vy;
            keep(&last_move, e);
            scroll_by(x - lx, y - ly, 0);
            lx = x; ly = y; last_ms = t;
            return;
        }
        if (f == 3) {
            tstate = T_IDLE;
            if (ev_time(e) - last_ms > 80) vx = vy = 0; /* the finger stopped before lifting */
            if (fabs(vx) > 0.3 || fabs(vy) > 0.3)
                glide_timer = CALL(glib, unsigned, g_timeout_add, (unsigned, int (*)(void *), void *))(16, glide, NULL);
            else scroll_by(0, 0, 1);
            return;
        }
        if (f == 4) return; /* another finger while scrolling: ignored until all are up */
        if (ev_type(e) == TOUCH_END || ev_type(e) == TOUCH_CANCEL) return;
        break;
    case T_PASS:
        if (f == 3) tstate = T_IDLE;
        break;
    case T_PINCH:
        if (f == 2 || f == 3) {
            if (ev_type(e) >= TOUCH_BEGIN) ((EvTouch *)e)->emulating_pointer = 0;
            if (f == 3) tstate = T_IDLE;
        }
        break;
    }
    deliver(e);
}

void gdk_event_handler_set(EventFunc func, void *data, void (*notify)(void *)) {
    static void (*f)(EventFunc, void *, void (*)(void *));
    if (!f) f = dlsym(RTLD_NEXT, "gdk_event_handler_set");
    if (getenv("LO_SHIM_NO_SCROLL") || !func) { f(func, data, notify); return; }
    real_handler = func; real_data = data;
    TRACE("event handler wrapped");
    f(shim_handler, data, notify);
}

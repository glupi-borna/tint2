/**************************************************************************
*
* Tint2 : read/write config file
*
* Copyright (C) 2007 Pål Staurland (staura@gmail.com)
* Modified (C) 2008 thierry lorthiois (lorthiois@bbsoft.fr) from Omega distribution
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License version 2
* as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**************************************************************************/

#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <glib/gstdio.h>
#include <pango/pango-font.h>
#include <Imlib2.h>

#include "config.h"

#ifndef TINT2CONF

#include "tint2rc.h"
#include "common.h"
#include "server.h"
#include "strnatcmp.h"
#include "panel.h"
#include "task.h"
#include "taskbar.h"
#include "taskbarname.h"
#include "systraybar.h"
#include "launcher.h"
#include "clock.h"
#include "window.h"
#include "tooltip.h"
#include "timer.h"
#include "separator.h"
#include "execplugin.h"

#ifdef ENABLE_BATTERY
#include "battery.h"
#endif

#endif

// global path
char *config_path = NULL;
char *snapshot_path = NULL;

#ifndef TINT2CONF

// --------------------------------------------------
// backward compatibility
// detect if it's an old config file (==1)
static gboolean new_config_file;

static gboolean read_bg_color_hover;
static gboolean read_border_color_hover;
static gboolean read_bg_color_press;
static gboolean read_border_color_press;
static gboolean read_panel_position;

void default_config()
{
    config_path = NULL;
    snapshot_path = NULL;
    new_config_file = FALSE;
    read_panel_position = FALSE;
}

void cleanup_config()
{
    free(config_path);
    config_path = NULL;
    free(snapshot_path);
    snapshot_path = NULL;
}

void get_action(char *event, MouseAction *action)
{
    #define ACTION_BIND(name, value) if (strcmp(event, (name)) == 0) { *action = (value); return; }

    ACTION_BIND("none", NONE);
    ACTION_BIND("close", CLOSE);
    ACTION_BIND("toggle", TOGGLE);
    ACTION_BIND("iconify", ICONIFY);
    ACTION_BIND("shade", SHADE);
    ACTION_BIND("toggle_iconify", TOGGLE_ICONIFY);
    ACTION_BIND("maximize_restore", MAXIMIZE_RESTORE);
    ACTION_BIND("desktop_left", DESKTOP_LEFT);
    ACTION_BIND("desktop_right", DESKTOP_RIGHT);
    ACTION_BIND("next_task", NEXT_TASK);
    ACTION_BIND("prev_task", PREV_TASK);
    fprintf(stderr, "tint2: Error: unrecognized action '%s'. Please fix your config file.\n", event);

    #undef ACTION_BIND
}

int get_task_status(char *status)
{
    #define RETURNIF(name, value) if (strcmp(status, (name)) == 0) return value;

    RETURNIF("active", TASK_ACTIVE);
    RETURNIF("iconified", TASK_ICONIFIED);
    RETURNIF("urgent", TASK_URGENT);

    #undef RETURNIF

    return -1;
}

int config_get_monitor(char *monitor)
{
    if (strcmp(monitor, "primary") == 0) {
        for (int i = 0; i < server.num_monitors; ++i) {
            if (server.monitors[i].primary)
                return i;
        }
        return 0;
    }
    if (strcmp(monitor, "all") == 0) {
        return -1;
    }
    char *endptr;
    int ret_int = strtol(monitor, &endptr, 10);
    if (*endptr == 0)
        return ret_int - 1;
    else {
        // monitor specified by name, not by index
        int i, j;
        for (i = 0; i < server.num_monitors; ++i) {
            if (server.monitors[i].names == 0)
                // xrandr can't identify monitors
                continue;
            j = 0;
            while (server.monitors[i].names[j] != 0) {
                if (strcmp(monitor, server.monitors[i].names[j++]) == 0)
                    return i;
            }
        }
    }

    // monitor not found or xrandr can't identify monitors => all
    return -1;
}

static gint compare_strings(gconstpointer a, gconstpointer b)
{
    return strnatcasecmp((const char *)a, (const char *)b);
}

void load_launcher_app_dir(const char *path)
{
    GList *subdirs = NULL;
    GList *files = NULL;

    GDir *d = g_dir_open(path, 0, NULL);
    if (d) {
        const gchar *name;
        while ((name = g_dir_read_name(d))) {
            gchar *file = g_build_filename(path, name, NULL);
            if (!g_file_test(file, G_FILE_TEST_IS_DIR) && g_str_has_suffix(file, ".desktop")) {
                files = g_list_append(files, file);
            } else if (g_file_test(file, G_FILE_TEST_IS_DIR)) {
                subdirs = g_list_append(subdirs, file);
            } else {
                g_free(file);
            }
        }
        g_dir_close(d);
    }

    subdirs = g_list_sort(subdirs, compare_strings);
    GList *l;
    for (l = subdirs; l; l = g_list_next(l)) {
        gchar *dir = (gchar *)l->data;
        load_launcher_app_dir(dir);
        g_free(dir);
    }
    g_list_free(subdirs);

    files = g_list_sort(files, compare_strings);
    for (l = files; l; l = g_list_next(l)) {
        gchar *file = (gchar *)l->data;
        panel_config.launcher.list_apps = g_slist_append(panel_config.launcher.list_apps, strdup(file));
        g_free(file);
    }
    g_list_free(files);
}

Separator *get_or_create_last_separator()
{
    if (!panel_config.separator_list) {
        fprintf(stderr, "tint2: Warning: separator items should shart with 'separator = new'\n");
        panel_config.separator_list = g_list_append(panel_config.separator_list, create_separator());
    }
    return (Separator *)g_list_last(panel_config.separator_list)->data;
}

Execp *get_or_create_last_execp()
{
    if (!panel_config.execp_list) {
        fprintf(stderr, "tint2: Warning: execp items should start with 'execp = new'\n");
        panel_config.execp_list = g_list_append(panel_config.execp_list, create_execp());
    }
    return (Execp *)g_list_last(panel_config.execp_list)->data;
}

Button *get_or_create_last_button()
{
    if (!panel_config.button_list) {
        fprintf(stderr, "tint2: Warning: button items should start with 'button = new'\n");
        panel_config.button_list = g_list_append(panel_config.button_list, create_button());
    }
    return (Button *)g_list_last(panel_config.button_list)->data;
}

void add_entry(char *key, char *value)
{
    char *value1 = 0, *value2 = 0, *value3 = 0;

    #define KEY_IS(name) strcmp(key, (name)) == 0

    #define SET_BG_IDX(VALUE, VARNAME) {\
        int id = atoi((VALUE)); \
        id = (id < backgrounds->len && id >= 0) ? id : 0; \
        (VARNAME) = &g_array_index(backgrounds, Background, id);\
    }

    #define SIMPLE_INT(KEY, VARIABLE) \
        else if (KEY_IS((KEY))) { (VARIABLE) = atoi(value); }

    #define SET_STR(VARIABLE) \
        if (strlen(value) > 0) (VARIABLE) = strdup(value)

    #define SIMPLE_STR(KEY, VARIABLE) \
        else if (KEY_IS((KEY))) { SET_STR(VARIABLE); }

    #define FREE_STR(KEY, VARIABLE) \
        else if (KEY_IS((KEY))) { \
            free_and_null(VARIABLE); \
            SET_STR(VARIABLE); \
        }

    #define FREE_STR_SET_FLAG(KEY, VARIABLE, FLAG) \
        else if (KEY_IS((KEY))) { \
            free_and_null(VARIABLE); \
            if (strlen(value) > 0) { \
                VARIABLE = strdup(value); \
                FLAG = TRUE; \
            } \
        }


    /* Background and border */
    if (KEY_IS("scale_relative_to_dpi")) {
        ui_scale_dpi_ref = atof(value);

    } else if (KEY_IS("scale_relative_to_screen_height")) {
        ui_scale_monitor_size_ref = atof(value);

    } else if (KEY_IS("rounded")) {
        // 'rounded' is the first parameter => alloc a new background
        if (backgrounds->len > 0) {
            Background *bg = &g_array_index(backgrounds, Background, backgrounds->len - 1);
            if (!read_bg_color_hover)
                memcpy(&bg->fill_color_hover, &bg->fill_color, sizeof(Color));
            if (!read_border_color_hover)
                memcpy(&bg->border_color_hover, &bg->border, sizeof(Color));
            if (!read_bg_color_press)
                memcpy(&bg->fill_color_pressed, &bg->fill_color_hover, sizeof(Color));
            if (!read_border_color_press)
                memcpy(&bg->border_color_pressed, &bg->border_color_hover, sizeof(Color));
        }
        Background bg;
        init_background(&bg);
        bg.border.radius = atoi(value);
        g_array_append_val(backgrounds, bg);
        read_bg_color_hover = FALSE;
        read_border_color_hover = FALSE;
        read_bg_color_press = FALSE;
        read_border_color_press = FALSE;

    } else if (KEY_IS("border_width")) {
        g_array_index(backgrounds, Background, backgrounds->len - 1).border.width = atoi(value);

    } else if (KEY_IS("border_sides")) {
        Background *bg = &g_array_index(backgrounds, Background, backgrounds->len - 1);
        bg->border.mask = 0;

        #define SET_BORDER(lower, upper, val) \
        if (strchr(value, (lower)) || strchr(value, (upper))) \
            bg->border.mask |= (val);

        SET_BORDER('l', 'L', BORDER_LEFT);
        SET_BORDER('r', 'R', BORDER_RIGHT);
        SET_BORDER('t', 'T', BORDER_TOP);
        SET_BORDER('b', 'B', BORDER_BOTTOM);
        #undef SET_BORDER

        if (!bg->border.mask)
            bg->border.width = 0;

    #define SET_BG(color_var) \
        Background *bg = &g_array_index(backgrounds, Background, backgrounds->len - 1); \
        extract_values(value, &value1, &value2, &value3); \
        get_color(value1, (color_var).rgb); \
        if (value2) \
            (color_var).alpha = (atoi(value2) / 100.0); \
        else \
            (color_var).alpha = 0.5;

    } else if (KEY_IS("background_color")) {
        SET_BG(bg->fill_color);

    } else if (KEY_IS("border_color")) {
        SET_BG(bg->border.color);

    } else if (KEY_IS("background_color_hover")) {
        SET_BG(bg->fill_color_hover);
        read_bg_color_hover = 1;

    } else if (KEY_IS("border_color_hover")) {
        SET_BG(bg->border_color_hover);
        read_border_color_hover = 1;

    } else if (KEY_IS("background_color_pressed")) {
        SET_BG(bg->fill_color_pressed);
        read_bg_color_press = 1;

    } else if (KEY_IS("border_color_pressed")) {
        SET_BG(bg->border_color_pressed);
        read_border_color_press = 1;
    #undef SET_BG

    #define SET_BG_GRADIENT(type) \
        Background *bg = &g_array_index(backgrounds, Background, backgrounds->len - 1); \
        int id = atoi(value); \
        id = (id < gradients->len && id >= 0) ? id : -1; \
        if (id >= 0) \
            bg->gradients[(type)] = &g_array_index(gradients, GradientClass, id);

    } else if (KEY_IS("gradient_id")) {
        SET_BG_GRADIENT(MOUSE_NORMAL);

    } else if (KEY_IS("gradient_id_hover") || KEY_IS("hover_gradient_id")) {
        SET_BG_GRADIENT(MOUSE_OVER);

    } else if (KEY_IS("gradient_id_pressed") || KEY_IS("pressed_gradient_id")) {
        SET_BG_GRADIENT(MOUSE_DOWN);
    #undef SET_BG_GRADIENT

    } else if (KEY_IS("border_content_tint_weight")) {
        Background *bg = &g_array_index(backgrounds, Background, backgrounds->len - 1);
        bg->border_content_tint_weight = MAX(0.0, MIN(1.0, atoi(value) / 100.));

    } else if (KEY_IS("background_content_tint_weight")) {
        Background *bg = &g_array_index(backgrounds, Background, backgrounds->len - 1);
        bg->fill_content_tint_weight = MAX(0.0, MIN(1.0, atoi(value) / 100.));
    }

    /* Gradients */
    else if (KEY_IS("gradient")) {
        // Create a new gradient
        GradientClass g;
        init_gradient(&g, gradient_type_from_string(value));
        g_array_append_val(gradients, g);
    } else if (KEY_IS("start_color")) {
        GradientClass *g = &g_array_index(gradients, GradientClass, gradients->len - 1);
        extract_values(value, &value1, &value2, &value3);
        get_color(value1, g->start_color.rgb);
        if (value2)
            g->start_color.alpha = (atoi(value2) / 100.0);
        else
            g->start_color.alpha = 0.5;
    } else if (KEY_IS("end_color")) {
        GradientClass *g = &g_array_index(gradients, GradientClass, gradients->len - 1);
        extract_values(value, &value1, &value2, &value3);
        get_color(value1, g->end_color.rgb);
        if (value2)
            g->end_color.alpha = (atoi(value2) / 100.0);
        else
            g->end_color.alpha = 0.5;
    } else if (KEY_IS("color_stop")) {
        GradientClass *g = &g_array_index(gradients, GradientClass, gradients->len - 1);
        extract_values(value, &value1, &value2, &value3);
        ColorStop *color_stop = (ColorStop *)calloc(1, sizeof(ColorStop));
        color_stop->offset = atof(value1) / 100.0;
        get_color(value2, color_stop->color.rgb);
        if (value3)
            color_stop->color.alpha = (atoi(value3) / 100.0);
        else
            color_stop->color.alpha = 0.5;
        g->extra_color_stops = g_list_append(g->extra_color_stops, color_stop);
    }

    /* Panel */
    SIMPLE_INT("panel_shrink", panel_shrink)
    SIMPLE_INT("font_shadow", panel_config.font_shadow)
    SIMPLE_INT("wm_menu", wm_menu)
    SIMPLE_INT("panel_dock", panel_dock)
    SIMPLE_INT("panel_pivot_struts", panel_pivot_struts)
    SIMPLE_INT("urgent_nb_of_blink", max_tick_urgent)
    SIMPLE_INT("urgent_nb_of_blink", max_tick_urgent)
    SIMPLE_INT("urgent_nb_of_blink", max_tick_urgent)
    SIMPLE_INT("disable_transparency", server.disable_transparency)

    SIMPLE_STR("panel_lclick_command", panel_config.lclick_command)
    SIMPLE_STR("panel_mclick_command", panel_config.mclick_command)
    SIMPLE_STR("panel_rclick_command", panel_config.rclick_command)
    SIMPLE_STR("panel_uwheel_command", panel_config.uwheel_command)
    SIMPLE_STR("panel_dwheel_command", panel_config.dwheel_command)

    else if (KEY_IS("panel_monitor")) {
        panel_config.monitor = config_get_monitor(value);
    } else if (KEY_IS("panel_size")) {
        extract_values(value, &value1, &value2, &value3);

        char *b;
        if ((b = strchr(value1, '%'))) {
            b[0] = '\0';
            panel_config.fractional_width = TRUE;
        }
        panel_config.area.width = atoi(value1);
        if (panel_config.area.width == 0) {
            // full width mode
            panel_config.area.width = 100;
            panel_config.fractional_width = TRUE;
        }
        if (value2) {
            if ((b = strchr(value2, '%'))) {
                b[0] = '\0';
                panel_config.fractional_height = 1;
            }
            panel_config.area.height = atoi(value2);
        }
    } else if (KEY_IS("panel_items")) {
        new_config_file = TRUE;
        free_and_null(panel_items_order);
        panel_items_order = strdup(value);
        systray_enabled = 0;
        launcher_enabled = 0;
#ifdef ENABLE_BATTERY
        battery_enabled = 0;
#endif
        clock_enabled = 0;
        taskbar_enabled = 0;
        for (int j = 0; j < strlen(panel_items_order); j++) {
            if (panel_items_order[j] == 'L')
                launcher_enabled = 1;
            if (panel_items_order[j] == 'T')
                taskbar_enabled = 1;
            if (panel_items_order[j] == 'B') {
#ifdef ENABLE_BATTERY
                battery_enabled = 1;
#else
                fprintf(stderr, "tint2: tint2 has been compiled without battery support\n");
#endif
            }
            if (panel_items_order[j] == 'S') {
                // systray disabled in snapshot mode
                if (snapshot_path == 0)
                    systray_enabled = 1;
            }
            if (panel_items_order[j] == 'C')
                clock_enabled = 1;
        }

    } else if (KEY_IS("panel_margin")) {
        extract_values(value, &value1, &value2, &value3);
        panel_config.marginx = atoi(value1);
        if (value2)
            panel_config.marginy = atoi(value2);

    } else if (KEY_IS("panel_padding")) {
        extract_values(value, &value1, &value2, &value3);
        panel_config.area.paddingxlr = panel_config.area.paddingx = atoi(value1);
        if (value2)
            panel_config.area.paddingy = atoi(value2);
        if (value3)
            panel_config.area.paddingx = atoi(value3);

    } else if (KEY_IS("panel_position")) {
        read_panel_position = TRUE;
        extract_values(value, &value1, &value2, &value3);
        if (strcmp(value1, "top") == 0)
            panel_position = TOP;
        else {
            if (strcmp(value1, "bottom") == 0)
                panel_position = BOTTOM;
            else
                panel_position = CENTER;
        }

        if (!value2)
            panel_position |= CENTER;
        else {
            if (strcmp(value2, "left") == 0)
                panel_position |= LEFT;
            else {
                if (strcmp(value2, "right") == 0)
                    panel_position |= RIGHT;
                else
                    panel_position |= CENTER;
            }
        }
        if (!value3)
            panel_horizontal = 1;
        else {
            if (strcmp(value3, "vertical") == 0)
                panel_horizontal = 0;
            else
                panel_horizontal = 1;
        }
    }

    else if (KEY_IS("panel_background_id"))
        SET_BG_IDX(value, panel_config.area.bg)

    else if (KEY_IS("panel_layer")) {
        if (strcmp(value, "bottom") == 0)
            panel_layer = BOTTOM_LAYER;
        else if (strcmp(value, "top") == 0)
            panel_layer = TOP_LAYER;
        else
            panel_layer = NORMAL_LAYER;

    } else if (KEY_IS("panel_window_name")) {
        if (strlen(value) > 0) {
            free(panel_window_name);
            panel_window_name = strdup(value);
        }
    }

    /* Battery */

    #ifdef ENABLE_BATTERY
    #define BATTERY_CONFIG(key, code) \
        else if (KEY_IS(key)) { code }
    #else
        else if (KEY_IS(key)) { }
    #endif

    BATTERY_CONFIG("battery_low_status", {
        battery_low_status = atoi(value);
        if (battery_low_status < 0 || battery_low_status > 100)
            battery_low_status = 0;
    })

    BATTERY_CONFIG("battery_lclick_command", SET_STR(battery_lclick_command);)
    BATTERY_CONFIG("battery_mclick_command", SET_STR(battery_mclick_command);)
    BATTERY_CONFIG("battery_rclick_command", SET_STR(battery_rclick_command);)
    BATTERY_CONFIG("battery_uwheel_command", SET_STR(battery_uwheel_command);)
    BATTERY_CONFIG("battery_dwheel_command", SET_STR(battery_dwheel_command);)
    BATTERY_CONFIG("battery_low_command", SET_STR(battery_low_cmd);)
    BATTERY_CONFIG("battery_full_command", SET_STR(battery_full_cmd);)
    BATTERY_CONFIG("battery_full_command", SET_STR(battery_full_cmd);)
    BATTERY_CONFIG("ac_connected_cmd", SET_STR(ac_connected_cmd);)
    BATTERY_CONFIG("ac_disconnected_cmd", SET_STR(ac_disconnected_cmd);)


    BATTERY_CONFIG("bat1_font", {
        bat1_font_desc = pango_font_description_from_string(value);
        bat1_has_font = TRUE;
    })

    BATTERY_CONFIG("bat2_font", {
        bat2_font_desc = pango_font_description_from_string(value);
        bat2_has_font = TRUE;
    })

    BATTERY_CONFIG("bat1_format", {
        if (strlen(value) > 0) {
            free(bat1_format);
            bat1_format = strdup(value);
            battery_enabled = 1;
        }
    })

    BATTERY_CONFIG("bat2_format", {
        if (strlen(value) > 0) {
            free(bat2_format);
            bat2_format = strdup(value);
            battery_enabled = 1;
        }
    })

    BATTERY_CONFIG("battery_font_color", {
        extract_values(value, &value1, &value2, &value3);
        get_color(value1, panel_config.battery.font_color.rgb);
        if (value2)
            panel_config.battery.font_color.alpha = (atoi(value2) / 100.0);
        else
            panel_config.battery.font_color.alpha = 0.5;
    })

    BATTERY_CONFIG("battery_padding", {
        extract_values(value, &value1, &value2, &value3);
        panel_config.battery.area.paddingxlr = panel_config.battery.area.paddingx = atoi(value1);
        if (value2)
            panel_config.battery.area.paddingy = atoi(value2);
        if (value3)
            panel_config.battery.area.paddingx = atoi(value3);
    })

    BATTERY_CONFIG("battery_background_id", {
        SET_BG_IDX(value, panel_config.battery.area.bg)
    })

    BATTERY_CONFIG("battery_hide", {
        percentage_hide = atoi(value);
        if (percentage_hide == 0)
            percentage_hide = 101;
    })

    BATTERY_CONFIG("battery_tooltip", {
        battery_tooltip_enabled = atoi(value);
    })

    #undef BATTERY_CONFIG

    /* Separator */
    SIMPLE_INT("separator_size", get_or_create_last_separator()->thickness)

    else if (KEY_IS("separator")) {
        panel_config.separator_list = g_list_append(panel_config.separator_list, create_separator());

    } else if (KEY_IS("separator_background_id")) {
        SET_BG_IDX(value, get_or_create_last_separator()->area.bg);

    } else if (KEY_IS("separator_color")) {
        Separator *separator = get_or_create_last_separator();
        extract_values(value, &value1, &value2, &value3);
        get_color(value1, separator->color.rgb);
        if (value2)
            separator->color.alpha = (atoi(value2) / 100.0);
        else
            separator->color.alpha = 0.5;

    } else if (KEY_IS("separator_style")) {
        Separator *separator = get_or_create_last_separator();
        if (g_str_equal(value, "empty"))
            separator->style = SEPARATOR_EMPTY;
        else if (g_str_equal(value, "line"))
            separator->style = SEPARATOR_LINE;
        else if (g_str_equal(value, "dots"))
            separator->style = SEPARATOR_DOTS;
        else
            fprintf(stderr, RED "tint2: Invalid separator_style value: %s" RESET "\n", value);

    } else if (KEY_IS("separator_padding")) {
        Separator *separator = get_or_create_last_separator();
        extract_values(value, &value1, &value2, &value3);
        separator->area.paddingxlr = separator->area.paddingx = atoi(value1);
        if (value2)
            separator->area.paddingy = atoi(value2);
        if (value3)
            separator->area.paddingx = atoi(value3);
    }

    /* Execp */
    SIMPLE_INT("execp_isolate", get_or_create_last_execp()->backend->isolate)
    SIMPLE_INT("execp_has_icon", get_or_create_last_execp()->backend->has_icon)
    SIMPLE_INT("execp_continuous", get_or_create_last_execp()->backend->continuous)
    SIMPLE_INT("execp_markup", get_or_create_last_execp()->backend->has_markup)
    SIMPLE_INT("execp_cache_icon", get_or_create_last_execp()->backend->cache_icon)
    SIMPLE_INT("execp_centered", get_or_create_last_execp()->backend->centered)

    FREE_STR("execp_command", get_or_create_last_execp()->backend->command)
    FREE_STR("execp_lclick_command", get_or_create_last_execp()->backend->lclick_command)
    FREE_STR("execp_mclick_command", get_or_create_last_execp()->backend->mclick_command)
    FREE_STR("execp_rclick_command", get_or_create_last_execp()->backend->rclick_command)
    FREE_STR("execp_uwheel_command", get_or_create_last_execp()->backend->uwheel_command)
    FREE_STR("execp_dwheel_command", get_or_create_last_execp()->backend->dwheel_command)
    FREE_STR_SET_FLAG("execp_tooltip",
        get_or_create_last_execp()->backend->tooltip,
        get_or_create_last_execp()->backend->has_user_tooltip)

    else if (KEY_IS("execp")) {
        panel_config.execp_list = g_list_append(panel_config.execp_list, create_execp());

    } else if (KEY_IS("execp_name")) {
        Execp *execp = get_or_create_last_execp();
        execp->backend->name[0] = 0;
        if (strlen(value) > sizeof(execp->backend->name) - 1)
            fprintf(stderr, RED "tint2: execp_name cannot be more than %ld bytes: '%s'" RESET "\n",
                    sizeof(execp->backend->name) - 1, value);
        else if (strlen(value) > 0)
            snprintf(execp->backend->name, sizeof(execp->backend->name), value);

    } else if (KEY_IS("execp_interval")) {
        Execp *execp = get_or_create_last_execp();
        execp->backend->interval = 0;
        int v = atoi(value);
        if (v < 0) {
            fprintf(stderr, "tint2: execp_interval must be an integer >= 0\n");
        } else {
            execp->backend->interval = v;
        }

    } else if (KEY_IS("execp_monitor")) {
        Execp *execp = get_or_create_last_execp();
        execp->backend->monitor = config_get_monitor(value);

    } else if (KEY_IS("execp_font")) {
        Execp *execp = get_or_create_last_execp();
        pango_font_description_free(execp->backend->font_desc);
        execp->backend->font_desc = pango_font_description_from_string(value);
        execp->backend->has_font = TRUE;

    } else if (KEY_IS("execp_font_color")) {
        Execp *execp = get_or_create_last_execp();
        extract_values(value, &value1, &value2, &value3);
        get_color(value1, execp->backend->font_color.rgb);
        if (value2)
            execp->backend->font_color.alpha = atoi(value2) / 100.0;
        else
            execp->backend->font_color.alpha = 0.5;

    } else if (KEY_IS("execp_padding")) {
        Execp *execp = get_or_create_last_execp();
        extract_values(value, &value1, &value2, &value3);
        execp->backend->paddingxlr = execp->backend->paddingx = atoi(value1);
        if (value2)
            execp->backend->paddingy = atoi(value2);
        else
            execp->backend->paddingy = 0;
        if (value3)
            execp->backend->paddingx = atoi(value3);

    } else if (KEY_IS("execp_background_id")) {
        SET_BG_IDX(value, get_or_create_last_execp()->backend->bg);

    } else if (KEY_IS("execp_icon_w")) {
        Execp *execp = get_or_create_last_execp();
        int v = atoi(value);
        if (v < 0) {
            fprintf(stderr, "tint2: execp_icon_w must be an integer >= 0\n");
        } else {
            execp->backend->icon_w = v;
        }

    } else if (KEY_IS("execp_icon_h")) {
        Execp *execp = get_or_create_last_execp();
        int v = atoi(value);
        if (v < 0) {
            fprintf(stderr, "tint2: execp_icon_h must be an integer >= 0\n");
        } else {
            execp->backend->icon_h = v;
        }
    }

    /* Button */
    FREE_STR("button_text", get_or_create_last_button()->backend->text)
    FREE_STR("button_tooltip", get_or_create_last_button()->backend->tooltip)
    FREE_STR("button_lclick_command", get_or_create_last_button()->backend->lclick_command)
    FREE_STR("button_mclick_command", get_or_create_last_button()->backend->mclick_command)
    FREE_STR("button_rclick_command", get_or_create_last_button()->backend->rclick_command)
    FREE_STR("button_uwheel_command", get_or_create_last_button()->backend->uwheel_command)
    FREE_STR("button_dwheel_command", get_or_create_last_button()->backend->dwheel_command)

    else if (KEY_IS("button")) {
        panel_config.button_list = g_list_append(panel_config.button_list, create_button());

    } else if (KEY_IS("button_icon")) {
        if (strlen(value)) {
            Button *button = get_or_create_last_button();
            button->backend->icon_name = expand_tilde(value);
        }

    } else if (KEY_IS("button_font")) {
        Button *button = get_or_create_last_button();
        pango_font_description_free(button->backend->font_desc);
        button->backend->font_desc = pango_font_description_from_string(value);
        button->backend->has_font = TRUE;

    } else if (KEY_IS("button_font_color")) {
        Button *button = get_or_create_last_button();
        extract_values(value, &value1, &value2, &value3);
        get_color(value1, button->backend->font_color.rgb);
        if (value2)
            button->backend->font_color.alpha = atoi(value2) / 100.0;
        else
            button->backend->font_color.alpha = 0.5;

    } else if (KEY_IS("button_padding")) {
        Button *button = get_or_create_last_button();
        extract_values(value, &value1, &value2, &value3);
        button->backend->paddingxlr = button->backend->paddingx = atoi(value1);
        if (value2)
            button->backend->paddingy = atoi(value2);
        else
            button->backend->paddingy = 0;
        if (value3)
            button->backend->paddingx = atoi(value3);

    } else if (KEY_IS("button_max_icon_size")) {
        Button *button = get_or_create_last_button();
        extract_values(value, &value1, &value2, &value3);
        button->backend->max_icon_size = MAX(0, atoi(value));
    } else if (KEY_IS("button_background_id")) {
        SET_BG_IDX(value, get_or_create_last_button()->backend->bg);

    } else if (KEY_IS("button_centered")) {
        Button *button = get_or_create_last_button();
        button->backend->centered = atoi(value);
    }

    /* Clock */
    SIMPLE_STR("time2_format", time2_format)
    SIMPLE_STR("time1_timezone", time1_timezone)
    SIMPLE_STR("time2_timezone", time2_timezone)
    SIMPLE_STR("clock_tooltip", time_tooltip_format)
    SIMPLE_STR("clock_tooltip_timezone", time_tooltip_timezone)
    SIMPLE_STR("clock_lclick_command", clock_lclick_command)
    SIMPLE_STR("clock_rclick_command", clock_rclick_command)
    SIMPLE_STR("clock_mclick_command", clock_mclick_command)
    SIMPLE_STR("clock_uwheel_command", clock_uwheel_command)
    SIMPLE_STR("clock_dwheel_command", clock_dwheel_command)

    else if (KEY_IS("time1_format")) {
        if (!new_config_file) {
            clock_enabled = TRUE;
            if (panel_items_order) {
                gchar *tmp = g_strconcat(panel_items_order, "C", NULL);
                free(panel_items_order);
                panel_items_order = strdup(tmp);
                g_free(tmp);
            } else {
                panel_items_order = strdup("C");
            }
        }
        if (strlen(value) > 0) {
            time1_format = strdup(value);
            clock_enabled = TRUE;
        }

    } else if (KEY_IS("time1_font")) {
        time1_font_desc = pango_font_description_from_string(value);
        time1_has_font = TRUE;

    } else if (KEY_IS("time2_font")) {
        time2_font_desc = pango_font_description_from_string(value);
        time2_has_font = TRUE;

    } else if (KEY_IS("clock_font_color")) {
        extract_values(value, &value1, &value2, &value3);
        get_color(value1, panel_config.clock.font.rgb);
        if (value2)
            panel_config.clock.font.alpha = (atoi(value2) / 100.0);
        else
            panel_config.clock.font.alpha = 0.5;

    } else if (KEY_IS("clock_padding")) {
        extract_values(value, &value1, &value2, &value3);
        panel_config.clock.area.paddingxlr = panel_config.clock.area.paddingx = atoi(value1);
        if (value2)
            panel_config.clock.area.paddingy = atoi(value2);
        if (value3)
            panel_config.clock.area.paddingx = atoi(value3);

    } else if (KEY_IS("clock_background_id")) {
        SET_BG_IDX(value, panel_config.clock.area.bg);
    }

    /* Taskbar */
    SIMPLE_INT("taskbar_name", taskbarname_enabled)
    SIMPLE_INT("taskbar_hide_inactive_tasks", hide_inactive_tasks)
    SIMPLE_INT("taskbar_hide_different_monitor", hide_task_diff_monitor)
    SIMPLE_INT("taskbar_hide_different_desktop", hide_task_diff_desktop)
    SIMPLE_INT("taskbar_hide_if_empty", hide_taskbar_if_empty)
    SIMPLE_INT("taskbar_always_show_all_desktop_tasks", always_show_all_desktop_tasks)

    else if (KEY_IS("taskbar_mode")) {
        if (strcmp(value, "multi_desktop") == 0)
            taskbar_mode = MULTI_DESKTOP;
        else
            taskbar_mode = SINGLE_DESKTOP;
    } else if (KEY_IS("taskbar_distribute_size")) {
        taskbar_distribute_size = atoi(value);
    } else if (KEY_IS("taskbar_padding")) {
        extract_values(value, &value1, &value2, &value3);
        panel_config.g_taskbar.area.paddingxlr = panel_config.g_taskbar.area.paddingx = atoi(value1);
        if (value2)
            panel_config.g_taskbar.area.paddingy = atoi(value2);
        if (value3)
            panel_config.g_taskbar.area.paddingx = atoi(value3);

    } else if (KEY_IS("taskbar_background_id")) {
        SET_BG_IDX(value, panel_config.g_taskbar.background[TASKBAR_NORMAL]);
        if (panel_config.g_taskbar.background[TASKBAR_ACTIVE] == 0)
            panel_config.g_taskbar.background[TASKBAR_ACTIVE] = panel_config.g_taskbar.background[TASKBAR_NORMAL];

    } else if (KEY_IS("taskbar_active_background_id")) {
        SET_BG_IDX(value, panel_config.g_taskbar.background[TASKBAR_ACTIVE]);

    } else if (KEY_IS("taskbar_name_padding")) {
        extract_values(value, &value1, &value2, &value3);
        panel_config.g_taskbar.area_name.paddingxlr = panel_config.g_taskbar.area_name.paddingx = atoi(value1);
        if (value2)
            panel_config.g_taskbar.area_name.paddingy = atoi(value2);


    } else if (KEY_IS("taskbar_name_background_id")) {
        SET_BG_IDX(value, panel_config.g_taskbar.background_name[TASKBAR_NORMAL]);
        if (panel_config.g_taskbar.background_name[TASKBAR_ACTIVE] == 0)
            panel_config.g_taskbar.background_name[TASKBAR_ACTIVE] =
                panel_config.g_taskbar.background_name[TASKBAR_NORMAL];

    } else if (KEY_IS("taskbar_name_active_background_id")) {
        SET_BG_IDX(value, panel_config.g_taskbar.background_name[TASKBAR_ACTIVE]);

    } else if (KEY_IS("taskbar_name_font")) {
        panel_config.taskbarname_font_desc = pango_font_description_from_string(value);
        panel_config.taskbarname_has_font = TRUE;

    } else if (KEY_IS("taskbar_name_font_color")) {
        extract_values(value, &value1, &value2, &value3);
        get_color(value1, taskbarname_font.rgb);
        if (value2)
            taskbarname_font.alpha = (atoi(value2) / 100.0);
        else
            taskbarname_font.alpha = 0.5;

    } else if (KEY_IS("taskbar_name_active_font_color")) {
        extract_values(value, &value1, &value2, &value3);
        get_color(value1, taskbarname_active_font.rgb);
        if (value2)
            taskbarname_active_font.alpha = (atoi(value2) / 100.0);
        else
            taskbarname_active_font.alpha = 0.5;

    } else if (KEY_IS("taskbar_sort_order")) {
        if (strcmp(value, "center") == 0) {
            taskbar_sort_method = TASKBAR_SORT_CENTER;
        } else if (strcmp(value, "title") == 0) {
            taskbar_sort_method = TASKBAR_SORT_TITLE;
        } else if (strcmp(value, "application") == 0) {
            taskbar_sort_method = TASKBAR_SORT_APPLICATION;
        } else if (strcmp(value, "lru") == 0) {
            taskbar_sort_method = TASKBAR_SORT_LRU;
        } else if (strcmp(value, "mru") == 0) {
            taskbar_sort_method = TASKBAR_SORT_MRU;
        } else {
            taskbar_sort_method = TASKBAR_NOSORT;
        }

    } else if (KEY_IS("task_align")) {
        if (strcmp(value, "center") == 0) {
            taskbar_alignment = ALIGN_CENTER;
        } else if (strcmp(value, "right") == 0) {
            taskbar_alignment = ALIGN_RIGHT;
        } else {
            taskbar_alignment = ALIGN_LEFT;
        }
    }

    /* Task */
    SIMPLE_INT("task_text", panel_config.g_task.has_text)
    SIMPLE_INT("task_icon", panel_config.g_task.has_icon)
    SIMPLE_INT("task_centered", panel_config.g_task.centered)
    // "tooltip" is deprecated but here for backwards compatibility
    SIMPLE_INT("task_tooltip", panel_config.g_task.tooltip_enabled)

    SIMPLE_INT("task_thumbnail", panel_config.g_task.thumbnail_enabled)
    else if (KEY_IS("task_thumbnail_size")) panel_config.g_task.thumbnail_width = MAX(8, atoi(value));

    else if (KEY_IS("task_width")) {
        // old parameter : just for backward compatibility
        panel_config.g_task.maximum_width = atoi(value);
        panel_config.g_task.maximum_height = 30;

    } else if (KEY_IS("task_maximum_size")) {
        extract_values(value, &value1, &value2, &value3);
        panel_config.g_task.maximum_width = atoi(value1);
        if (value2)
            panel_config.g_task.maximum_height = atoi(value2);
        else
            panel_config.g_task.maximum_height = panel_config.g_task.maximum_width;

    } else if (KEY_IS("task_padding")) {
        extract_values(value, &value1, &value2, &value3);
        panel_config.g_task.area.paddingxlr = panel_config.g_task.area.paddingx = atoi(value1);
        if (value2)
            panel_config.g_task.area.paddingy = atoi(value2);
        if (value3)
            panel_config.g_task.area.paddingx = atoi(value3);

    } else if (KEY_IS("task_font")) {
        panel_config.g_task.font_desc = pango_font_description_from_string(value);
        panel_config.g_task.has_font = TRUE;

    } else if (g_regex_match_simple("task.*_font_color", key, 0, 0)) {
        gchar **split = g_regex_split_simple("_", key, 0, 0);
        int status = g_strv_length(split) == 3 ? TASK_NORMAL : get_task_status(split[1]);
        g_strfreev(split);
        if (status >= 0) {
            extract_values(value, &value1, &value2, &value3);
            float alpha = 1;
            if (value2)
                alpha = (atoi(value2) / 100.0);
            get_color(value1, panel_config.g_task.font[status].rgb);
            panel_config.g_task.font[status].alpha = alpha;
            panel_config.g_task.config_font_mask |= (1 << status);
        }

    } else if (g_regex_match_simple("task.*_icon_asb", key, 0, 0)) {
        gchar **split = g_regex_split_simple("_", key, 0, 0);
        int status = g_strv_length(split) == 3 ? TASK_NORMAL : get_task_status(split[1]);
        g_strfreev(split);
        if (status >= 0) {
            extract_values(value, &value1, &value2, &value3);
            panel_config.g_task.alpha[status] = atoi(value1);
            panel_config.g_task.saturation[status] = atoi(value2);
            panel_config.g_task.brightness[status] = atoi(value3);
            panel_config.g_task.config_asb_mask |= (1 << status);
        }

    } else if (g_regex_match_simple("task.*_background_id", key, 0, 0)) {
        gchar **split = g_regex_split_simple("_", key, 0, 0);
        int status = g_strv_length(split) == 3 ? TASK_NORMAL : get_task_status(split[1]);
        g_strfreev(split);
        if (status >= 0) {
            SET_BG_IDX(value, panel_config.g_task.background[status]);

            panel_config.g_task.config_background_mask |= (1 << status);
            if (status == TASK_NORMAL)
                panel_config.g_task.area.bg = panel_config.g_task.background[TASK_NORMAL];
            if (panel_config.g_task.background[status]->border_content_tint_weight > 0 ||
                panel_config.g_task.background[status]->fill_content_tint_weight > 0)
                panel_config.g_task.has_content_tint = TRUE;
        }
    }

    /* Systray */
    SIMPLE_INT("systray_icon_size", systray_max_icon_size)

    else if (KEY_IS("systray_padding")) {
        if (!new_config_file && systray_enabled == 0) {
            systray_enabled = TRUE;
            if (panel_items_order) {
                gchar *tmp = g_strconcat(panel_items_order, "S", NULL);
                free(panel_items_order);
                panel_items_order = strdup(tmp);
                g_free(tmp);
            } else
                panel_items_order = strdup("S");
        }
        extract_values(value, &value1, &value2, &value3);
        systray.area.paddingxlr = systray.area.paddingx = atoi(value1);
        if (value2)
            systray.area.paddingy = atoi(value2);
        if (value3)
            systray.area.paddingx = atoi(value3);

    } else if (KEY_IS("systray_background_id")) {
        SET_BG_IDX(value, systray.area.bg);

    } else if (KEY_IS("systray_sort")) {
        if (strcmp(value, "descending") == 0)
            systray.sort = SYSTRAY_SORT_DESCENDING;
        else if (strcmp(value, "ascending") == 0)
            systray.sort = SYSTRAY_SORT_ASCENDING;
        else if (strcmp(value, "left2right") == 0)
            systray.sort = SYSTRAY_SORT_LEFT2RIGHT;
        else if (strcmp(value, "right2left") == 0)
            systray.sort = SYSTRAY_SORT_RIGHT2LEFT;

    } else if (KEY_IS("systray_icon_asb")) {
        extract_values(value, &value1, &value2, &value3);
        systray.alpha = atoi(value1);
        systray.saturation = atoi(value2);
        systray.brightness = atoi(value3);

    } else if (KEY_IS("systray_monitor")) {
        systray_monitor = MAX(0, config_get_monitor(value));

    } else if (KEY_IS("systray_name_filter")) {
        if (systray_hide_name_filter) {
            fprintf(stderr, "tint2: Error: duplicate option 'systray_name_filter'. Please use it only once. See "
                            "https://gitlab.com/o9000/tint2/issues/652\n");
            free(systray_hide_name_filter);
        }
        systray_hide_name_filter = strdup(value);
    }

    /* Launcher */
    SIMPLE_INT("launcher_icon_size", launcher_max_icon_size)
    SIMPLE_INT("launcher_icon_theme_override", launcher_icon_theme_override)
    SIMPLE_INT("launcher_tooltip", launcher_tooltip_enabled)
    SIMPLE_INT("startup_notifications", startup_notifications)

    else if (KEY_IS("launcher_padding")) {
        extract_values(value, &value1, &value2, &value3);
        panel_config.launcher.area.paddingxlr = panel_config.launcher.area.paddingx = atoi(value1);
        if (value2)
            panel_config.launcher.area.paddingy = atoi(value2);
        if (value3)
            panel_config.launcher.area.paddingx = atoi(value3);

    } else if (KEY_IS("launcher_background_id")) {
        SET_BG_IDX(value, panel_config.launcher.area.bg);

    } else if (KEY_IS("launcher_icon_background_id")) {
        SET_BG_IDX(value, launcher_icon_bg);

    } else if (KEY_IS("launcher_item_app")) {
        char *app = expand_tilde(value);
        panel_config.launcher.list_apps = g_slist_append(panel_config.launcher.list_apps, app);

    } else if (KEY_IS("launcher_apps_dir")) {
        char *path = expand_tilde(value);
        load_launcher_app_dir(path);
        free(path);

    } else if (KEY_IS("launcher_icon_theme")) {
        // if XSETTINGS manager running, tint2 use it.
        if (icon_theme_name_config)
            free(icon_theme_name_config);
        icon_theme_name_config = strdup(value);

    } else if (KEY_IS("launcher_icon_asb")) {
        extract_values(value, &value1, &value2, &value3);
        launcher_alpha = atoi(value1);
        launcher_saturation = atoi(value2);
        launcher_brightness = atoi(value3);
    }

    /* Tooltip */
    else if (KEY_IS("tooltip_show_timeout")) {
        int timeout_msec = 1000 * atof(value);
        g_tooltip.show_timeout_msec = timeout_msec;

    } else if (KEY_IS("tooltip_hide_timeout")) {
        int timeout_msec = 1000 * atof(value);
        g_tooltip.hide_timeout_msec = timeout_msec;

    } else if (KEY_IS("tooltip_padding")) {
        extract_values(value, &value1, &value2, &value3);
        if (value1)
            g_tooltip.paddingx = atoi(value1);
        if (value2)
            g_tooltip.paddingy = atoi(value2);

    } else if (KEY_IS("tooltip_background_id")) {
        SET_BG_IDX(value, g_tooltip.bg);

    } else if (KEY_IS("tooltip_font_color")) {
        extract_values(value, &value1, &value2, &value3);
        get_color(value1, g_tooltip.font_color.rgb);
        if (value2)
            g_tooltip.font_color.alpha = (atoi(value2) / 100.0);
        else
            g_tooltip.font_color.alpha = 0.1;

    } else if (KEY_IS("tooltip_font")) {
        g_tooltip.font_desc = pango_font_description_from_string(value);
    }

    /* Mouse actions */
    else if (KEY_IS("mouse_left"))
        get_action(value, &mouse_left);

    else if (KEY_IS("mouse_middle"))
        get_action(value, &mouse_middle);

    else if (KEY_IS("mouse_right"))
        get_action(value, &mouse_right);

    else if (KEY_IS("mouse_scroll_up"))
        get_action(value, &mouse_scroll_up);

    else if (KEY_IS("mouse_scroll_down"))
        get_action(value, &mouse_scroll_down);

    else if (KEY_IS("mouse_effects"))
        panel_config.mouse_effects = atoi(value);

    else if (KEY_IS("mouse_hover_icon_asb")) {
        extract_values(value, &value1, &value2, &value3);
        panel_config.mouse_over_alpha = atoi(value1);
        panel_config.mouse_over_saturation = atoi(value2);
        panel_config.mouse_over_brightness = atoi(value3);

    } else if (KEY_IS("mouse_pressed_icon_asb")) {
        extract_values(value, &value1, &value2, &value3);
        panel_config.mouse_pressed_alpha = atoi(value1);
        panel_config.mouse_pressed_saturation = atoi(value2);
        panel_config.mouse_pressed_brightness = atoi(value3);
    }

    /* autohide options */
    SIMPLE_INT("autohide", panel_autohide)

    else if (KEY_IS("autohide_show_timeout"))
        panel_autohide_show_timeout = 1000 * atof(value);

    else if (KEY_IS("autohide_hide_timeout"))
        panel_autohide_hide_timeout = 1000 * atof(value);

    else if (KEY_IS("strut_policy")) {
        if (strcmp(value, "follow_size") == 0)
            panel_strut_policy = STRUT_FOLLOW_SIZE;
        else if (strcmp(value, "none") == 0)
            panel_strut_policy = STRUT_NONE;
        else
            panel_strut_policy = STRUT_MINIMUM;

    } else if (KEY_IS("autohide_height")) {
        panel_autohide_height = atoi(value);
        if (panel_autohide_height == 0) {
            // autohide need height > 0
            panel_autohide_height = 1;
        }
    }

    // old config option
    else if (KEY_IS("systray")) {
        if (!new_config_file) {
            systray_enabled = atoi(value);
            if (systray_enabled) {
                if (panel_items_order) {
                    gchar *tmp = g_strconcat(panel_items_order, "S", NULL);
                    free(panel_items_order);
                    panel_items_order = strdup(tmp);
                    g_free(tmp);
                } else
                    panel_items_order = strdup("S");
            }
        }
    }

#ifdef ENABLE_BATTERY
    else if (KEY_IS("battery")) {
        if (!new_config_file) {
            battery_enabled = atoi(value);
            if (battery_enabled) {
                if (panel_items_order) {
                    gchar *tmp = g_strconcat(panel_items_order, "B", NULL);
                    free(panel_items_order);
                    panel_items_order = strdup(tmp);
                    g_free(tmp);
                } else
                    panel_items_order = strdup("B");
            }
        }
    }
#endif

    else if (KEY_IS("primary_monitor_first")) {
        fprintf(stderr,
                "tint2: deprecated config option \"%s\"\n"
                "       Please see the documentation regarding the alternatives.\n",
                key);
    } else
        fprintf(stderr, "tint2: invalid option \"%s\",\n  upgrade tint2 or correct your config file\n", key);

    #undef KEY_IS

    if (value1) free(value1);
    if (value2) free(value2);
    if (value3) free(value3);
}

gboolean config_read_file(const char *path)
{
    fprintf(stderr, "tint2: Loading config file: %s\n", path);

    FILE *fp = fopen(path, "r");
    if (!fp)
        return FALSE;

    char *line = NULL;
    size_t line_size = 0;
    while (getline(&line, &line_size, fp) >= 0) {
        char *key, *value;
        if (parse_line(line, &key, &value)) {
            add_entry(key, value);
            free(key);
            free(value);
        }
    }
    free(line);
    fclose(fp);

    if (!read_panel_position) {
        panel_horizontal = TRUE;
        panel_position = BOTTOM;
    }

    // append Taskbar item
    if (!new_config_file) {
        taskbar_enabled = TRUE;
        if (panel_items_order) {
            gchar *tmp = g_strconcat("T", panel_items_order, NULL);
            free(panel_items_order);
            panel_items_order = strdup(tmp);
            g_free(tmp);
        } else {
            panel_items_order = strdup("T");
        }
    }

    if (backgrounds->len > 0) {
        Background *bg = &g_array_index(backgrounds, Background, backgrounds->len - 1);
        if (!read_bg_color_hover)
            memcpy(&bg->fill_color_hover, &bg->fill_color, sizeof(Color));
        if (!read_border_color_hover)
            memcpy(&bg->border_color_hover, &bg->border, sizeof(Color));
        if (!read_bg_color_press)
            memcpy(&bg->fill_color_pressed, &bg->fill_color_hover, sizeof(Color));
        if (!read_border_color_press)
            memcpy(&bg->border_color_pressed, &bg->border_color_hover, sizeof(Color));
    }

    return TRUE;
}

gboolean config_read_default_path()
{
    const gchar *const *system_dirs;
    gchar *path1;

    // follow XDG specification
    // check tint2rc in user directory
    path1 = g_build_filename(g_get_user_config_dir(), "tint2", "tint2rc", NULL);
    if (g_file_test(path1, G_FILE_TEST_EXISTS)) {
        gboolean result = config_read_file(path1);
        config_path = strdup(path1);
        g_free(path1);
        return result;
    }
    g_free(path1);

    // copy tint2rc from system directory to user directory

    fprintf(stderr, "tint2: could not find a config file! Creating a default one.\n");
    // According to the XDG Base Directory Specification
    // (https://specifications.freedesktop.org/basedir-spec/basedir-spec-0.6.html)
    // if the user's config directory does not exist, we should create it with permissions set to 0700.
    if (!g_file_test(g_get_user_config_dir(), G_FILE_TEST_IS_DIR))
        g_mkdir_with_parents(g_get_user_config_dir(), 0700);

    gchar *path2 = 0;
    system_dirs = g_get_system_config_dirs();
    for (int i = 0; system_dirs[i]; i++) {
        path2 = g_build_filename(system_dirs[i], "tint2", "tint2rc", NULL);

        if (g_file_test(path2, G_FILE_TEST_EXISTS))
            break;
        g_free(path2);
        path2 = 0;
    }

    if (path2) {
        // copy file in user directory (path1)
        gchar *dir = g_build_filename(g_get_user_config_dir(), "tint2", NULL);
        if (!g_file_test(dir, G_FILE_TEST_IS_DIR))
            g_mkdir_with_parents(dir, 0700);
        g_free(dir);

        path1 = g_build_filename(g_get_user_config_dir(), "tint2", "tint2rc", NULL);
        copy_file(path2, path1);
        g_free(path2);

        gboolean result = config_read_file(path1);
        config_path = strdup(path1);
        g_free(path1);
        return result;
    }

    // generate config file
    gchar *dir = g_build_filename(g_get_user_config_dir(), "tint2", NULL);
    if (!g_file_test(dir, G_FILE_TEST_IS_DIR))
        g_mkdir_with_parents(dir, 0700);
    g_free(dir);

    path1 = g_build_filename(g_get_user_config_dir(), "tint2", "tint2rc", NULL);
    FILE *f = fopen(path1, "w");
    if (f) {
        fwrite(themes_tint2rc, 1, themes_tint2rc_len, f);
        fclose(f);
    }

    gboolean result = config_read_file(path1);
    config_path = strdup(path1);
    g_free(path1);
    return result;
}

gboolean config_read()
{
    if (config_path)
        return config_read_file(config_path);
    return config_read_default_path();
}

#endif

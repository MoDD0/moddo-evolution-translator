/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Translate Preferences dialog implementation */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "translate-preferences.h"
#include "translate-utils.h"

typedef struct {
    const char *code;
    const char *name;
} Lang;

static const Lang k_langs[] = {
    {"en", N_("English")}, {"es", N_("Spanish")}, {"fr", N_("French")},
    {"de", N_("German")}, {"it", N_("Italian")}, {"pt", N_("Portuguese")},
    {"nl", N_("Dutch")}, {"sv", N_("Swedish")}, {"da", N_("Danish")},
    {"no", N_("Norwegian")}, {"fi", N_("Finnish")}, {"pl", N_("Polish")},
    {"ru", N_("Russian")}, {"uk", N_("Ukrainian")}, {"cs", N_("Czech")},
    {"sk", N_("Slovak")}, {"hu", N_("Hungarian")}, {"ro", N_("Romanian")},
    {"bg", N_("Bulgarian")}, {"el", N_("Greek")}, {"tr", N_("Turkish")},
    {"ar", N_("Arabic")}, {"he", N_("Hebrew")}, {"hi", N_("Hindi")},
    {"ja", N_("Japanese")}, {"ko", N_("Korean")}, {"zh", N_("Chinese")},
};

/* ============================================================================
 * Shortcut capture button
 * ============================================================================ */

typedef struct {
    gchar    *accel;         /* current accelerator string, owned */
    gboolean  capturing;
    gulong    key_handler_id;
    GtkWidget *dialog;
} ShortcutButtonData;

static void
shortcut_button_data_free (gpointer ptr)
{
    ShortcutButtonData *data = ptr;
    g_free (data->accel);
    g_free (data);
}

static gboolean
shortcut_key_press_cb (GtkWidget   *widget,
                       GdkEventKey *event,
                       gpointer     user_data)
{
    GtkButton *button = GTK_BUTTON (user_data);
    ShortcutButtonData *data = g_object_get_data (G_OBJECT (button), "shortcut-data");

    if (!data || !data->capturing)
        return FALSE;

    /* Ignore standalone modifier key presses */
    if (event->is_modifier)
        return TRUE;

    data->capturing = FALSE;
    g_signal_handler_disconnect (data->dialog, data->key_handler_id);
    data->key_handler_id = 0;

    if (event->keyval == GDK_KEY_Escape) {
        /* Restore previous label */
        guint kval = 0;
        GdkModifierType mods = 0;
        if (data->accel && *data->accel)
            gtk_accelerator_parse (data->accel, &kval, &mods);
        gchar *label = kval ? gtk_accelerator_get_label (kval, mods)
                            : g_strdup (data->accel && *data->accel ? data->accel : _("(none)"));
        gtk_button_set_label (button, label);
        g_free (label);
    } else {
        GdkModifierType mods = event->state & gtk_accelerator_get_default_mod_mask ();
        g_free (data->accel);
        data->accel = gtk_accelerator_name (event->keyval, mods);
        gchar *label = gtk_accelerator_get_label (event->keyval, mods);
        gtk_button_set_label (button, label);
        g_free (label);
    }

    return TRUE;
}

static void
shortcut_button_clicked_cb (GtkButton *button,
                             gpointer   user_data)
{
    ShortcutButtonData *data = g_object_get_data (G_OBJECT (button), "shortcut-data");

    if (!data || data->capturing)
        return;

    data->capturing = TRUE;
    gtk_button_set_label (button, _("Press shortcut\xe2\x80\xa6"));
    data->key_handler_id = g_signal_connect (data->dialog, "key-press-event",
                                              G_CALLBACK (shortcut_key_press_cb), button);
}

static GtkWidget *
create_shortcut_button (const gchar *accel,
                         GtkWidget   *dialog)
{
    ShortcutButtonData *data = g_new0 (ShortcutButtonData, 1);
    data->accel = g_strdup (accel ? accel : "");
    data->capturing = FALSE;
    data->key_handler_id = 0;
    data->dialog = dialog;

    guint kval = 0;
    GdkModifierType mods = 0;
    if (accel && *accel)
        gtk_accelerator_parse (accel, &kval, &mods);
    gchar *label = kval ? gtk_accelerator_get_label (kval, mods)
                        : g_strdup (accel && *accel ? accel : _("(none)"));
    GtkWidget *button = gtk_button_new_with_label (label);
    g_free (label);

    g_object_set_data_full (G_OBJECT (button), "shortcut-data", data,
                            shortcut_button_data_free);
    g_signal_connect (button, "clicked", G_CALLBACK (shortcut_button_clicked_cb), NULL);

    return button;
}

/* ============================================================================
 * Preferences dialog
 * ============================================================================ */

void
translate_preferences_show (GtkWindow *parent)
{
    g_autoptr(GSettings) settings = g_settings_new ("org.gnome.evolution.translate");
    GSettings *provider_settings = translate_utils_get_provider_settings ();

    GtkWidget *dlg = gtk_dialog_new_with_buttons (_("Translate Settings"), parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
        _("Cancel"), GTK_RESPONSE_CANCEL,
        _("Save"), GTK_RESPONSE_OK,
        NULL);

    GtkWidget *area = gtk_dialog_get_content_area (GTK_DIALOG (dlg));
    GtkWidget *grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
    gtk_container_set_border_width (GTK_CONTAINER (grid), 12);
    gtk_container_add (GTK_CONTAINER (area), grid);

    GtkWidget *lbl_lang = gtk_label_new (_("Target language:"));
    gtk_widget_set_halign (lbl_lang, GTK_ALIGN_START);
    GtkWidget *combo = gtk_combo_box_text_new ();
    for (guint i = 0; i < G_N_ELEMENTS (k_langs); i++)
        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), k_langs[i].code, k_langs[i].name);
    g_autofree gchar *cur = g_settings_get_string (settings, "target-language");
    gtk_combo_box_set_active_id (GTK_COMBO_BOX (combo), cur && *cur ? cur : "en");

    GtkWidget *lbl_provider = gtk_label_new (_("Provider:"));
    gtk_widget_set_halign (lbl_provider, GTK_ALIGN_START);
    GtkWidget *provider_combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (provider_combo), "argos", "Argos Translate (offline, privacy-focused)");
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (provider_combo), "google", "Google Translate (online, free, recommended)");
    g_autofree gchar *cur_provider = g_settings_get_string (settings, "provider-id");
    gtk_combo_box_set_active_id (GTK_COMBO_BOX (provider_combo), cur_provider && *cur_provider ? cur_provider : "google");

    GtkWidget *lbl_venv = gtk_label_new (_("Argos venv path (optional):"));
    gtk_widget_set_halign (lbl_venv, GTK_ALIGN_START);
    GtkWidget *venv_entry = gtk_entry_new ();

    GtkWidget *install_on_demand = gtk_check_button_new_with_label (_("Install models on demand"));
    /* Load current install-on-demand setting */
    gboolean current_install_on_demand = g_settings_get_boolean (provider_settings, "install-on-demand");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (install_on_demand), current_install_on_demand);

    /* Load current shortcut */
    g_autofree gchar *cur_translate_shortcut = g_settings_get_string (settings, "translate-shortcut");

    GtkWidget *lbl_translate_shortcut = gtk_label_new (_("Translate shortcut:"));
    gtk_widget_set_halign (lbl_translate_shortcut, GTK_ALIGN_START);
    GtkWidget *translate_shortcut_btn = create_shortcut_button (cur_translate_shortcut, dlg);

    gtk_grid_attach (GTK_GRID (grid), lbl_lang, 0, 0, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), combo, 1, 0, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), lbl_provider, 0, 1, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), provider_combo, 1, 1, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), lbl_venv, 0, 2, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), venv_entry, 1, 2, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), install_on_demand, 1, 3, 1, 1);
    GtkWidget *lbl_shortcut_hint = gtk_label_new ("Takes effect after restarting Evolution.");
    gtk_widget_set_halign (lbl_shortcut_hint, GTK_ALIGN_START);
    gtk_style_context_add_class (gtk_widget_get_style_context (lbl_shortcut_hint), "dim-label");

    gtk_grid_attach (GTK_GRID (grid), lbl_translate_shortcut, 0, 4, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), translate_shortcut_btn, 1, 4, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), lbl_shortcut_hint, 1, 5, 1, 1);

    gtk_widget_show_all (dlg);
    if (gtk_dialog_run (GTK_DIALOG (dlg)) == GTK_RESPONSE_OK) {
        /* Save target language setting */
        const gchar *sel = gtk_combo_box_get_active_id (GTK_COMBO_BOX (combo));
        if (sel && *sel)
            g_settings_set_string (settings, "target-language", sel);

        /* Save provider selection */
        const gchar *sel_provider = gtk_combo_box_get_active_id (GTK_COMBO_BOX (provider_combo));
        if (sel_provider && *sel_provider)
            g_settings_set_string (settings, "provider-id", sel_provider);

        /* Save install-on-demand setting */
        gboolean install_enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (install_on_demand));
        g_settings_set_boolean (provider_settings, "install-on-demand", install_enabled);

        /* Save shortcut */
        ShortcutButtonData *ts_data = g_object_get_data (G_OBJECT (translate_shortcut_btn), "shortcut-data");
        if (ts_data && ts_data->accel)
            g_settings_set_string (settings, "translate-shortcut", ts_data->accel);

        /* venv_entry not yet implemented */
        (void)venv_entry;
    }
    gtk_widget_destroy (dlg);
}

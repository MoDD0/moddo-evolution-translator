/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Translate DOM helpers - Manages DOM state during translation
 *
 * This module handles:
 * - Storing original message state before translation
 * - Applying translated HTML to the display
 * - Restoring original messages
 * - Detecting message changes to clear stale translations
 * - Caching translations so returning to a message re-applies them
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <gtk/gtk.h>
#include <mail/e-mail-reader.h>
#include <mail/e-mail-view.h>
#include <mail/e-mail-paned-view.h>
#include <mail/e-mail-display.h>
#include <e-util/e-util.h>

#include "translate-dom.h"

/* Internal state structure to track original message */
typedef struct {
    EMailPartList *original_part_list;
    CamelMimeMessage *original_message;
    gchar *original_message_uid;
} DomState;

/* Global state table: EMailDisplay* → DomState* */
static GHashTable *s_states;

/* Translation cache: message_uid (gchar*) → translated_html (gchar*) */
static GHashTable *s_translation_cache;

/* Free a DomState structure */
static void
free_dom_state (gpointer data)
{
    DomState *st = data;
    if (st) {
        g_clear_object (&st->original_part_list);
        g_clear_object (&st->original_message);
        g_free (st->original_message_uid);
        g_free (st);
    }
}

static void
ensure_state_table (void)
{
    if (!s_states) {
        s_states = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, free_dom_state);
    }
}

static void
ensure_translation_cache (void)
{
    if (!s_translation_cache) {
        s_translation_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    }
}

/* Extract EMailDisplay from EShellView */
static EMailDisplay *
get_display_from_shell_view (EShellView *shell_view)
{
    EShellContent *shell_content;
    EMailView *mail_view = NULL;
    shell_content = e_shell_view_get_shell_content (shell_view);
    g_object_get (shell_content, "mail-view", &mail_view, NULL);
    if (!mail_view)
        return NULL;
    EMailDisplay *display = e_mail_reader_get_mail_display (E_MAIL_READER (mail_view));
    g_object_unref (mail_view);
    return display;
}

/* Extract EMailDisplay from EMailReader */
static EMailDisplay *
get_display_from_reader (EMailReader *reader)
{
    return e_mail_reader_get_mail_display (reader);
}

/* Get the current message UID from a display's part list */
static const gchar *
get_current_uid (EMailDisplay *display)
{
    EMailPartList *pl = e_mail_display_get_part_list (display);
    return pl ? e_mail_part_list_get_message_uid (pl) : NULL;
}

/* ============================================================================
 * INTERNAL HELPER FUNCTIONS
 * ============================================================================ */

/**
 * apply_translation_internal:
 *
 * Applies translated HTML to a display and caches the result.
 * Uses e_web_view_load_string() which fully replaces the web view content.
 */
static void
apply_translation_internal (EMailDisplay *display,
                            const gchar *translated_html,
                            gboolean verbose_logging)
{
    ensure_state_table ();
    ensure_translation_cache ();
    if (!display) return;

    EMailPartList *current_part_list = e_mail_display_get_part_list (display);
    const gchar *current_uid = get_current_uid (display);

    DomState *existing_state = g_hash_table_lookup (s_states, display);

    /* If state exists but it's for a different message, clear it first */
    if (existing_state) {
        gboolean is_same_message = FALSE;
        if (current_uid && existing_state->original_message_uid) {
            is_same_message = (g_strcmp0 (current_uid, existing_state->original_message_uid) == 0);
        }

        if (!is_same_message) {
            g_message ("[translate] Clearing old translation state for different message");
            g_hash_table_remove (s_states, display);
            existing_state = NULL;
        }
    }

    /* Create new state if needed */
    if (!existing_state) {
        DomState *st = g_new0 (DomState, 1);
        st->original_part_list = current_part_list;
        if (st->original_part_list) {
            g_object_ref (st->original_part_list);
            if (current_uid) {
                st->original_message_uid = g_strdup (current_uid);
            }
        }
        g_hash_table_insert (s_states, display, st);

        if (verbose_logging) {
            g_message ("[translate] Created new translation state for message UID: %s",
                       current_uid ? current_uid : "(none)");
        }
    }

    /* Load translated HTML into the web view FIRST — translated_html may
     * point into s_translation_cache, and the insert below frees the old
     * value.  e_web_view_load_string copies the string internally. */
    e_web_view_load_string (E_WEB_VIEW (display), translated_html ? translated_html : "");

    /* Cache the translation by message UID */
    if (current_uid) {
        g_hash_table_insert (s_translation_cache,
                             g_strdup (current_uid),
                             g_strdup (translated_html));
    }

    if (verbose_logging) {
        g_message ("[translate] Applied translated content (%zu bytes) to preview",
                   translated_html ? strlen (translated_html) : 0UL);
    }
}

/**
 * restore_original_internal:
 *
 * Restores the original message and removes the translation from cache.
 */
static void
restore_original_internal (EMailDisplay *display,
                           EMailReader *reader,
                           EShellView *shell_view)
{
    ensure_state_table ();
    ensure_translation_cache ();
    if (!display) return;

    DomState *st = g_hash_table_lookup (s_states, display);
    if (!st) return;

    /* Remove cached translation so the message stays in original language */
    if (st->original_message_uid) {
        g_hash_table_remove (s_translation_cache, st->original_message_uid);
    }

    /* Force reload of the original message */
    if (st->original_part_list) {
        e_mail_display_set_part_list (display, st->original_part_list);
        e_mail_display_load (display, NULL);

        if (reader) {
            e_mail_reader_reload (reader);
        } else if (shell_view) {
            EShellContent *shell_content = e_shell_view_get_shell_content (shell_view);
            EMailView *mail_view = NULL;
            g_object_get (shell_content, "mail-view", &mail_view, NULL);
            if (mail_view && E_IS_MAIL_READER (mail_view)) {
                e_mail_reader_reload (E_MAIL_READER (mail_view));
            }
            if (mail_view)
                g_object_unref (mail_view);
        }
    }

    g_hash_table_remove (s_states, display);
    g_message ("[translate] Restored original content");
}

static gboolean
is_translated_internal (EMailDisplay *display)
{
    ensure_state_table ();
    if (!display) return FALSE;
    return g_hash_table_contains (s_states, display);
}

/**
 * clear_if_message_changed_internal:
 *
 * Clears stale translation state when the displayed message changes.
 */
static void
clear_if_message_changed_internal (EMailDisplay *display)
{
    ensure_state_table ();
    if (!display) return;

    DomState *existing_state = g_hash_table_lookup (s_states, display);
    if (!existing_state) return;

    const gchar *current_uid = get_current_uid (display);

    gboolean is_same_message = FALSE;
    if (current_uid && existing_state->original_message_uid) {
        is_same_message = (g_strcmp0 (current_uid, existing_state->original_message_uid) == 0);
    }

    if (!is_same_message) {
        g_message ("[translate] Message changed (stored: %s, current: %s) - clearing stale state",
                   existing_state->original_message_uid ? existing_state->original_message_uid : "(none)",
                   current_uid ? current_uid : "(none)");
        g_hash_table_remove (s_states, display);
    }
}

/**
 * has_cached_translation_internal:
 *
 * Checks if the current message has a cached translation.
 * If so, applies it instantly and returns TRUE.
 * Called from the translate action to avoid redundant network requests.
 */
static gboolean
has_cached_translation_internal (EMailDisplay *display)
{
    ensure_translation_cache ();
    if (!display) return FALSE;

    const gchar *uid = get_current_uid (display);
    if (!uid) return FALSE;

    const gchar *cached = g_hash_table_lookup (s_translation_cache, uid);
    if (!cached) return FALSE;

    g_message ("[translate] Applying cached translation for UID: %s", uid);
    apply_translation_internal (display, cached, FALSE);
    return TRUE;
}

/* ============================================================================
 * PUBLIC API - SHELL VIEW VARIANTS
 * ============================================================================ */

void
translate_dom_apply_to_shell_view (EShellView *shell_view,
                                   const gchar *translated_html)
{
    EMailDisplay *display = get_display_from_shell_view (shell_view);
    apply_translation_internal (display, translated_html, TRUE);
}

void
translate_dom_restore_original (EShellView *shell_view)
{
    EMailDisplay *display = get_display_from_shell_view (shell_view);
    restore_original_internal (display, NULL, shell_view);
}

gboolean
translate_dom_is_translated (EShellView *shell_view)
{
    EMailDisplay *display = get_display_from_shell_view (shell_view);
    return is_translated_internal (display);
}

void
translate_dom_clear_if_message_changed (EShellView *shell_view)
{
    EMailDisplay *display = get_display_from_shell_view (shell_view);
    clear_if_message_changed_internal (display);
}

gboolean
translate_dom_apply_cached (EShellView *shell_view)
{
    EMailDisplay *display = get_display_from_shell_view (shell_view);
    return has_cached_translation_internal (display);
}

/* ============================================================================
 * PUBLIC API - READER VARIANTS
 * ============================================================================ */

void
translate_dom_apply_to_reader (EMailReader *reader,
                                const gchar *translated_html)
{
    EMailDisplay *display = get_display_from_reader (reader);
    apply_translation_internal (display, translated_html, FALSE);
}

void
translate_dom_restore_original_reader (EMailReader *reader)
{
    EMailDisplay *display = get_display_from_reader (reader);
    restore_original_internal (display, reader, NULL);
}

gboolean
translate_dom_is_translated_reader (EMailReader *reader)
{
    EMailDisplay *display = get_display_from_reader (reader);
    return is_translated_internal (display);
}

void
translate_dom_clear_if_message_changed_reader (EMailReader *reader)
{
    EMailDisplay *display = get_display_from_reader (reader);
    clear_if_message_changed_internal (display);
}

gboolean
translate_dom_apply_cached_reader (EMailReader *reader)
{
    EMailDisplay *display = get_display_from_reader (reader);
    return has_cached_translation_internal (display);
}

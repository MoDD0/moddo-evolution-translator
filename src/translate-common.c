/* SPDX-License-Identifier: LGPL-2.1-or-later */
/**
 * translate-common.c
 * Common translation logic shared across UI components
 *
 * This module centralizes the translation request logic that was previously
 * duplicated in translate-mail-ui.c and translate-browser-extension.c.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <e-util/e-util.h>
#include <shell/e-shell.h>
#include "translate-common.h"
#include "translate-utils.h"
#include "providers/translate-provider.h"

/**
 * TranslateAsyncData:
 * Internal structure for managing async translation with activity feedback
 */
typedef struct {
    GAsyncReadyCallback user_callback;
    gpointer user_data;
    EActivity *activity;
    gchar *provider_name;
} TranslateAsyncData;

/**
 * internal_translate_callback:
 * Internal callback that wraps user callback and updates activity status.
 * Uses g_task_had_error() to check success without consuming the GAsyncResult,
 * so the user callback can still call translate_provider_translate_finish().
 */
static void
internal_translate_callback (GObject      *source_object,
                             GAsyncResult *res,
                             gpointer      user_data)
{
    TranslateAsyncData *data = (TranslateAsyncData*)user_data;

    /* Update activity status without consuming the GAsyncResult.
     * g_task_had_error() peeks at the result without transferring ownership. */
    if (data->activity) {
        if (G_IS_TASK (res) && g_task_had_error (G_TASK (res))) {
            e_activity_set_state (data->activity, E_ACTIVITY_CANCELLED);
            e_activity_set_text (data->activity, "Translation failed");
        } else {
            g_autofree gchar *msg = g_strdup_printf (
                "Text translated by %s.",
                data->provider_name ? data->provider_name : "translator"
            );
            e_activity_set_state (data->activity, E_ACTIVITY_COMPLETED);
            e_activity_set_text (data->activity, msg);
        }
        g_object_unref (data->activity);
    }

    /* Call user's original callback — they can consume the GAsyncResult normally */
    if (data->user_callback) {
        data->user_callback (source_object, res, data->user_data);
    }

    g_free (data->provider_name);
    g_free (data);
}

/**
 * translate_common_start_translation:
 * @body_html: HTML content to translate
 * @cancellable: (nullable): optional GCancellable
 * @callback: callback for when translation completes
 * @user_data: data for callback
 *
 * Shared helper that creates the provider and starts the async translation.
 * Used by both translate_common_translate_async and the activity variant.
 *
 * Returns: the provider display name (only valid until provider_obj is freed),
 *          or NULL on failure.
 */
static const gchar *
translate_common_start_translation (const gchar         *body_html,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
    /* Get target language from settings */
    g_autofree gchar *target_lang = translate_utils_get_target_language ();

    /* Get provider ID from settings */
    g_autofree gchar *provider_id = translate_utils_get_provider_id ();
    if (!provider_id || !*provider_id) {
        provider_id = g_strdup ("google");
    }

    /* Create the translation provider */
    g_autoptr(GObject) provider_obj = translate_provider_new_by_id (provider_id);
    if (!provider_obj) {
        g_warning ("[translate] No provider found for '%s'", provider_id);
        return NULL;
    }

    const gchar *provider_name = translate_provider_get_name ((TranslateProvider*)provider_obj);

    translate_provider_translate_async ((TranslateProvider*)provider_obj,
                                        body_html,
                                        TRUE,  /* is_html */
                                        NULL,  /* source (auto-detect) */
                                        target_lang,
                                        cancellable,
                                        callback,
                                        user_data);

    return provider_name;
}

/**
 * translate_common_translate_async:
 * @body_html: The HTML content to translate
 * @callback: (scope async): Callback to invoke when translation completes
 * @user_data: User data to pass to the callback
 *
 * Initiates an asynchronous translation of the provided HTML content.
 * The callback will receive the result; use translate_provider_translate_finish()
 * to retrieve the translated text.
 */
void
translate_common_translate_async (const gchar         *body_html,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
    g_return_if_fail (body_html != NULL);
    g_return_if_fail (*body_html != '\0');
    g_return_if_fail (callback != NULL);

    translate_common_start_translation (body_html, NULL, callback, user_data);
}

/**
 * translate_common_translate_async_with_activity:
 * @body_html: The HTML content to translate
 * @shell_backend: EShellBackend to display activity status
 * @callback: (scope async): Callback to invoke when translation completes
 * @user_data: User data to pass to the callback
 *
 * Initiates an asynchronous translation with status bar activity feedback.
 * Shows "Requesting translation from <provider>..." while running, then
 * "Text translated by <provider>." on success or "Translation failed" on error.
 */
void
translate_common_translate_async_with_activity (const gchar         *body_html,
                                                 EShellBackend       *shell_backend,
                                                 GAsyncReadyCallback  callback,
                                                 gpointer             user_data)
{
    g_return_if_fail (body_html != NULL);
    g_return_if_fail (*body_html != '\0');
    g_return_if_fail (E_IS_SHELL_BACKEND (shell_backend));

    /* Get provider name before starting (need it for the activity message) */
    g_autofree gchar *provider_id = translate_utils_get_provider_id ();
    if (!provider_id || !*provider_id) {
        provider_id = g_strdup ("google");
    }
    g_autoptr(GObject) provider_obj = translate_provider_new_by_id (provider_id);
    const gchar *provider_name = provider_obj
        ? translate_provider_get_name ((TranslateProvider*)provider_obj)
        : provider_id;
    if (!provider_name || !*provider_name)
        provider_name = provider_id;

    /* Create and configure the activity */
    EActivity *activity = e_activity_new ();
    g_autofree gchar *initial_msg = g_strdup_printf (
        "Requesting translation from %s...", provider_name
    );
    e_activity_set_text (activity, initial_msg);
    e_activity_set_state (activity, E_ACTIVITY_RUNNING);

    /* Add to backend — makes it visible in the status bar */
    e_shell_backend_add_activity (shell_backend, activity);

    /* Prepare wrapper data (takes ownership of activity ref) */
    TranslateAsyncData *data = g_new0 (TranslateAsyncData, 1);
    data->user_callback = callback;
    data->user_data = user_data;
    data->activity = activity;  /* transfer ref to data */
    data->provider_name = g_strdup (provider_name);

    translate_common_start_translation (body_html, NULL,
                                        internal_translate_callback, data);
}

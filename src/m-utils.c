/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* m-utils.c
 * Simple UI manager utilities for enabling/disabling actions.
 * Supports both legacy GtkUIManager and new EUIManager APIs.
 */

#include "m-utils.h"

#ifdef HAVE_EUI_MANAGER

void
m_utils_enable_actions_by_name (EUIManager  *ui_manager,
                                 const gchar *group_name,
                                 const gchar * const *action_names,
                                 gboolean     enable)
{
    EUIActionGroup *group;

    if (!ui_manager || !group_name || !action_names)
        return;

    group = e_ui_manager_get_action_group (ui_manager, group_name);
    if (!group)
        return;

    for (guint i = 0; action_names[i] != NULL; i++) {
        EUIAction *action = e_ui_action_group_get_action (group, action_names[i]);
        if (action)
            e_ui_action_set_sensitive (action, enable);
    }
}

#else /* Legacy GtkUIManager API */

void
m_utils_enable_actions (GtkUIManager *ui_manager,
                        const GtkActionEntry *entries,
                        guint n_entries,
                        gboolean enable)
{
    GtkActionGroup *action_group;
    GtkAction *action;
    guint i;

    if (!ui_manager)
        return;

    GList *groups = gtk_ui_manager_get_action_groups (ui_manager);
    if (!groups)
        return;

    for (i = 0; i < n_entries; i++) {
        GList *group_iter;
        for (group_iter = groups; group_iter; group_iter = group_iter->next) {
            action_group = GTK_ACTION_GROUP (group_iter->data);
            action = gtk_action_group_get_action (action_group, entries[i].name);

            if (action) {
                gtk_action_set_sensitive (action, enable);
                break;
            }
        }
    }
}

#endif /* HAVE_EUI_MANAGER */

/* gtd-panel-scheduled.c
 *
 * Copyright (C) 2015 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gtd-panel-scheduled.h"

#include <glib/gi18n.h>
#include <gnome-todo.h>

struct _GtdPanelScheduled
{
  GtkBox              parent;

  GMenu              *menu;

  gchar              *title;
  guint               number_of_tasks;
  GtdTaskList        *task_list;
  GtkWidget          *view;
};

static void          gtd_panel_iface_init                        (GtdPanelInterface  *iface);

G_DEFINE_TYPE_EXTENDED (GtdPanelScheduled, gtd_panel_scheduled, GTK_TYPE_BOX,
                        0,
                        G_IMPLEMENT_INTERFACE (GTD_TYPE_PANEL,
                                               gtd_panel_iface_init))

#define GTD_PANEL_SCHEDULED_NAME "panel-scheduled"

enum {
  PROP_0,
  PROP_MENU,
  PROP_NAME,
  PROP_TITLE,
  N_PROPS
};

static gchar*
get_string_for_date (GDateTime *dt,
                     gint      *span)
{
  GDateTime *now;
  gchar *str;
  gint days_diff;

  /* This case should never happen */
  if (!dt)
    return g_strdup (_("No date set"));

  now = g_date_time_new_now_local ();
  days_diff = g_date_time_difference (dt, now) / G_TIME_SPAN_DAY;

  if (days_diff < -1)
    {
      str = g_strdup_printf (_("%d days ago"), -days_diff);
    }
  else if (days_diff == -1)
    {
      str = g_strdup (_("Yesterday"));
    }
  else if (days_diff == 0)
    {
      str = g_strdup (_("Today"));
    }
  else if (days_diff == 1)
    {
      str = g_strdup (_("Tomorrow"));
    }
  else if (days_diff > 1 && days_diff < 7)
    {
      str = g_date_time_format (dt, "%A"); // Weekday name
    }
  else if (days_diff >= 7 && days_diff < (365 + g_date_time_get_year (dt) % 4 == 0))
    {
      str = g_date_time_format (dt, "%B"); // Full month name
    }
  else
    {
      str = g_strdup_printf ("%d", g_date_time_get_year (dt));
    }

  if (span)
    *span = days_diff;

  g_clear_pointer (&now, g_date_time_unref);

  return str;
}

static GtkWidget*
create_label (const gchar *text,
              gint         span,
              gboolean     first_header)
{
  GtkStyleContext *context;
  GtkWidget *label;
  GtkWidget *box;

  label = g_object_new (GTK_TYPE_LABEL,
                        "label", text,
                        "margin-left", 12,
                        "margin-bottom", 6,
                        "margin-top", first_header ? 6 : 18,
                        "xalign", 0,
                        "hexpand", TRUE,
                        NULL);

  context = gtk_widget_get_style_context (label);
  gtk_style_context_add_class (context, span < 0 ? "date-overdue" : "date-scheduled");

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  gtk_container_add (GTK_CONTAINER (box), label);
  gtk_container_add (GTK_CONTAINER (box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));

  gtk_widget_show_all (box);

  return box;
}

static void
gtd_panel_scheduled_header_func (GtkListBoxRow     *row,
                                 GtdTask           *row_task,
                                 GtkListBoxRow     *before,
                                 GtdTask           *before_task,
                                 GtdPanelScheduled *panel)
{
  GDateTime *dt;
  gchar *text;
  gint span;

  dt = gtd_task_get_due_date (row_task);

  if (!before)
    {
      text = get_string_for_date (dt, &span);

      gtk_list_box_row_set_header (row, create_label (text,
                                                      span,
                                                      TRUE));

      g_free (text);
    }
  else
    {
      GDateTime *before_dt;
      gint diff;

      before_dt = gtd_task_get_due_date (before_task);
      diff = g_date_time_difference (dt, before_dt) / G_TIME_SPAN_DAY;

      if (diff != 0)
        {
          text = get_string_for_date (dt, &span);

          gtk_list_box_row_set_header (row, create_label (text,
                                                          span,
                                                          FALSE));

          g_free (text);
        }
      else
        {
          gtk_list_box_row_set_header (row, NULL);
        }

      g_clear_pointer (&before_dt, g_date_time_unref);
    }

  g_clear_pointer (&dt, g_date_time_unref);
}

static void
gtd_panel_scheduled_clear (GtdPanelScheduled *panel)
{
  GList *tasks;
  GList *t;

  tasks = gtd_task_list_get_tasks (panel->task_list);

  for (t = tasks; t != NULL; t = t->next)
    gtd_task_list_remove_task (panel->task_list, t->data);

  g_list_free (tasks);
}

static void
gtd_panel_scheduled_count_tasks (GtdPanelScheduled *panel)
{
  GtdManager *manager;
  GList *tasklists;
  GList *l;
  guint number_of_tasks;

  manager = gtd_manager_get_default ();
  tasklists = gtd_manager_get_task_lists (manager);
  number_of_tasks = 0;

  /* Reset list */
  gtd_panel_scheduled_clear (panel);

  /* Recount tasks */
  for (l = tasklists; l != NULL; l = l->next)
    {
      GList *tasks;
      GList *t;

      tasks = gtd_task_list_get_tasks (l->data);

      for (t = tasks; t != NULL; t = t->next)
        {
          GDateTime *task_dt;

          task_dt = gtd_task_get_due_date (t->data);

          /*
           * GtdTaskListView automagically updates the list
           * whever a task is added/removed/changed.
           */
          if (task_dt)
            {
              gtd_task_list_save_task (panel->task_list, t->data);

              if (!gtd_task_get_complete (t->data))
                number_of_tasks++;
            }

          g_clear_pointer (&task_dt, g_date_time_unref);
        }

      g_list_free (tasks);
    }

  if (number_of_tasks != panel->number_of_tasks)
    {
      panel->number_of_tasks = number_of_tasks;

      /* Update title */
      g_clear_pointer (&panel->title, g_free);
      if (number_of_tasks == 0)
        {
          panel->title = g_strdup (_("Scheduled"));
        }
      else
        {
          panel->title = g_strdup_printf ("%s (%d)",
                                          _("Scheduled"),
                                          panel->number_of_tasks);
        }

      g_object_notify (G_OBJECT (panel), "title");
    }

  g_list_free (tasklists);
}

/**********************
 * GtdPanel iface init
 **********************/
static const gchar*
gtd_panel_scheduled_get_panel_name (GtdPanel *panel)
{
  return GTD_PANEL_SCHEDULED_NAME;
}

static const gchar*
gtd_panel_scheduled_get_panel_title (GtdPanel *panel)
{
  return GTD_PANEL_SCHEDULED (panel)->title;
}

static GList*
gtd_panel_scheduled_get_header_widgets (GtdPanel *panel)
{
  return NULL;
}

static const GMenu*
gtd_panel_scheduled_get_menu (GtdPanel *panel)
{
  return GTD_PANEL_SCHEDULED (panel)->menu;
}

static void
gtd_panel_iface_init (GtdPanelInterface *iface)
{
  iface->get_panel_name = gtd_panel_scheduled_get_panel_name;
  iface->get_panel_title = gtd_panel_scheduled_get_panel_title;
  iface->get_header_widgets = gtd_panel_scheduled_get_header_widgets;
  iface->get_menu = gtd_panel_scheduled_get_menu;
}

static void
gtd_panel_scheduled_finalize (GObject *object)
{
  GtdPanelScheduled *self = (GtdPanelScheduled *)object;

  g_clear_pointer (&self->title, g_free);

  G_OBJECT_CLASS (gtd_panel_scheduled_parent_class)->finalize (object);
}

static void
gtd_panel_scheduled_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  GtdPanelScheduled *self = GTD_PANEL_SCHEDULED (object);

  switch (prop_id)
    {
    case PROP_MENU:
      g_value_set_object (value, NULL);
      break;

    case PROP_NAME:
      g_value_set_string (value, GTD_PANEL_SCHEDULED_NAME);
      break;

    case PROP_TITLE:
      g_value_set_string (value, self->title);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_panel_scheduled_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
gtd_panel_scheduled_class_init (GtdPanelScheduledClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gtd_panel_scheduled_finalize;
  object_class->get_property = gtd_panel_scheduled_get_property;
  object_class->set_property = gtd_panel_scheduled_set_property;

  g_object_class_override_property (object_class, PROP_MENU, "menu");
  g_object_class_override_property (object_class, PROP_NAME, "name");
  g_object_class_override_property (object_class, PROP_TITLE, "title");
}

static void
gtd_panel_scheduled_init (GtdPanelScheduled *self)
{
  GtdManager *manager;

  /* Connect to GtdManager::list-* signals to update the title */
  manager = gtd_manager_get_default ();

  g_signal_connect_swapped (manager,
                            "list-added",
                            G_CALLBACK (gtd_panel_scheduled_count_tasks),
                            self);

  g_signal_connect_swapped (manager,
                            "list-removed",
                            G_CALLBACK (gtd_panel_scheduled_count_tasks),
                            self);

  g_signal_connect_swapped (manager,
                            "list-changed",
                            G_CALLBACK (gtd_panel_scheduled_count_tasks),
                            self);

  /* Setup a title */
  self->title = g_strdup (_("Scheduled"));

  /* Task list */
  self->task_list = gtd_task_list_new (NULL);

  /* Menu */
  self->menu = g_menu_new ();
  g_menu_append (self->menu,
                 _("Clear completed tasks…"),
                 "list.clear-completed-tasks");

  /* The main view */
  self->view = gtd_task_list_view_new ();
  gtd_task_list_view_set_show_list_name (GTD_TASK_LIST_VIEW (self->view), TRUE);
  gtd_task_list_view_set_task_list (GTD_TASK_LIST_VIEW (self->view), self->task_list);

  gtk_widget_set_hexpand (self->view, TRUE);
  gtk_widget_set_vexpand (self->view, TRUE);
  gtk_container_add (GTK_CONTAINER (self), self->view);

  gtd_task_list_view_set_header_func (GTD_TASK_LIST_VIEW (self->view),
                                      (GtdTaskListViewHeaderFunc) gtd_panel_scheduled_header_func,
                                      self);

  gtk_widget_show_all (GTK_WIDGET (self));
}

GtkWidget*
gtd_panel_scheduled_new (void)
{
  return g_object_new (GTD_TYPE_PANEL_SCHEDULED, NULL);
}


/*
 * Copyright (C) 2020 Filip Szymański <fszymanski(dot)pl(at)gmail(dot)com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <geanyplugin.h>

#define MAX_RECENT_FILES 200

enum {
  ICON_COLUMN,
  BASENAME_COLUMN,
  FILENAME_COLUMN,
  COLUMN_COUNT
};

enum {
  KB_GOTO_FILE,
  KB_COUNT
};

GeanyPlugin *geany_plugin;
GeanyData *geany_data;

static GList *get_current_documents_dir_files(void)
{
  const gchar *basename;
  gchar *dirname, *doc_filename, *filename;
  GDir *dir;
  guint i;
  GList *files = NULL;

  foreach_document(i) {
    doc_filename = utils_get_locale_from_utf8(DOC_FILENAME(documents[i]));
    if (g_file_test(doc_filename, G_FILE_TEST_EXISTS)) {
      dirname = g_path_get_dirname(doc_filename);
      dir = g_dir_open(dirname, 0, NULL);
      while ((basename = g_dir_read_name(dir)) != NULL) {
        filename = g_strjoin(G_DIR_SEPARATOR_S, dirname, basename, NULL);
        if (!g_file_test(filename, G_FILE_TEST_IS_DIR)) {
          // TODO: Do something with these files
        }

        g_free(filename);
      }

      g_free(dirname);
      g_dir_close(dir);
    }

    g_free(doc_filename);
  }

  return files;
}

/* Get recently used files */

static gint sort_recent_files(GtkRecentInfo *a, GtkRecentInfo *b)
{
  return (gtk_recent_info_get_modified(b) - gtk_recent_info_get_modified(a));
}

static GList *get_recent_files(void)
{
  GList *l, *recent_files, *filtered_recent_files = NULL;
  GtkRecentManager *manager;

  manager = gtk_recent_manager_get_default();
  recent_files = gtk_recent_manager_get_items(manager);
  for (l = recent_files; l != NULL; l = l->next) {
    if (gtk_recent_info_has_group(l->data, "geany")) {
      filtered_recent_files = g_list_prepend(filtered_recent_files, l->data);
    }
  }

  return g_list_sort(filtered_recent_files, (GCompareFunc)sort_recent_files);
}

/* Create and fill the model */

static gboolean file_visible(GtkTreeModel *model, GtkTreeIter *iter, GtkEntry *filter_entry)
{
  gboolean result = TRUE;
  const gchar *needle;
  gchar *haystack;

  needle = gtk_entry_get_text(filter_entry);
  if (*needle == '\0') {
    return result;
  }

  gtk_tree_model_get(model, iter, BASENAME_COLUMN, &haystack, -1);

  result = (g_strstr_len(haystack, -1, needle) != NULL ? TRUE : FALSE);

  g_free(haystack);

  return result;
}

static GtkTreeModel *create_and_fill_model(GtkEntry *filter_entry)
{
  char *filename;
  GFileInfo *info;
  GFile *recent_file;
  GList *l, *recent_files;
  GtkListStore *store;
  GtkTreeIter iter;
  GtkTreeModel *filter;
  guint i = 0;

  store = gtk_list_store_new(COLUMN_COUNT, G_TYPE_ICON, G_TYPE_STRING, G_TYPE_STRING);

  recent_files = get_recent_files();
  for (l = recent_files; l != NULL; l = l->next) {
    recent_file = g_file_new_for_uri(gtk_recent_info_get_uri(l->data));
    if (g_file_query_exists(recent_file, NULL)) {
      info = g_file_query_info(recent_file, "standard::*", G_FILE_QUERY_INFO_NONE, NULL, NULL);
      filename = g_file_get_path(recent_file);

      gtk_list_store_append(store, &iter);
      gtk_list_store_set(store, &iter,
                         ICON_COLUMN, g_file_info_get_icon(info),
                         BASENAME_COLUMN, g_file_info_get_display_name(info),
                         FILENAME_COLUMN, filename,
                         -1);

      g_object_unref(info);
      g_free(filename);

      i++;
      if (i >= MAX_RECENT_FILES) {
        g_object_unref(recent_file);
        break;
      }
    }

    g_object_unref(recent_file);
  }

  g_list_free_full(g_steal_pointer(&recent_files), (GDestroyNotify)gtk_recent_info_unref);

  filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(store), NULL);

  g_object_unref(store);

  gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filter),
                                         (GtkTreeModelFilterVisibleFunc)file_visible,
                                         filter_entry, NULL);

  return filter;
}

/* Callbacks */

static gboolean on_key_press_event(GtkWidget *window, GdkEventKey *event, GtkTreeSelection *selection)
{
  char *filename;
  GtkTreeIter iter;
  GtkTreeModel *model;

  switch (event->keyval) {
    case GDK_KEY_Escape:
      gtk_widget_destroy(window);
      break;
    case GDK_KEY_Return:
      if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gtk_tree_model_get(model, &iter, FILENAME_COLUMN, &filename, -1);

        document_open_file(filename, FALSE, NULL, NULL);

        g_free(filename);

        gtk_widget_destroy(window);
      }

      break;
    default:
      break;
  }

  return FALSE;
}

static void on_row_activated(GtkTreeView *file_view, GtkTreePath *path,
                             G_GNUC_UNUSED GtkTreeViewColumn *column, GtkWidget *window)
{
  gchar *filename;
  GtkTreeIter iter;
  GtkTreeModel *model;

  model = gtk_tree_view_get_model(file_view);
  if (gtk_tree_model_get_iter(model, &iter, path)) {
    gtk_tree_model_get(model, &iter, FILENAME_COLUMN, &filename, -1);

    document_open_file(filename, FALSE, NULL, NULL);

    g_free(filename);

    gtk_widget_destroy(window);
  }
}

static void on_search_changed(G_GNUC_UNUSED GtkSearchEntry *filter_entry, GtkTreeModelFilter *filter)
{
  gtk_tree_model_filter_refilter(filter);
}

static void on_selection_changed(GtkTreeSelection *selection, GtkLabel *filename_label)
{
  gchar *filename = NULL;
  GtkTreeIter iter;
  GtkTreeModel *model;

  if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
    gtk_tree_model_get(model, &iter, FILENAME_COLUMN, &filename, -1);
  }

  gtk_label_set_text(filename_label, (filename != NULL ? filename : '\0'));

  g_free(filename);
}

static void on_goto_file_activate(G_GNUC_UNUSED GtkWidget *goto_file_menu_item, G_GNUC_UNUSED gpointer data)
{
  GtkCellRenderer *renderer;
  GtkTreeModel *filter;
  GtkTreeSelection *selection;
  GtkTreeViewColumn *column;
  GtkTreeView *file_view;
  GtkWidget *filename_label, *filter_entry, *scroller, *vbox;
  GtkWindow *window;

  filter_entry = gtk_search_entry_new();
  gtk_entry_grab_focus_without_selecting(GTK_ENTRY(filter_entry));
  gtk_entry_set_placeholder_text(GTK_ENTRY(filter_entry), _("Search"));

  filter = create_and_fill_model(GTK_ENTRY(filter_entry));
  file_view = g_object_new(GTK_TYPE_TREE_VIEW,
                           "activate-on-single-click", TRUE,
                           "enable-search", FALSE,
                           "headers-visible", FALSE,
                           "model", filter,
                           NULL);

  g_object_unref(filter);

  renderer = gtk_cell_renderer_pixbuf_new();
  column = gtk_tree_view_column_new_with_attributes("icon",
                                                    renderer,
                                                    "gicon", ICON_COLUMN,
                                                    NULL);
  g_object_set(column,
               "resizable", FALSE,
               NULL);
  gtk_tree_view_append_column(file_view, column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes("basename",
                                                    renderer,
                                                    "text", BASENAME_COLUMN,
                                                    NULL);
  g_object_set(column,
               "resizable", TRUE,
               NULL);
  gtk_tree_view_append_column(file_view, column);

  selection = gtk_tree_view_get_selection(file_view);

  scroller = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(scroller), GTK_WIDGET(file_view));

  filename_label = gtk_label_new(NULL);
  gtk_widget_set_halign(filename_label, GTK_ALIGN_START);
  gtk_label_set_ellipsize(GTK_LABEL(filename_label), PANGO_ELLIPSIZE_MIDDLE);

  window = g_object_new(GTK_TYPE_WINDOW,
                        "default_width", 500,
                        "default_height", 360,
                        "destroy-with-parent", TRUE,
                        "modal", TRUE,
                        "title", _("Quick Open"),
                        "transient-for", GTK_WINDOW(geany_data->main_widgets->window),
                        "window-position", GTK_WIN_POS_CENTER_ON_PARENT,
                        NULL);

  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(vbox), filter_entry, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), scroller, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), filename_label, FALSE, FALSE, 0);
  gtk_container_add(GTK_CONTAINER(window), vbox);

  g_signal_connect(GTK_WIDGET(window), "key-press-event", G_CALLBACK(on_key_press_event), selection);
  g_signal_connect(file_view, "row-activated", G_CALLBACK(on_row_activated), GTK_WIDGET(window));
  g_signal_connect(filter_entry, "search-changed", G_CALLBACK(on_search_changed), filter);
  g_signal_connect(selection, "changed", G_CALLBACK(on_selection_changed), GTK_LABEL(filename_label));

  gtk_widget_show_all(GTK_WIDGET(window));
}

static void on_goto_file_kb(G_GNUC_UNUSED guint key_id)
{
  on_goto_file_activate(NULL, NULL);
}

/* Initialize and cleanup after the plugin */

static gboolean quickopen_init(GeanyPlugin *plugin, G_GNUC_UNUSED gpointer data)
{
  GtkWidget *goto_file_menu_item;
  GeanyKeyGroup *kb_group;

  geany_plugin = plugin;
  geany_data = plugin->geany_data;

  goto_file_menu_item = gtk_menu_item_new_with_mnemonic(_("Go to _File..."));
  gtk_container_add(GTK_CONTAINER(geany_data->main_widgets->tools_menu), goto_file_menu_item);
  gtk_widget_show(goto_file_menu_item);

  g_signal_connect(goto_file_menu_item, "activate", G_CALLBACK(on_goto_file_activate), NULL);

  geany_plugin_set_data(geany_plugin, goto_file_menu_item, NULL);

  kb_group = plugin_set_key_group(geany_plugin, "quickopen", KB_COUNT, NULL);
  keybindings_set_item(kb_group, KB_GOTO_FILE, on_goto_file_kb, 0, 0,  // Ctrl+P
                       "goto_file", _("Go to a file"), goto_file_menu_item);

  return TRUE;
}
static void quickopen_cleanup(G_GNUC_UNUSED GeanyPlugin *plugin, gpointer data)
{
  GtkWidget *goto_file_menu_item = (GtkWidget *)data;

  gtk_widget_destroy(goto_file_menu_item);
}

/* Load module */

G_MODULE_EXPORT void geany_load_module(GeanyPlugin *plugin)
{
  main_locale_init(LOCALEDIR, GETTEXT_PACKAGE);

  plugin->info->name = _("Quick Open");
  plugin->info->description = _("Quickly open a file");
  plugin->info->version = "0.1";
  plugin->info->author = "Filip Szymański <fszymanski(dot)pl(at)gmail(dot)com>";

  plugin->funcs->init = quickopen_init;
  plugin->funcs->cleanup = quickopen_cleanup;

  GEANY_PLUGIN_REGISTER(plugin, 225);
}

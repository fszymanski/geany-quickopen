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
  DISPLAY_NAME_COLUMN,
  FILENAME_COLUMN,
  COLUMN_COUNT
};

enum {
  KB_GOTO_FILE,
  KB_COUNT
};

GeanyPlugin *geany_plugin;
GeanyData *geany_data;

static GHashTable *unique_files;

static GtkWidget *desktop_dir_files_checkbox;
static GtkWidget *doc_dir_files_checkbox;
static GtkWidget *home_dir_files_checkbox;
static GtkWidget *recent_files_checkbox;

static gboolean config_desktop_dir_files;
static gboolean config_doc_dir_files;
static gboolean config_home_dir_files;
static gboolean config_recent_files;

static void add_files_from_path(const gchar *path)
{
  gchar *filename;
  GSList *filenames, *node;

  filenames = utils_get_file_list_full(path, TRUE, TRUE, NULL);
  if (filenames != NULL) {
    foreach_slist(node, filenames) {
      filename = node->data;
      if (!g_file_test(filename, G_FILE_TEST_IS_SYMLINK) && !g_file_test(filename, G_FILE_TEST_IS_DIR)) {
        g_hash_table_add(unique_files, filename);
      } else {
        g_free(filename);
      }
    }

    g_slist_free(g_steal_pointer(&filenames));
  }
}

static void get_desktop_dir_files(void)
{
  const gchar *desktop_dir;

  desktop_dir = g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP);
  if (desktop_dir != NULL) {
    add_files_from_path(desktop_dir);
  }
}

static void get_home_dir_files(void)
{
  add_files_from_path(g_get_home_dir());
}

static void get_open_document_dir_files(void)
{
  gchar *dirname, *filename;
  guint i;

  foreach_document(i) {
    filename = utils_get_locale_from_utf8(DOC_FILENAME(documents[i]));
    if (g_file_test(filename, G_FILE_TEST_EXISTS)) {
      dirname = g_path_get_dirname(filename);
      add_files_from_path(dirname);

      g_free(dirname);
    }

    g_free(filename);
  }
}

static gint sort_recent_info(GtkRecentInfo *a, GtkRecentInfo *b)
{
  return (gtk_recent_info_get_modified(b) - gtk_recent_info_get_modified(a));
}

static void get_recent_files(void)
{
  gchar *filename;
  GList *node, *recent_items, *filtered_recent_items = NULL;
  GtkRecentManager *manager;
  guint i = 0;

  manager = gtk_recent_manager_get_default();
  recent_items = gtk_recent_manager_get_items(manager);
  for (node = recent_items; node != NULL; node = node->next) {
    if (gtk_recent_info_has_group(node->data, "geany")) {
      filtered_recent_items = g_list_prepend(filtered_recent_items, node->data);
    }
  }

  filtered_recent_items = g_list_sort(filtered_recent_items, (GCompareFunc)sort_recent_info);

  for (node = filtered_recent_items; node != NULL; node = node->next) {
    filename = g_filename_from_uri(gtk_recent_info_get_uri(node->data), NULL, NULL);
    if (g_file_test(filename, G_FILE_TEST_EXISTS)) {
      g_hash_table_add(unique_files, filename);

      i++;
    } else {
      g_free(filename);
    }

    if (i >= MAX_RECENT_FILES) {
      break;
    }
  }

  g_list_free(g_steal_pointer(&filtered_recent_items));
  g_list_free_full(g_steal_pointer(&recent_items), (GDestroyNotify)gtk_recent_info_unref);
}

static gboolean file_visible(GtkTreeModel *model, GtkTreeIter *iter, GtkEntry *filter_entry)
{
  const gchar *needle;
  gboolean result = TRUE;
  gchar *haystack;

  needle = gtk_entry_get_text(filter_entry);
  if (*needle == '\0') {
    return result;
  }

  gtk_tree_model_get(model, iter, DISPLAY_NAME_COLUMN, &haystack, -1);

  result = (g_strstr_len(haystack, -1, needle) != NULL ? TRUE : FALSE);

  g_free(haystack);

  return result;
}

static GtkTreeModel *create_and_fill_model(GtkEntry *filter_entry)
{
  GFile *file;
  GFileInfo *info;
  GHashTableIter h_iter;
  gpointer filename, _;
  GtkListStore *store;
  GtkTreeIter t_iter;
  GtkTreeModel *filter;

  unique_files = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  if (config_desktop_dir_files) {
    get_desktop_dir_files();
  }

  if (config_doc_dir_files) {
    get_open_document_dir_files();
  }

  if (config_home_dir_files) {
    get_home_dir_files();
  }

  if (config_recent_files) {
    get_recent_files();
  }

  store = gtk_list_store_new(COLUMN_COUNT, G_TYPE_ICON, G_TYPE_STRING, G_TYPE_STRING);

  g_hash_table_iter_init(&h_iter, unique_files);
  while (g_hash_table_iter_next(&h_iter, &filename, &_)) {
    file = g_file_new_for_path(filename);
    info = g_file_query_info(file, "standard::*", G_FILE_QUERY_INFO_NONE, NULL, NULL);

    gtk_list_store_append(store, &t_iter);
    gtk_list_store_set(store, &t_iter,
                       ICON_COLUMN, g_file_info_get_icon(info),
                       DISPLAY_NAME_COLUMN, g_file_info_get_display_name(info),
                       FILENAME_COLUMN, filename,
                       -1);

    g_object_unref(file);
    g_object_unref(info);
  }

  g_hash_table_destroy(unique_files);

  filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(store), NULL);

  g_object_unref(store);

  gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filter),
                                         (GtkTreeModelFilterVisibleFunc)file_visible,
                                         filter_entry, NULL);

  return filter;
}

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
    case GDK_KEY_ISO_Enter:
    case GDK_KEY_KP_Enter:
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
  gchar *filename, *utf8_filename = NULL;
  GtkTreeIter iter;
  GtkTreeModel *model;

  if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
    gtk_tree_model_get(model, &iter, FILENAME_COLUMN, &filename, -1);

    utf8_filename = utils_get_utf8_from_locale(filename);

    g_free(filename);
  }

  gtk_label_set_text(filename_label, (utf8_filename != NULL ? utf8_filename : '\0'));

  g_free(utf8_filename);
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
  column = gtk_tree_view_column_new_with_attributes("display_name",
                                                    renderer,
                                                    "text", DISPLAY_NAME_COLUMN,
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

static gchar *get_config_filename(void)
{
  return g_build_filename(geany_data->app->configdir, "plugins", "quickopen", "quickopen.conf", NULL);
}

static void save_settings(void)
{
  gchar *config_dir, *data, *filename;
  GKeyFile *config;

  config = g_key_file_new();
  filename = get_config_filename();
  g_key_file_load_from_file(config, filename, G_KEY_FILE_NONE, NULL);

  config_dir = g_path_get_dirname(filename);
  if (!g_file_test(config_dir, G_FILE_TEST_IS_DIR) && utils_mkdir(config_dir, TRUE) != 0) {
    dialogs_show_msgbox(GTK_MESSAGE_ERROR, _("Plugin configuration directory could not be created."));
  } else {
    g_key_file_set_boolean(config, "quickopen", "desktop_dir_files", config_desktop_dir_files);
    g_key_file_set_boolean(config, "quickopen", "doc_dir_files", config_doc_dir_files);
    g_key_file_set_boolean(config, "quickopen", "home_dir_files", config_home_dir_files);
    g_key_file_set_boolean(config, "quickopen", "recent_files", config_recent_files);

    data = g_key_file_to_data(config, NULL, NULL);
    utils_write_file(filename, data);

    g_free(data);
  }

  g_key_file_free(config);
  g_free(filename);
  g_free(config_dir);
}

static void load_settings(void)
{
  gchar *filename;
  GKeyFile *config;

  config = g_key_file_new();
  filename = get_config_filename();
  g_key_file_load_from_file(config, filename, G_KEY_FILE_NONE, NULL);

  config_desktop_dir_files = utils_get_setting_boolean(config, "quickopen", "desktop_dir_files", FALSE);
  config_doc_dir_files = utils_get_setting_boolean(config, "quickopen", "doc_dir_files", FALSE);
  config_home_dir_files = utils_get_setting_boolean(config, "quickopen", "home_dir_files", FALSE);
  config_recent_files = utils_get_setting_boolean(config, "quickopen", "recent_files", TRUE);

  g_key_file_free(config);
  g_free(filename);
}

static void on_configure_response(G_GNUC_UNUSED GtkDialog *dialog, gint response, G_GNUC_UNUSED gpointer data)
{
  if (response == GTK_RESPONSE_OK || response == GTK_RESPONSE_APPLY) {
    config_desktop_dir_files = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(desktop_dir_files_checkbox));
    config_doc_dir_files = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(doc_dir_files_checkbox));
    config_home_dir_files = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(home_dir_files_checkbox));
    config_recent_files = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(recent_files_checkbox));

    save_settings();
  }
}

static gboolean quickopen_init(GeanyPlugin *plugin, G_GNUC_UNUSED gpointer data)
{
  GtkWidget *file_menu, *goto_file_menu_item;
  GeanyKeyGroup *kb_group;

  geany_plugin = plugin;
  geany_data = plugin->geany_data;

  load_settings();

  file_menu = ui_lookup_widget(geany_data->main_widgets->window, "file1_menu");

  goto_file_menu_item = gtk_menu_item_new_with_mnemonic(_("_Go to File..."));
  gtk_container_add(GTK_CONTAINER(file_menu), goto_file_menu_item);
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

static GtkWidget *quickopen_configure(G_GNUC_UNUSED GeanyPlugin *plugin, GtkDialog *dialog, G_GNUC_UNUSED gpointer data)
{
  GtkWidget *look_label, *vbox;

  look_label = gtk_label_new(_("Look for files in:"));
  gtk_widget_set_halign(look_label, GTK_ALIGN_START);

  desktop_dir_files_checkbox = gtk_check_button_new_with_label(_("Desktop directory"));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(desktop_dir_files_checkbox), config_desktop_dir_files);

  doc_dir_files_checkbox = gtk_check_button_new_with_label(_("Directory of the currently opened document"));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(doc_dir_files_checkbox), config_doc_dir_files);

  home_dir_files_checkbox = gtk_check_button_new_with_label(_("Home directory"));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(home_dir_files_checkbox), config_home_dir_files);

  recent_files_checkbox = gtk_check_button_new_with_label(_("Recently used files"));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(recent_files_checkbox), config_recent_files);


  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_pack_start(GTK_BOX(vbox), look_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), recent_files_checkbox, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), doc_dir_files_checkbox, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), desktop_dir_files_checkbox, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), home_dir_files_checkbox, FALSE, FALSE, 0);

  gtk_widget_show_all(vbox);

  g_signal_connect(dialog, "response", G_CALLBACK(on_configure_response), NULL);

  return vbox;
}

G_MODULE_EXPORT void geany_load_module(GeanyPlugin *plugin)
{
  main_locale_init(LOCALEDIR, GETTEXT_PACKAGE);

  plugin->info->name = _("Quick Open");
  plugin->info->description = _("Quickly open a file");
  plugin->info->version = "0.9.1";
  plugin->info->author = "Filip Szymański <fszymanski(dot)pl(at)gmail(dot)com>";

  plugin->funcs->init = quickopen_init;
  plugin->funcs->cleanup = quickopen_cleanup;
  plugin->funcs->configure = quickopen_configure;

  GEANY_PLUGIN_REGISTER(plugin, 225);
}

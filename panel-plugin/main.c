/*
 *  Copyright (c) 2008-2009 Mike Massonnet <mmassonnet@xfce.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#include <glib/gstdio.h>
#include <X11/Xlib.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <libxfce4util/libxfce4util.h>
#include <libxfcegui4/libxfcegui4.h>
#include <libxfce4panel/xfce-panel-plugin.h>
#include <libxfce4panel/xfce-panel-convenience.h>
#include <xfconf/xfconf.h>

#include "common.h"
#include "actions.h"
#include "collector.h"
#include "history.h"
#include "menu.h"



/*
 * MyPlugin structure
 */

typedef struct _MyPlugin MyPlugin;
struct _MyPlugin
{
  XfcePanelPlugin      *panel_plugin;
  GtkStatusIcon        *status_icon;
  XfconfChannel        *channel;
  ClipmanActions       *actions;
  ClipmanCollector     *collector;
  ClipmanHistory       *history;
  GtkWidget            *button;
  GtkWidget            *image;
  GtkWidget            *menu;
  GtkWidget            *popup_menu;
  gulong                popup_menu_id;
};

/*
 * Status Icon
 */

static MyPlugin*        status_icon_register            ();
static gboolean         cb_status_icon_is_embedded      (GtkStatusIcon *status_icon);
static void             cb_status_icon_activate         (MyPlugin *plugin);
static void             cb_status_icon_popup_menu       (MyPlugin *plugin,
                                                         guint button,
                                                         guint activate_time);
static void             cb_status_icon_quit             (MyPlugin *plugin);
static gboolean         cb_status_icon_set_size         (MyPlugin *plugin,
                                                         gint size);
static void             cb_status_icon_finalize         (MyPlugin *plugin);
static void             install_autostart_file          ();
static void             update_autostart_file           ();

/*
 * Panel Plugin
 */

static void             panel_plugin_register           (XfcePanelPlugin *panel_plugin);
static gboolean         panel_plugin_set_size           (MyPlugin *plugin,
                                                         gint size);

/*
 * Plugin Registration
 */

static gboolean         plugin_preinit                  (gint argc,
                                                         gchar *argv[]);
static gboolean         plugin_check                    (GdkScreen *screen);
XFCE_PANEL_PLUGIN_REGISTER_EXTERNAL_FULL (panel_plugin_register,
                                          plugin_preinit, plugin_check);

/*
 * Plugin Functions
 */

static MyPlugin*        plugin_register                 ();
static void             plugin_load                     (MyPlugin *plugin);
static void             plugin_save                     (MyPlugin *plugin);
static void             plugin_free                     (MyPlugin *plugin);
static void             plugin_about                    (MyPlugin *plugin);
static void             cb_about_dialog_url_hook        (GtkAboutDialog *dialog,
                                                         const gchar *uri,
                                                         gpointer user_data);
static void             plugin_configure                (MyPlugin *plugin);
static void             plugin_block_menu               (MyPlugin *plugin);
static void             plugin_unblock_menu             (MyPlugin *plugin);
static void             cb_button_toggled               (GtkToggleButton *button,
                                                         MyPlugin *plugin);
static void             cb_menu_deactivate              (GtkMenuShell *menu,
                                                         MyPlugin *plugin);
static void             my_plugin_position_menu         (GtkMenu *menu,
                                                         gint *x,
                                                         gint *y,
                                                         gboolean *push_in,
                                                         MyPlugin *plugin);

/*
 * X11 Selection on CLIPBOARD_MANAGER
 */

static gboolean         my_plugin_take_ownership        ();

/*
 * X11 Selection for the popup command
 */

static gboolean         my_plugin_set_selection         (MyPlugin *plugin);
static gboolean         cb_message_received             (MyPlugin *plugin,
                                                         GdkEventClient *ev);



/*
 * Plugin Registration
 */

static gboolean
plugin_preinit (gint argc,
                gchar *argv[])
{
  MyPlugin *plugin;

  /* Text Domain and Locale */
  xfce_textdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8");

  if (argc == 1)
    {
      /* Consider the plugin to be run by command line */
      gtk_init (&argc, &argv);

      if (!plugin_check (NULL))
        return FALSE;

      g_set_application_name (_("Clipman"));
      plugin = status_icon_register ();
      install_autostart_file ();

      gtk_main ();

      g_object_unref (plugin->status_icon);

      return FALSE;
    }

  return TRUE;
}

static gboolean
plugin_check (GdkScreen *screen)
{
  /* Take Ownership on CLIPBOARD_MANAGER */
  return my_plugin_take_ownership ();
}

static MyPlugin *
plugin_register ()
{
  /* MyPlugin */
  MyPlugin *plugin = g_slice_new0 (MyPlugin);
  plugin->panel_plugin = NULL;
  plugin->status_icon = NULL;

  /* Xfconf */
  xfconf_init (NULL);
  plugin->channel = xfconf_channel_new_with_property_base ("xfce4-panel", "/plugins/clipman");

  /* ClipmanActions */
  plugin->actions = clipman_actions_get ();

  /* ClipmanHistory */
  plugin->history = clipman_history_get ();
  xfconf_g_property_bind (plugin->channel, "/settings/max-texts-in-history",
                          G_TYPE_UINT, plugin->history, "max-texts-in-history");
  xfconf_g_property_bind (plugin->channel, "/settings/max-images-in-history",
                          G_TYPE_UINT, plugin->history, "max-images-in-history");
  xfconf_g_property_bind (plugin->channel, "/settings/save-on-quit",
                          G_TYPE_BOOLEAN, plugin->history, "save-on-quit");

  /* ClipmanCollector */
  plugin->collector = clipman_collector_get ();
  xfconf_g_property_bind (plugin->channel, "/settings/add-primary-clipboard",
                          G_TYPE_BOOLEAN, plugin->collector, "add-primary-clipboard");
  xfconf_g_property_bind (plugin->channel, "/settings/history-ignore-primary-clipboard",
                          G_TYPE_BOOLEAN, plugin->collector, "history-ignore-primary-clipboard");
  xfconf_g_property_bind (plugin->channel, "/settings/enable-actions",
                          G_TYPE_BOOLEAN, plugin->collector, "enable-actions");

  /* ClipmanMenu */
  plugin->menu = clipman_menu_new ();

  /* Load the data */
  plugin_load (plugin);

  /* Connect signal to save content */
  g_signal_connect_swapped (plugin->history, "item-added",
                            G_CALLBACK (plugin_save), plugin);

  /* Set the selection for the popup command */
  my_plugin_set_selection (plugin);

  return plugin;
}

/*
 * Status Icon
 */

static MyPlugin *
status_icon_register ()
{
  MyPlugin *plugin = plugin_register ();

  /* Status Icon */
  plugin->status_icon = gtk_status_icon_new ();
  gtk_status_icon_set_tooltip (plugin->status_icon, _("Clipman"));
  g_timeout_add_seconds (60, (GSourceFunc)cb_status_icon_is_embedded, plugin->status_icon);

  /* Signals */
  g_signal_connect_swapped (plugin->status_icon, "activate",
                            G_CALLBACK (cb_status_icon_activate), plugin);
  plugin->popup_menu_id =
    g_signal_connect_swapped (plugin->status_icon, "popup-menu",
                              G_CALLBACK (cb_status_icon_popup_menu), plugin);
  g_signal_connect_swapped (plugin->status_icon, "size-changed",
                            G_CALLBACK (cb_status_icon_set_size), plugin);
  g_object_weak_ref (G_OBJECT (plugin->status_icon), (GWeakNotify)cb_status_icon_finalize, plugin);

  return plugin;
}

static gboolean
cb_status_icon_is_embedded (GtkStatusIcon *status_icon)
{
  if (!gtk_status_icon_is_embedded (status_icon))
    {
      g_warning ("Status Icon is not embedded");
      gtk_main_quit ();
    }
  return FALSE;
}

static void
cb_status_icon_activate (MyPlugin *plugin)
{
  gtk_menu_set_screen (GTK_MENU (plugin->menu), gtk_status_icon_get_screen (plugin->status_icon));
  gtk_menu_popup (GTK_MENU (plugin->menu), NULL, NULL,
                  (GtkMenuPositionFunc)gtk_status_icon_position_menu, plugin->status_icon,
                  0, gtk_get_current_event_time ());
}

static void
cb_status_icon_popup_menu (MyPlugin *plugin, guint button, guint activate_time)
{
  GtkWidget *mi;

  if (G_UNLIKELY (plugin->popup_menu == NULL))
    {
      plugin->popup_menu = gtk_menu_new ();
      mi = gtk_menu_item_new_with_label (_("Clipman"));
      gtk_widget_set_sensitive (mi, FALSE);
      gtk_menu_shell_append (GTK_MENU_SHELL (plugin->popup_menu), mi);
      mi = gtk_separator_menu_item_new ();
      gtk_menu_shell_append (GTK_MENU_SHELL (plugin->popup_menu), mi);
      mi = gtk_image_menu_item_new_from_stock (GTK_STOCK_PROPERTIES, NULL);
      gtk_menu_shell_append (GTK_MENU_SHELL (plugin->popup_menu), mi);
      g_signal_connect_swapped (mi, "activate", G_CALLBACK (plugin_configure), plugin);
      mi = gtk_image_menu_item_new_from_stock (GTK_STOCK_ABOUT, NULL);
      gtk_menu_shell_append (GTK_MENU_SHELL (plugin->popup_menu), mi);
      g_signal_connect_swapped (mi, "activate", G_CALLBACK (plugin_about), plugin);
      mi = gtk_separator_menu_item_new ();
      gtk_menu_shell_append (GTK_MENU_SHELL (plugin->popup_menu), mi);
      mi = gtk_image_menu_item_new_from_stock (GTK_STOCK_REMOVE, NULL);
      g_signal_connect_swapped (mi, "activate", G_CALLBACK (cb_status_icon_quit), plugin);
      gtk_menu_shell_append (GTK_MENU_SHELL (plugin->popup_menu), mi);
      gtk_widget_show_all (plugin->popup_menu);
    }

  gtk_menu_set_screen (GTK_MENU (plugin->popup_menu), gtk_status_icon_get_screen (plugin->status_icon));
  gtk_menu_popup (GTK_MENU (plugin->popup_menu), NULL, NULL,
                  (GtkMenuPositionFunc)NULL, NULL,
                  0, gtk_get_current_event_time ());
}

static void
cb_status_icon_quit (MyPlugin *plugin)
{
  gint res;
  res = xfce_message_dialog (NULL, _("Autostart Clipman"), GTK_STOCK_DIALOG_QUESTION,
                             _("Autostart Clipman"), _("Do you want to restart the clipboard manager the next time you login?"),
                             GTK_STOCK_YES, GTK_RESPONSE_YES, GTK_STOCK_NO, GTK_RESPONSE_NO, NULL);
  update_autostart_file (res == GTK_RESPONSE_YES);

  gtk_status_icon_set_visible (plugin->status_icon, FALSE);
  gtk_main_quit ();
}

static gboolean
cb_status_icon_set_size (MyPlugin *plugin, gint size)
{
  GdkPixbuf *pixbuf;
 
  pixbuf = xfce_themed_icon_load (GTK_STOCK_PASTE, size);
  gtk_status_icon_set_from_pixbuf (plugin->status_icon, pixbuf);
  g_object_unref (G_OBJECT (pixbuf));

  return TRUE;
}

static void
cb_status_icon_finalize (MyPlugin *plugin)
{
  plugin_save (plugin);
  plugin_free (plugin);
}

static void
install_autostart_file ()
{
  gchar *sysfile;
  gchar *userfile;
  gint res;
  GKeyFile *keyfile;
  gchar *data;

  sysfile = g_strdup (SYSCONFDIR"/xdg/autostart/"PACKAGE"-autostart.desktop");
  userfile = g_strdup_printf ("%s/autostart/"PACKAGE"-autostart.desktop", g_get_user_config_dir ());

  if (!g_file_test (sysfile, G_FILE_TEST_EXISTS))
    {
      g_warning ("The autostart file (%s) is missing in the system installation", sysfile);
      goto out;
    }

  /* Check if the user autostart file exists */
  if (g_file_test (userfile, G_FILE_TEST_EXISTS))
    {
      goto out;
    }

  /* Ask the user */
  res = xfce_message_dialog (NULL, _("Autostart Clipman"), GTK_STOCK_DIALOG_QUESTION,
                             _("Autostart Clipman"), _("Do you want to autostart the clipboard manager?"),
                             GTK_STOCK_YES, GTK_RESPONSE_YES, GTK_STOCK_NO, GTK_RESPONSE_NO, NULL);

  /* Copy the file */
  keyfile = g_key_file_new ();
  g_key_file_load_from_file (keyfile, sysfile, G_KEY_FILE_KEEP_TRANSLATIONS, NULL);
  if (res == GTK_RESPONSE_YES)
    {
      g_key_file_set_boolean (keyfile, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_HIDDEN, FALSE);
    }
  data = g_key_file_to_data (keyfile, NULL, NULL);
  g_file_set_contents (userfile, data, -1, NULL);
  g_free (data);
  g_key_file_free (keyfile);

out:
  g_free (sysfile);
  g_free (userfile);
}

static void
update_autostart_file (gboolean autostart)
{
  gchar *userfile;
  gint res;
  GKeyFile *keyfile;
  gchar *data;

  userfile = g_strdup_printf ("%s/autostart/"PACKAGE"-autostart.desktop", g_get_user_config_dir ());

  /* Check if the user autostart file exists */
  if (!g_file_test (userfile, G_FILE_TEST_EXISTS))
    {
      g_warning ("The autostart file (%s) is not installed, what did you do?", userfile);
      g_free (userfile);
      return;
    }

  /* Update the file */
  keyfile = g_key_file_new ();
  g_key_file_load_from_file (keyfile, userfile, G_KEY_FILE_KEEP_TRANSLATIONS, NULL);
  g_key_file_set_boolean (keyfile, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_HIDDEN, !autostart);
  data = g_key_file_to_data (keyfile, NULL, NULL);
  g_file_set_contents (userfile, data, -1, NULL);
  g_free (data);
  g_key_file_free (keyfile);
  g_free (userfile);
}

/*
 * Panel Plugin
 */

static void
panel_plugin_register (XfcePanelPlugin *panel_plugin)
{
  MyPlugin *plugin = plugin_register ();

  /* Panel Plugin */
  plugin->panel_plugin = panel_plugin;
#if GTK_CHECK_VERSION (2,12,0)
  gtk_widget_set_tooltip_text (GTK_WIDGET (panel_plugin), _("Clipman"));
#endif

  /* Panel Button */
  plugin->button = xfce_create_panel_toggle_button ();
  /* The image is set through the set_size callback */
  plugin->image = gtk_image_new ();
  gtk_container_add (GTK_CONTAINER (plugin->button), plugin->image);
  gtk_container_add (GTK_CONTAINER (panel_plugin), plugin->button);
  xfce_panel_plugin_add_action_widget (panel_plugin, plugin->button);
  g_signal_connect (plugin->button, "toggled",
                    G_CALLBACK (cb_button_toggled), plugin);

  /* Signals */
  g_signal_connect_swapped (panel_plugin, "size-changed",
                            G_CALLBACK (panel_plugin_set_size), plugin);
  xfce_panel_plugin_menu_show_about (panel_plugin);
  g_signal_connect_swapped (panel_plugin, "about",
                            G_CALLBACK (plugin_about), plugin);
  xfce_panel_plugin_menu_show_configure (panel_plugin);
  g_signal_connect_swapped (panel_plugin, "configure-plugin",
                            G_CALLBACK (plugin_configure), plugin);
  g_signal_connect_swapped (panel_plugin, "save",
                            G_CALLBACK (plugin_save), plugin);
  g_signal_connect_swapped (panel_plugin, "free-data",
                            G_CALLBACK (plugin_free), plugin);
  g_signal_connect (plugin->menu, "deactivate",
                    G_CALLBACK (cb_menu_deactivate), plugin);

  gtk_widget_show_all (GTK_WIDGET (panel_plugin));
}

static gboolean
panel_plugin_set_size (MyPlugin *plugin,
                       gint size)
{
  GdkPixbuf *pixbuf;

  gtk_widget_set_size_request (plugin->button, size, size);

  size -= 2 + 2 * MAX (plugin->button->style->xthickness,
                       plugin->button->style->ythickness);
  pixbuf = xfce_themed_icon_load (GTK_STOCK_PASTE, size);
  gtk_image_set_from_pixbuf (GTK_IMAGE (plugin->image), pixbuf);
  g_object_unref (G_OBJECT (pixbuf));

  return TRUE;
}

/*
 * Plugin Functions
 */

static void
plugin_load (MyPlugin *plugin)
{
  GtkClipboard *clipboard;
  GKeyFile *keyfile;
  gchar **texts = NULL;
  gchar *filename;
  GdkPixbuf *image;
  gint i = 0;
  gboolean save_on_quit;

  /* Return if the history must not be saved */
  g_object_get (plugin->history, "save-on-quit", &save_on_quit, NULL);
  if (save_on_quit == FALSE)
    return;

  clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

  /* Load images */
  while (TRUE)
    {
      filename = g_strdup_printf ("%s/xfce4/clipman/image%d.png", g_get_user_cache_dir (), i++);
      image = gdk_pixbuf_new_from_file (filename, NULL);
      g_unlink (filename);
      g_free (filename);
      if (image == NULL)
        break;

      DBG ("Loading image from cache file %s", filename);
      clipman_history_add_image (plugin->history, image);
      g_object_unref (image);
    }

  /* Load texts */
  filename = g_strdup_printf ("%s/xfce4/clipman/textsrc", g_get_user_cache_dir ());
  DBG ("Loading texts from cache file %s", filename);
  keyfile = g_key_file_new ();
  if (g_key_file_load_from_file (keyfile, filename, G_KEY_FILE_NONE, NULL))
    {
      texts = g_key_file_get_string_list (keyfile, "texts", "texts", NULL, NULL);
      for (i = 0; texts != NULL && texts[i] != NULL; i++)
        clipman_history_add_text (plugin->history, texts[i]);
      g_unlink (filename);
    }

  g_key_file_free (keyfile);
  g_strfreev (texts);
  g_free (filename);

  /* Set no current item */
  clipman_history_set_item_to_restore (plugin->history, NULL);
}

static void
plugin_save (MyPlugin *plugin)
{
  GSList *list, *l;
  const ClipmanHistoryItem *item;
  GKeyFile *keyfile;
  const gchar **texts;
  gchar *data;
  gchar *filename;
  gint n_texts, n_images;
  gboolean save_on_quit;

  /* Return if the history must not be saved */
  g_object_get (plugin->history, "save-on-quit", &save_on_quit, NULL);
  if (save_on_quit == FALSE)
    return;

  /* Create initial directory */
  filename = xfce_resource_save_location (XFCE_RESOURCE_CACHE, "xfce4/clipman/", TRUE);
  g_free (filename);

  /* Save the history */
  list = clipman_history_get_list (plugin->history);
  list = g_slist_reverse (list);
  if (list != NULL)
    {
      texts = g_malloc0 (g_slist_length (list) * sizeof (gchar *));
      for (n_texts = n_images = 0, l = list; l != NULL; l = l->next)
        {
          item = l->data;

          switch (item->type)
            {
            case CLIPMAN_HISTORY_TYPE_TEXT:
              texts[n_texts++] = item->content.text;
              break;

            case CLIPMAN_HISTORY_TYPE_IMAGE:
              filename = g_strdup_printf ("%s/xfce4/clipman/image%d.png", g_get_user_cache_dir (), n_images++);
              if (!gdk_pixbuf_save (item->content.image, filename, "png", NULL, NULL))
                g_warning ("Failed to save image to cache file %s", filename);
              else
                DBG ("Saved image to cache file %s", filename);
              g_free (filename);
              break;

            default:
              g_assert_not_reached ();
            }
        }

      if (n_texts > 0)
        {
          filename = g_strdup_printf ("%s/xfce4/clipman/textsrc", g_get_user_cache_dir ());
          keyfile = g_key_file_new ();
          g_key_file_set_string_list (keyfile, "texts", "texts", texts, n_texts);
          data = g_key_file_to_data (keyfile, NULL, NULL);
          g_file_set_contents (filename, data, -1, NULL);
          DBG ("Saved texts to cache file %s", filename);

          g_key_file_free (keyfile);
          g_free (data);
          g_free (filename);
        }

      g_free (texts);
      g_slist_free (list);
    }
}

static void
plugin_free (MyPlugin *plugin)
{
  gtk_widget_destroy (plugin->menu);
  g_object_unref (plugin->channel);
  g_object_unref (plugin->actions);
  g_object_unref (plugin->collector);
  g_object_unref (plugin->history);

  if (plugin->panel_plugin != NULL)
    {
      gtk_widget_destroy (plugin->button);
    }
  else if (plugin->status_icon != NULL)
    {
      gtk_widget_destroy (plugin->popup_menu);
    }

  g_slice_free (MyPlugin, plugin);
  xfconf_shutdown ();
}

static void
plugin_about (MyPlugin *plugin)
{
  const gchar *artists[] = { "Mike Massonnet", NULL, };
  const gchar *authors[] = { "(c) 2008-2009 Mike Massonnet",
                             "(c) 2005-2006 Nick Schermer",
                             "(c) 2003 Eduard Roccatello",
                             NULL, };
  const gchar *documenters[] = { "Mike Massonnet", NULL, };
  const gchar *license =
    "This program is free software; you can redistribute it and/or modify\n"
    "it under the terms of the GNU General Public License as published by\n"
    "the Free Software Foundation; either version 2 of the License, or\n"
    "(at your option) any later version.\n";

  gtk_about_dialog_set_url_hook (cb_about_dialog_url_hook, NULL, NULL);
  gtk_show_about_dialog (NULL,
                         "program-name", _("Clipman"),
                         "logo-icon-name", "xfce4-clipman-plugin",
                         "comments", _("Clipboard Manager for Xfce"),
                         "version", PACKAGE_VERSION,
                         "copyright", "Copyright © 2003-2009 The Xfce development team",
                         "license", license,
                         "website", "http://goodies.xfce.org/projects/panel-plugins/xfce4-clipman-plugin",
                         "website-label", "goodies.xfce.org",
                         "artists", artists,
                         "authors", authors,
                         "documenters", documenters,
                         "translator-credits", _("translator-credits"),
                         NULL);
}

static void
cb_about_dialog_url_hook (GtkAboutDialog *dialog,
                          const gchar *uri,
                          gpointer user_data)
{
  gchar *command;

  command = g_strdup_printf ("exo-open %s", uri);
  if (!g_spawn_command_line_async (command, NULL))
    {
      g_free (command);
      command = g_strdup_printf ("firefox %s", uri);
      g_spawn_command_line_async (command, NULL);
    }
  g_free (command);
}

static void
plugin_configure (MyPlugin *plugin)
{
  GError *error = NULL;
  GtkWidget *error_dialog;

  gdk_spawn_command_line_on_screen (gdk_screen_get_default (), "xfce4-clipman-settings", &error);
  if (error != NULL)
  {
    error_dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                           _("Unable to open the settings dialog"), NULL);
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (error_dialog), "%s", error->message, NULL);
    gtk_dialog_run (GTK_DIALOG (error_dialog));
    gtk_widget_destroy (error_dialog);
    g_error_free (error);
  }
}

static void
plugin_block_menu (MyPlugin *plugin)
{
  if (plugin->panel_plugin != NULL)
    {
      xfce_panel_plugin_block_menu (plugin->panel_plugin);
    }
  else
    {
      g_signal_handler_block (plugin->status_icon, plugin->popup_menu_id);
    }
}

static void
plugin_unblock_menu (MyPlugin *plugin)
{
  if (plugin->panel_plugin != NULL)
    {
      xfce_panel_plugin_unblock_menu (plugin->panel_plugin);
    }
  else
    {
      g_signal_handler_unblock (plugin->status_icon, plugin->popup_menu_id);
    }
}

static void
cb_button_toggled (GtkToggleButton *button,
                   MyPlugin *plugin)
{
  if (gtk_toggle_button_get_active (button))
    {
      gtk_menu_set_screen (GTK_MENU (plugin->menu), gtk_widget_get_screen (plugin->button));
      xfce_panel_plugin_register_menu (plugin->panel_plugin, GTK_MENU (plugin->menu));
      gtk_menu_popup (GTK_MENU (plugin->menu), NULL, NULL,
                      (GtkMenuPositionFunc)my_plugin_position_menu, plugin,
                      0, gtk_get_current_event_time ());
    }
}

static void
cb_menu_deactivate (GtkMenuShell *menu,
                    MyPlugin *plugin)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->button), FALSE);
}

static void
my_plugin_position_menu (GtkMenu *menu,
                         gint *x,
                         gint *y,
                         gboolean *push_in,
                         MyPlugin *plugin)
{
  GtkWidget *button;
  GtkRequisition requisition;
  GtkOrientation orientation;

  button = plugin->button;
  orientation = xfce_panel_plugin_get_orientation (plugin->panel_plugin);
  gtk_widget_size_request (GTK_WIDGET (menu), &requisition);
  gdk_window_get_origin (button->window, x, y);

  switch (orientation)
    {
    case GTK_ORIENTATION_HORIZONTAL:
      if (*y + button->allocation.height + requisition.height > gdk_screen_height ())
        /* Show menu above */
        *y -= requisition.height;
      else
        /* Show menu below */
        *y += button->allocation.height;

      if (*x + requisition.width > gdk_screen_width ())
        /* Adjust horizontal position */
        *x = gdk_screen_width () - requisition.width;
      break;

    case GTK_ORIENTATION_VERTICAL:
      if (*x + button->allocation.width + requisition.width > gdk_screen_width ())
        /* Show menu on the right */
        *x -= requisition.width;
      else
        /* Show menu on the left */
        *x += button->allocation.width;

      if (*y + requisition.height > gdk_screen_height ())
        /* Adjust vertical position */
        *y = gdk_screen_height () - requisition.height;
      break;

    default:
      break;
    }
}

/*
 * X11 Selection on CLIPBOARD_MANAGER
 */

static gboolean
my_plugin_take_ownership ()
{
  Display      *display;
  Window        root;
  Atom          atom;
  GtkWidget    *dialog;

  display = GDK_DISPLAY ();
  root = XDefaultRootWindow (display);

  atom = XInternAtom (display, "CLIPBOARD_MANAGER", FALSE);
  if (XGetSelectionOwner (display, atom))
    {
      dialog = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                       _("There is already a clipboard manager running"));
      gtk_window_set_keep_above (GTK_WINDOW (dialog), TRUE);
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      return FALSE;
    }
  XSetSelectionOwner (display, atom, root, GDK_CURRENT_TIME);

  return TRUE;
}

/*
 * X11 Selection for the popup command
 */

static gboolean
my_plugin_set_selection (MyPlugin *plugin)
{
  GdkScreen          *gscreen;
  gchar              *selection_name;
  Atom                selection_atom;
  GtkWidget          *win;
  Window              id;

  win = gtk_invisible_new ();
  gtk_widget_realize (win);
  id = GDK_WINDOW_XID (win->window);

  gscreen = gtk_widget_get_screen (win);
  selection_name = g_strdup_printf (XFCE_CLIPMAN_SELECTION"%d",
                                    gdk_screen_get_number (gscreen));
  selection_atom = XInternAtom (GDK_DISPLAY(), selection_name, FALSE);

  if (XGetSelectionOwner (GDK_DISPLAY(), selection_atom))
    {
      gtk_widget_destroy (win);
      return FALSE;
    }

  XSelectInput (GDK_DISPLAY(), id, PropertyChangeMask);
  XSetSelectionOwner (GDK_DISPLAY(), selection_atom, id, GDK_CURRENT_TIME);

  g_signal_connect_swapped (win, "client-event",
                            G_CALLBACK (cb_message_received), plugin);

  return TRUE;
}

static gboolean
cb_message_received (MyPlugin *plugin,
                     GdkEventClient *ev)
{
  if (G_LIKELY (ev->data_format == 8 && *(ev->data.b) != '\0'))
    {
      if (!g_ascii_strcasecmp (XFCE_CLIPMAN_MESSAGE, ev->data.b))
        {
          DBG ("Message received: %s", ev->data.b);

          if (plugin->panel_plugin != NULL)
            xfce_panel_plugin_set_panel_hidden (plugin->panel_plugin, FALSE);

          while (gtk_events_pending ())
            gtk_main_iteration ();

          if (plugin->panel_plugin != NULL)
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->button), TRUE);
          else if (plugin->status_icon != NULL)
            g_signal_emit_by_name (plugin->status_icon, "activate", NULL);
          return TRUE;
        }
    }
  return FALSE;
}

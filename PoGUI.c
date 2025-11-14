#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>

#ifdef G_OS_WIN32
#define POTRACE_EXE_NAME "potrace.exe"
#else
#define POTRACE_EXE_NAME "potrace"
#endif

typedef struct {
    GtkWidget *window;
    GtkWidget *status_label;
} AppWidgets;

static gchar *potrace_path = NULL;

static void show_message(GtkWindow *parent,
                         GtkMessageType type,
                         const gchar *title,
                         const gchar *message)
{
    GtkWidget *dialog = gtk_message_dialog_new(
        parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        type,
        GTK_BUTTONS_OK,
        "%s",
        message
    );
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void on_convert_clicked(GtkButton *button, gpointer user_data)
{
    AppWidgets *widgets = (AppWidgets *)user_data;
    GtkWidget *dialog;
    gchar *input_filename = NULL;
    gchar *output_filename = NULL;
    gchar *tmp_input_filename = NULL;

    if (potrace_path == NULL) {
        show_message(GTK_WINDOW(widgets->window),
                     GTK_MESSAGE_ERROR,
                     "Potrace not configured",
                     "Internal error: Potrace path is not set.");
        return;
    }

    dialog = gtk_file_chooser_dialog_new(
        "Select image",
        GTK_WINDOW(widgets->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL
    );

    GtkFileFilter *filter_img = gtk_file_filter_new();
    gtk_file_filter_set_name(filter_img, "Image files");
    gtk_file_filter_add_pattern(filter_img, "*.png");
    gtk_file_filter_add_pattern(filter_img, "*.jpg");
    gtk_file_filter_add_pattern(filter_img, "*.jpeg");
    gtk_file_filter_add_pattern(filter_img, "*.bmp");
    gtk_file_filter_add_pattern(filter_img, "*.gif");
    gtk_file_filter_add_pattern(filter_img, "*.tif");
    gtk_file_filter_add_pattern(filter_img, "*.tiff");
    gtk_file_filter_add_pattern(filter_img, "*.ppm");
    gtk_file_filter_add_pattern(filter_img, "*.pgm");
    gtk_file_filter_add_pattern(filter_img, "*.pbm");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_img);

    GtkFileFilter *filter_all = gtk_file_filter_new();
    gtk_file_filter_set_name(filter_all, "All files");
    gtk_file_filter_add_pattern(filter_all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_all);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        input_filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    }
    gtk_widget_destroy(dialog);

    if (!input_filename) {
        return;
    }

    dialog = gtk_file_chooser_dialog_new(
        "Save SVG as",
        GTK_WINDOW(widgets->window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT,
        NULL
    );
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

    gchar *base = g_path_get_basename(input_filename);
    gchar *dot = strrchr(base, '.');
    if (dot != NULL) {
        *dot = '\0';
    }
    gchar *suggested = g_strconcat(base, ".svg", NULL);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), suggested);
    g_free(suggested);
    g_free(base);

    GtkFileFilter *svg_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(svg_filter, "SVG files");
    gtk_file_filter_add_pattern(svg_filter, "*.svg");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), svg_filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        output_filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    }
    gtk_widget_destroy(dialog);

    if (!output_filename) {
        g_free(input_filename);
        return;
    }

    GError *tmp_err = NULL;
    gint tmp_fd = g_file_open_tmp("potrace_input_XXXXXX.pgm", &tmp_input_filename, &tmp_err);
    if (tmp_fd == -1) {
        gchar *msg = g_strdup_printf(
            "Failed to create temporary file:\n%s",
            tmp_err ? tmp_err->message : "Unknown error"
        );
        show_message(GTK_WINDOW(widgets->window), GTK_MESSAGE_ERROR, "Error", msg);
        g_free(msg);
        if (tmp_err) g_error_free(tmp_err);
        g_free(input_filename);
        g_free(output_filename);
        return;
    }
    g_close(tmp_fd, NULL);

    gtk_label_set_text(GTK_LABEL(widgets->status_label),
                       "Converting image with ImageMagick...");
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    GError *error = NULL;
    gint status = 0;
    gboolean ok;
    gchar *convert_argv[] = {
        "convert",
        input_filename,
        tmp_input_filename,
        NULL
    };

    error = NULL;
    ok = g_spawn_sync(
        NULL,                /* working directory (inherit) */
        convert_argv,        /* argv */
        NULL,                /* envp */
        G_SPAWN_SEARCH_PATH, /* search PATH for "convert" */
        NULL,                /* child setup */
        NULL,                /* user data for child setup */
        NULL,                /* standard output (ignored) */
        NULL,                /* standard error idk man (ignored) */
        &status,
        &error
    );

    if (!ok) {
        gchar *msg = g_strdup_printf(
            "Failed to run ImageMagick 'convert':\n%s",
            error ? error->message : "Unknown error"
        );
        show_message(GTK_WINDOW(widgets->window), GTK_MESSAGE_ERROR, "Error", msg);
        g_free(msg);
        if (error) g_error_free(error);
        gtk_label_set_text(GTK_LABEL(widgets->status_label), "Conversion failed.");
        g_unlink(tmp_input_filename);
        g_free(tmp_input_filename);
        g_free(input_filename);
        g_free(output_filename);
        return;
    }

    if (!g_spawn_check_exit_status(status, &error)) {
        gchar *msg = g_strdup_printf(
            "ImageMagick 'convert' exited with an error:\n%s",
            error ? error->message : "Unknown error"
        );
        show_message(GTK_WINDOW(widgets->window), GTK_MESSAGE_ERROR, "Convert Error", msg);
        g_free(msg);
        if (error) g_error_free(error);
        gtk_label_set_text(GTK_LABEL(widgets->status_label), "Conversion failed.");
        g_unlink(tmp_input_filename);
        g_free(tmp_input_filename);
        g_free(input_filename);
        g_free(output_filename);
        return;
    }

    gtk_label_set_text(GTK_LABEL(widgets->status_label),
                       "Tracing bitmap with Potrace...");
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    gchar *potrace_argv[] = {
        potrace_path,
        tmp_input_filename,
        "-s",
        "-o",
        output_filename,
        NULL
    };

    error = NULL;
    status = 0;
    ok = g_spawn_sync(
        NULL,              /* working directory (inherit) */
        potrace_argv,      /* argv */
        NULL,              /* envp */
        G_SPAWN_DEFAULT,   /* do NOT search PATH for Potrace */
        NULL,              /* child setup */
        NULL,              /* user data for child setup */
        NULL,              /* standard output (ignored) */
        NULL,              /* standard error I still don't know (ignored) */
        &status,
        &error
    );

    if (!ok) {
        gchar *msg = g_strdup_printf(
            "Failed to run Potrace at:\n%s\n\nError: %s",
            potrace_path,
            error ? error->message : "Unknown error"
        );
        show_message(GTK_WINDOW(widgets->window), GTK_MESSAGE_ERROR, "Error", msg);
        g_free(msg);
        if (error) g_error_free(error);
        gtk_label_set_text(GTK_LABEL(widgets->status_label), "Conversion failed.");
    } else if (!g_spawn_check_exit_status(status, &error)) {
        gchar *msg = g_strdup_printf(
            "Potrace exited with an error:\n%s",
            error ? error->message : "Unknown error"
        );
        show_message(GTK_WINDOW(widgets->window), GTK_MESSAGE_ERROR, "Potrace Error", msg);
        g_free(msg);
        if (error) g_error_free(error);
        gtk_label_set_text(GTK_LABEL(widgets->status_label), "Conversion failed.");
    } else {
        gchar *msg = g_strdup_printf("SVG successfully saved to:\n%s", output_filename);
        show_message(GTK_WINDOW(widgets->window), GTK_MESSAGE_INFO, "Success", msg);
        g_free(msg);
        gtk_label_set_text(GTK_LABEL(widgets->status_label), "Conversion completed.");
    }

    g_unlink(tmp_input_filename);
    g_free(tmp_input_filename);
    g_free(input_filename);
    g_free(output_filename);
}

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    AppWidgets widgets;

    if (argv[0] != NULL) {
        gchar *exe_dir = g_path_get_dirname(argv[0]);
        potrace_path = g_build_filename(exe_dir, POTRACE_EXE_NAME, NULL);
        g_free(exe_dir);
    } else {
        potrace_path = g_strdup(POTRACE_EXE_NAME);
    }

    widgets.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(widgets.window), "Potrace SVG Converter");
    gtk_container_set_border_width(GTK_CONTAINER(widgets.window), 12);
    gtk_window_set_resizable(GTK_WINDOW(widgets.window), FALSE);

    g_signal_connect(widgets.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(GTK_CONTAINER(widgets.window), vbox);

    GtkWidget *label = gtk_label_new(
        "Select an image (PNG, JPEG, BMP, etc.) and convert it\n"
        "to SVG using ImageMagick + Potrace.\n\n"
        "Requirements:\n"
        "  - Potrace in the same folder as this program.\n"
        "  - ImageMagick 'convert' in PATH."
    );
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    GtkWidget *button = gtk_button_new_with_label("Convert image to SVG...");
    gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);

    widgets.status_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(widgets.status_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(vbox), widgets.status_label, FALSE, FALSE, 0);

    g_signal_connect(button, "clicked", G_CALLBACK(on_convert_clicked), &widgets);

    gtk_widget_show_all(widgets.window);
    gtk_main();
    if (potrace_path != NULL) {
        g_free(potrace_path);
        potrace_path = NULL;
    }

    return 0;
}
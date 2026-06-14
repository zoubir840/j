#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/widget.h>

typedef struct {
    ViewDispatcher* view_dispatcher;
    Widget* widget;
} AppContext;

static void draw_callback(Canvas* canvas, void* ctx) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "ULTIMATE SCANNER");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 30, "Systeme pret...");
    canvas_draw_str(canvas, 0, 50, "En attente de signal...");
}

int32_t subghz_pro_app(void* p) {
    UNUSED(p);
    AppContext* ctx = malloc(sizeof(AppContext));
    ctx->view_dispatcher = view_dispatcher_alloc();
    ctx->widget = widget_alloc();
    
    widget_set_draw_callback(ctx->widget, draw_callback, NULL);
    view_dispatcher_add_view(ctx->view_dispatcher, 0, widget_get_view(ctx->widget));
    view_dispatcher_attach_to_gui(ctx->view_dispatcher, furi_record_open(RECORD_GUI), ViewDispatcherTypeFullscreen);
    
    view_dispatcher_run(ctx->view_dispatcher);
    
    widget_free(ctx->widget);
    view_dispatcher_free(ctx->view_dispatcher);
    free(ctx);
    return 0;
}

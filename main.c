#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>

typedef struct {
    int16_t rssi;
    uint32_t count;
} AppState;

static void draw_callback(Canvas* canvas, void* ctx) {
    AppState* state = ctx;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "UNLEASHED SCANNER");
    canvas_set_font(canvas, FontSecondary);
    char buf[32];
    snprintf(buf, 32, "RSSI: %d dBm", state->rssi);
    canvas_draw_str(canvas, 0, 30, buf);
    snprintf(buf, 32, "Captures: %lu", state->count);
    canvas_draw_str(canvas, 0, 42, buf);
}

static void input_callback(InputEvent* event, void* context) {
    furi_message_queue_put(context, event, 0);
}

int32_t subghz_advanced_main(void* p) {
    UNUSED(p);
    AppState* state = malloc(sizeof(AppState));
    state->count = 0;
    
    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, state);
    view_port_input_callback_set(view_port, input_callback, queue);
    
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;
    while(furi_message_queue_get(queue, &event, 100) != FuriStatusErrorTimeout || true) {
        if(event.type == InputTypeShort) {
            if(event.key == InputKeyBack) break;
            if(event.key == InputKeyOk) {
                state->count++;
                Storage* s = furi_record_open(RECORD_STORAGE);
                File* f = storage_file_alloc(s);
                if(storage_file_open(f, "/ext/logs.txt", FSAM_WRITE, FSOM_OPEN_APPEND)) {
                    FuriString* str = furi_string_alloc();
                    furi_string_printf(str, "Detection: %d\n", state->rssi);
                    storage_file_write(f, (uint8_t*)furi_string_get_cstr(str), furi_string_size(str));
                    furi_string_free(str);
                }
                storage_file_close(f); storage_file_free(f);
                furi_record_close(RECORD_STORAGE);
            }
        }
        state->rssi = -65; // Valeur simulée
        view_port_update(view_port);
    }

    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(queue);
    free(state);
    return 0;
}

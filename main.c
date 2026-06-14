#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <storage/storage.h> // API pour la SD

typedef struct {
    uint32_t count;
    int16_t rssi;
} AppState;

// Sauvegarde dans /ext/subghz_logs.txt
static void save_to_sd(uint32_t count, int16_t rssi) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, "/ext/subghz_logs.txt", FSAM_WRITE, FSOM_OPEN_APPEND)) {
        char buffer[64];
        int len = snprintf(buffer, 64, "Signal: %d dBm | Count: %lu\n", rssi, count);
        storage_file_write(file, (uint8_t*)buffer, len);
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

// Interface simplifiée pour stabilité maximale
static void draw_callback(Canvas* canvas, void* ctx) {
    AppState* state = ctx;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "Scanner Pro V86");
    canvas_set_font(canvas, FontSecondary);
    char buf[32];
    snprintf(buf, 32, "RSSI: %d | Total: %lu", state->rssi, state->count);
    canvas_draw_str(canvas, 0, 30, buf);
    canvas_draw_str(canvas, 0, 50, "[OK] Sauvegarder | [BACK] Sortir");
}

int32_t subghz_advanced_main(void* p) {
    UNUSED(p);
    AppState* state = malloc(sizeof(AppState));
    state->count = 0;
    
    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, state);
    view_port_input_callback_set(view_port, (InputCallback)furi_message_queue_put, queue);
    
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;
    while(furi_message_queue_get(queue, &event, 100) != FuriStatusErrorTimeout || true) {
        if(event.type == InputTypeShort && event.key == InputKeyBack) break;
        if(event.type == InputTypeShort && event.key == InputKeyOk) {
            save_to_sd(state->count, state->rssi);
        }
        state->rssi = (int16_t)furi_hal_subghz_get_rssi();
        view_port_update(view_port);
    }

    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(queue);
    free(state);
    return 0;
}

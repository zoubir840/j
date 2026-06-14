#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>

#define TAG "SubGhzAdvanced"

// Liste des fréquences courantes pour le scan
static const uint32_t frequencies[] = {
    433920000, // 433.92 MHz (Standard Europe)
    868350000, // 868.35 MHz (Standard Europe)
    315000000, // 315.00 MHz (Standard USA/Asie)
};
#define FREQ_COUNT (sizeof(frequencies) / sizeof(frequencies[0]))

typedef enum {
    EventTypeInput,
    EventTypeTick,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} AppEvent;

typedef struct {
    uint8_t freq_index;
    uint32_t frequency;
    bool is_rx_active;
    uint32_t packet_count;
    int16_t last_rssi;
    char status_msg[32];
    FuriMutex* mutex;
} AppState;

// Callback de rendu graphique (Interface Pro)
static void app_draw_callback(Canvas* canvas, void* ctx) {
    AppState* state = ctx;
    furi_mutex_acquire(state->mutex, FuriWaitForever);

    canvas_clear(canvas);
    
    // Cadre principal et titre
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Sub-GHz AI Pro Analyzer");
    canvas_draw_line(canvas, 0, 15, 128, 15);

    canvas_set_font(canvas, FontSecondary);
    
    // Fréquence courante
    char freq_str[32];
    snprintf(freq_str, sizeof(freq_str), "Freq: %lu.%02lu MHz", state->frequency / 1000000, (state->frequency % 1000000) / 10000);
    canvas_draw_str(canvas, 2, 28, freq_str);

    // Statut Matériel
    if(state->is_rx_active) {
        canvas_draw_str(canvas, 80, 28, "[RX ACTIVE]");
    } else {
        canvas_draw_str(canvas, 80, 28, "[RX IDLE]");
    }

    // Statistiques & RSSI
    char rssi_str[32];
    snprintf(rssi_str, sizeof(rssi_str), "RSSI: %d dBm", state->last_rssi);
    canvas_draw_str(canvas, 2, 40, rssi_str);

    char pkts_str[32];
    snprintf(pkts_str, sizeof(pkts_str), "Signaux detectes: %lu", state->packet_count);
    canvas_draw_str(canvas, 2, 52, pkts_str);

    // Barre d'aide en bas
    canvas_draw_line(canvas, 0, 55, 128, 55);
    canvas_draw_str(canvas, 2, 64, "[<>] Freq | [OK] Reset | [Back] Exit");

    furi_mutex_release(state->mutex);
}

// Callback de capture des touches
static void app_input_callback(InputEvent* input_event, void* ctx) {
    FuriMessageQueue* event_queue = ctx;
    AppEvent event = {.type = EventTypeInput, .input = *input_event};
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

// Met à jour la configuration radio du CC1101
static void update_radio_frequency(AppState* state) {
    furi_mutex_acquire(state->mutex, FuriWaitForever);
    state->frequency = frequencies[state->freq_index];
    
    if(state->is_rx_active) {
        furi_hal_subghz_idle(); // Stop temporaire
        furi_hal_subghz_set_frequency(state->frequency);
        furi_hal_subghz_rx(); // Redémarrage sur nouvelle freq
    } else {
        furi_hal_subghz_set_frequency(state->frequency);
    }
    furi_mutex_release(state->mutex);
}

// Point d'entrée principal de l'application
int32_t subghz_advanced_main(void* p) {
    UNUSED(p);

    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(AppEvent));
    AppState* state = malloc(sizeof(AppState));
    state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    
    state->freq_index = 0;
    state->frequency = frequencies[state->freq_index];
    state->is_rx_active = false;
    state->packet_count = 0;
    state->last_rssi = -120;
    strncpy(state->status_msg, "Ready", sizeof(state->status_msg));

    // Config Interface
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, app_draw_callback, state);
    view_port_input_callback_set(view_port, app_input_callback, event_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    // Init Radio Hardware
    furi_hal_subghz_init();
    update_radio_frequency(state);
    furi_hal_subghz_rx();
    state->is_rx_active = true;

    AppEvent event;
    bool running = true;

    while(running) {
        // Lecture des événements avec un timeout court (100ms) pour surveiller le RSSI en continu
        FuriStatus status = furi_message_queue_get(event_queue, &event, 100);

        if(status == FuriStatusOk) {
            if(event.type == EventTypeInput && event.input.type == InputTypeShort) {
                switch(event.input.key) {
                    case InputKeyBack:
                        running = false;
                        break;
                    case InputKeyLeft:
                        if(state->freq_index > 0) state->freq_index--;
                        else state->freq_index = FREQ_COUNT - 1;
                        update_radio_frequency(state);
                        break;
                    case InputKeyRight:
                        if(state->freq_index < FREQ_COUNT - 1) state->freq_index++;
                        else state->freq_index = 0;
                        update_radio_frequency(state);
                        break;
                    case InputKeyOk:
                        furi_mutex_acquire(state->mutex, FuriWaitForever);
                        state->packet_count = 0;
                        state->last_rssi = -120;
                        furi_mutex_release(state->mutex);
                        break;
                    default:
                        break;
                }
            }
        } else if(status == FuriStatusTimeout) {
            // Lecture du RSSI en temps réel si la radio écoute
            if(state->is_rx_active) {
                furi_mutex_acquire(state->mutex, FuriWaitForever);
                
                // Récupération du RSSI brut du CC1101 via l'API HAL
                float rssi_value = furi_hal_subghz_get_rssi();
                state->last_rssi = (int16_t)rssi_value;

                // Si le signal dépasse un seuil de bruit (-85 dBm), on considère qu'on capte quelque chose
                if(state->last_rssi > -85) {
                    state->packet_count++;
                }
                
                furi_mutex_release(state->mutex);
                view_port_update(view_port);
            }
        }
    }

    // Fermeture propre du matériel radio
    furi_hal_subghz_idle();
    furi_hal_subghz_sleep();
    furi_hal_subghz_shutdown();

    // Libération de la mémoire
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(event_queue);
    furi_mutex_free(state->mutex);
    free(state);

    return 0;
}
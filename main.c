#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>

#define TAG "SubGhzSmartScanner"
#define RSSI_THRESHOLD -75 // Seuil en dBm pour capturer un signal actif

// Liste des fréquences majeures à balayer en boucle
static const uint32_t scan_frequencies[] = {
    315000000, // Télécommandes USA / Asie
    433920000, // Domotique, portails, alarmes (Standard Europe)
    868350000, // Systèmes de sécurité et capteurs modernes
};
#define FREQ_COUNT (sizeof(scan_frequencies) / sizeof(scan_frequencies[0]))

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
    bool is_scanning;       // Mode balayage automatique activé
    uint32_t signals_found; // Compteur de détections
    int16_t current_rssi;   // RSSI actuel en dBm
    int16_t max_rssi;       // Pic historique enregistré
    uint32_t last_scan_tick;
    FuriMutex* mutex;
} AppState;

// Callback d'affichage de l'interface graphique (GUI)
static void app_draw_callback(Canvas* canvas, void* ctx) {
    AppState* state = ctx;
    furi_mutex_acquire(state->mutex, FuriWaitForever);

    canvas_clear(canvas);
    
    // En-tête de l'application
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Sub-GHz Smart Scanner");
    canvas_draw_line(canvas, 0, 15, 128, 15);

    canvas_set_font(canvas, FontSecondary);
    
    // Fréquence en cours d'écoute
    char freq_str[32];
    snprintf(freq_str, sizeof(freq_str), "Freq: %lu.%02lu MHz", state->frequency / 1000000, (state->frequency % 1000000) / 10000);
    canvas_draw_str(canvas, 2, 27, freq_str);

    // État du scanner
    if(state->is_scanning) {
        canvas_draw_str(canvas, 82, 27, "[SCANNING]");
    } else {
        canvas_draw_str(canvas, 82, 27, "[LOCK/IDLE]");
    }

    // Affichage des puissances (RSSI)
    char rssi_str[32];
    snprintf(rssi_str, sizeof(rssi_str), "RSSI Actuel: %d dBm", state->current_rssi);
    canvas_draw_str(canvas, 2, 39, rssi_str);

    char max_rssi_str[32];
    snprintf(max_rssi_str, sizeof(max_rssi_str), "Pic Max: %d dBm", state->max_rssi);
    canvas_draw_str(canvas, 2, 49, max_rssi_str);

    char count_str[32];
    snprintf(count_str, sizeof(count_str), "Alertes capturees: %lu", state->signals_found);
    canvas_draw_str(canvas, 2, 59, count_str);

    // Indicateur graphique de puissance (Barre de progression en bas)
    int bar_width = (state->current_rssi + 120) * 128 / 100; // Mappe le RSSI (-120 à -20 dBm) sur l'écran
    if(bar_width < 0) bar_width = 0;
    if(bar_width > 128) bar_width = 128;
    canvas_draw_box(canvas, 0, 62, bar_width, 2);

    furi_mutex_release(state->mutex);
}

// Callback de gestion des touches du clavier
static void app_input_callback(InputEvent* input_event, void* ctx) {
    FuriMessageQueue* event_queue = ctx;
    AppEvent event = {.type = EventTypeInput, .input = *input_event};
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

// Configuration matérielle de la puce CC1101
static void set_radio_frequency(AppState* state, uint32_t freq) {
    furi_mutex_acquire(state->mutex, FuriWaitForever);
    state->frequency = freq;
    furi_hal_subghz_idle();
    furi_hal_subghz_set_frequency(state->frequency);
    furi_hal_subghz_rx();
    furi_mutex_release(state->mutex);
}

// Fonction principale de l'application
int32_t subghz_advanced_main(void* p) {
    UNUSED(p);

    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(AppEvent));
    AppState* state = malloc(sizeof(AppState));
    state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    
    state->freq_index = 0;
    state->frequency = scan_frequencies[state->freq_index];
    state->is_scanning = true;
    state->signals_found = 0;
    state->current_rssi = -120;
    state->max_rssi = -120;
    state->last_scan_tick = furi_get_tick();

    // Initialisation du ViewPort pour l'affichage graphique
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, app_draw_callback, state);
    view_port_input_callback_set(view_port, app_input_callback, event_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    // Initialisation du matériel radio
    furi_hal_subghz_init();
    set_radio_frequency(state, state->frequency);

    AppEvent event;
    bool running = true;

    while(running) {
        // Interrogation de la file d'attente (timeout court de 50ms pour rester réactif)
        FuriStatus status = furi_message_queue_get(event_queue, &event, 50);

        if(status == FuriStatusOk) {
            if(event.type == EventTypeInput && event.input.type == InputTypeShort) {
                switch(event.input.key) {
                    case InputKeyBack:
                        running = false; // Quitte l'application
                        break;
                    case InputKeyOk:
                        // Réinitialise les statistiques et relance le scan automatique
                        furi_mutex_acquire(state->mutex, FuriWaitForever);
                        state->signals_found = 0;
                        state->max_rssi = -120;
                        state->is_scanning = true;
                        furi_mutex_release(state->mutex);
                        break;
                    case InputKeyLeft:
                        // Sélection manuelle de fréquence (coupe le mode automatique)
                        state->is_scanning = false;
                        if(state->freq_index > 0) state->freq_index--;
                        else state->freq_index = FREQ_COUNT - 1;
                        set_radio_frequency(state, scan_frequencies[state->freq_index]);
                        break;
                    case InputKeyRight:
                        state->is_scanning = false;
                        if(state->freq_index < FREQ_COUNT - 1) state->freq_index++;
                        else state->freq_index = 0;
                        set_radio_frequency(state, scan_frequencies[state->freq_index]);
                        break;
                    default:
                        break;
                }
            }
        } else if(status == FuriStatusErrorTimeout) {
            // Pas d'entrée utilisateur : exécution des tâches d'arrière-plan de la radio
            furi_mutex_acquire(state->mutex, FuriWaitForever);
            float rssi_value = furi_hal_subghz_get_rssi();
            state->current_rssi = (int16_t)rssi_value;

            // Enregistrement du pic historique de puissance
            if(state->current_rssi > state->max_rssi) {
                state->max_rssi = state->current_rssi;
            }

            // Gestion du comportement de scan intelligent
            if(state->current_rssi > RSSI_THRESHOLD) {
                if(state->is_scanning) {
                    state->signals_found++;
                    state->is_scanning = false; // Verrouille la fréquence actuelle pour l'observer
                }
            } else if(state->is_scanning) {
                // Si aucun signal n'est détecté, on passe à la fréquence suivante toutes les 300ms
                if(furi_get_tick() - state->last_scan_tick > 300) {
                    state->freq_index = (state->freq_index + 1) % FREQ_COUNT;
                    furi_mutex_release(state->mutex); // Libère le mutex avant de toucher au matériel
                    set_radio_frequency(state, scan_frequencies[state->freq_index]);
                    furi_mutex_acquire(state->mutex, FuriWaitForever);
                    state->last_scan_tick = furi_get_tick();
                }
            }
            
            furi_mutex_release(state->mutex);
            view_port_update(view_port);
        }
    }

    // Arrêt propre et libération de la puce radio
    furi_hal_subghz_idle();
    furi_hal_subghz_sleep();
    furi_hal_subghz_shutdown();

    // Nettoyage complet des structures mémoire
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(event_queue);
    furi_mutex_free(state->mutex);
    free(state);

    return 0;
}

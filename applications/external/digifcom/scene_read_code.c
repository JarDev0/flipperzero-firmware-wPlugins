/*
Listen to two devices chat and then allow options for saving either code

Left save s code
Right save r code
Back return

Post save should go back to us

TODO: Need to test this... It may not actually work as-is

(It probably won't anymore for sure)
*/
#include "flipper.h"
#include "app_state.h"
#include "scenes.h"
#include "dmcomm_lib/fcom.h"
#include "scene_read_code.h"
#include <furi_hal_cortex.h>

/*
Callback from dmcomm thread with serial results
*/
void processReadInput(void* context) {
    furi_assert(context);
    App* app = context;

    char out[64];
    size_t recieved = 0;
    memset(out, 0, 64);

    recieved = furi_stream_buffer_receive(app->dmcomm_output_stream, &out, 63, 0);
    UNUSED(recieved);
    FURI_LOG_I(TAG, "DMComm Data: %s", out);

    if(app->state->waitForCode) {
        FURI_LOG_I(TAG, "reading code");
        furi_string_reset(app->state->r_code);
        furi_string_reset(app->state->s_code);
        int rpackets = 0;
        int spackets = 0;
        int l = strlen(out);
        int first = true;
        for(int i = 0; i < l; i++) {
            if(out[i] == 's' && i + 5 < l) {
                FURI_LOG_I(TAG, "found s");
                if(furi_string_empty(app->state->s_code)) {
                    if(first) {
                        furi_string_cat_printf(app->state->s_code, "V1-");
                        first = false;
                    } else
                        furi_string_cat_printf(app->state->s_code, "V2-");
                } else
                    furi_string_cat_printf(app->state->s_code, "-");

                i += 2; // :
                for(int j = 0; j < 4; j++)
                    furi_string_push_back(app->state->s_code, out[i++]); // 4 hex
                spackets++;
            }

            if(out[i] == 'r' && i + 5 < l) {
                FURI_LOG_I(TAG, "found r");
                if(furi_string_empty(app->state->r_code)) {
                    if(first) {
                        furi_string_cat_printf(app->state->r_code, "V1-");
                        first = false;
                    } else
                        furi_string_cat_printf(app->state->r_code, "V2-");
                } else
                    furi_string_cat_printf(app->state->r_code, "-");

                i += 2; // :
                for(int j = 0; j < 4; j++)
                    furi_string_push_back(app->state->r_code, out[i++]); // 4 hex
                rpackets++;
            }
        }

        FURI_LOG_I(TAG, "s code %s", furi_string_get_cstr(app->state->s_code));
        FURI_LOG_I(TAG, "r code %s", furi_string_get_cstr(app->state->r_code));

        //if spackets == rpackets and spackets = code packets, then present code for saving
        if(rpackets > 0 && spackets > 0 && abs(rpackets - spackets) <= 1) {
            dialog_ex_set_header(
                app->dialog, furi_string_get_cstr(app->state->s_code), 10, 12, AlignLeft, AlignTop);
            dialog_ex_set_text(
                app->dialog, furi_string_get_cstr(app->state->r_code), 10, 24, AlignLeft, AlignTop);

            app->state->waitForCode = false;
            dialog_ex_set_left_button_text(app->dialog, "Save Top");
            dialog_ex_set_right_button_text(app->dialog, "Save Bot");
        }
    }

    FURI_LOG_I(TAG, "done");
}

void read_code_cb(void* context) {
    // This needs to be pretty short or it will delay comms
    furi_assert(context);
    App* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, SerialInputAvailable);
}

void read_code_dialog_callback(DialogExResult result, void* context) {
    furi_assert(context);
    App* app = context;
    UNUSED(app);
    app->state->save_code_return_scene = FcomReadCodeScene;
    if(result == DialogExResultRight) {
        FURI_LOG_I(TAG, "DialogExResultRight");
        // copy r_code
        strncpy(
            app->state->result_code, furi_string_get_cstr(app->state->r_code), MAX_DIGIROM_LEN);
        scene_manager_next_scene(app->scene_manager, FcomSaveCodeScene);
    }
    if(result == DialogExResultLeft) {
        FURI_LOG_I(TAG, "DialogExResultLeft");
        // copy s_code
        strncpy(
            app->state->result_code, furi_string_get_cstr(app->state->s_code), MAX_DIGIROM_LEN);
        scene_manager_next_scene(app->scene_manager, FcomSaveCodeScene);
    }
}

void fcom_read_code_scene_on_enter(void* context) {
    FURI_LOG_I(TAG, "fcom_read_code_scene_on_enter");
    App* app = context;

    // TODO: somehow if we return from save dialog, don't clear and restart the read
    // because we will want to allow to save both codes

    dialog_ex_set_header(app->dialog, "Waiting For Data", 64, 12, AlignCenter, AlignTop);
    dialog_ex_set_text(
        app->dialog, "Connect to device bus and transfer data", 10, 24, AlignLeft, AlignTop);
    dialog_ex_set_left_button_text(app->dialog, NULL);
    dialog_ex_set_right_button_text(app->dialog, NULL);
    dialog_ex_set_center_button_text(
        app->dialog, NULL); // This will eventually be a "resend" button
    dialog_ex_set_result_callback(app->dialog, read_code_dialog_callback);
    dialog_ex_set_context(app->dialog, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, FcomReadCodeView);

    app->state->waitForCode = true;
    set_serial_callback(read_code_cb);
    furi_string_reset(app->state->r_code);
    furi_string_reset(app->state->s_code);

    dmcomm_sendcommand(app, app->state->current_code);
}

bool fcom_read_code_scene_on_event(void* context, SceneManagerEvent event) {
    FURI_LOG_I(TAG, "fcom_read_code_scene_on_event");
    App* app = context;
    bool consumed = false;
    switch(event.type) {
    case SceneManagerEventTypeCustom:
        switch(event.event) {
        case SerialInputAvailable:
            processReadInput(app);
            consumed = true;
            break;
        }
        break;
    default: // eg. SceneManagerEventTypeBack, SceneManagerEventTypeTick
        consumed = false;
        break;
    }
    return consumed;
}

void fcom_read_code_scene_on_exit(void* context) {
    FURI_LOG_I(TAG, "fcom_read_code_scene_on_exit");
    UNUSED(context);
    App* app = context;
    UNUSED(app);

    set_serial_callback(NULL);
    dmcomm_sendcommand(app, "0\n");
    app->state->waitForCode = false;
}

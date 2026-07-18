#include <stdio.h>
#include <string.h>
#include "mvs_protocol.h"

int main(void)
{
    mvs_preeq_state_t peq;
    memset(&peq, 0, sizeof(peq));
    peq.block_enabled = 1;
    peq.pre_gain_raw = -384;
    peq.selected_filter = 7;
    for (int i = 0; i < 10; i++) {
        peq.filters[i].enabled = (i % 3) == 0;
        peq.filters[i].type = (uint16_t)(i % 9);
        peq.filters[i].frequency_hz = (uint16_t)(35 + i * 317);
        peq.filters[i].q_raw = (uint16_t)(700 + i * 37);
        peq.filters[i].gain_raw = (int16_t)(-512 + i * 111);
    }
    uint8_t peq_frame[112];
    if (mvs_build_preeq_full_frame(&peq, peq_frame, sizeof(peq_frame)) != ESP_OK)
        return 1;
    fwrite(peq_frame, 1, sizeof(peq_frame), stdout);

    mvs_drc_packed_state_t drc;
    memset(&drc, 0, sizeof(drc));
    drc.enabled = 1; drc.mode = 0; drc.crossover_type = 2;
    drc.crossover_q1_raw = 724; drc.crossover_q2_raw = 1024;
    drc.crossover_freq1_hz = 300; drc.crossover_freq2_hz = 2000;
    for (int i = 0; i < 4; i++) {
        drc.thresholds[i] = (int16_t)(-500 - i * 125);
        drc.ratios[i] = (uint16_t)(100 + i * 25);
        drc.attacks[i] = (uint16_t)(2 + i);
        drc.releases[i] = (uint16_t)(100 + i * 200);
        drc.pregains[i] = (uint16_t)(4096 + i * 300);
    }
    uint8_t drc_frame[60];
    if (mvs_build_drc_full_frame(&drc, drc_frame, sizeof(drc_frame)) != ESP_OK)
        return 1;
    fwrite(drc_frame, 1, sizeof(drc_frame), stdout);
    return 0;
}

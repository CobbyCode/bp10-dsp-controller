#include <assert.h>
#include <stdio.h>

#include "mvs_device_profile.h"
#include "mvs_protocol.h"

int main(void)
{
    mvs_device_profile_t profile;
    mvs_device_profile_begin_generic(&profile, 0x8888, 0x1719, 4, 26);

    assert(mvs_device_profile_map_catalog_entry(
        &profile, 6, 13, "Music Virtual Bass"));
    assert(mvs_device_profile_map_catalog_entry(
        &profile, 7, 13, "Rec Virtual Bass"));

    mvs_effect_path_t *music = &profile.paths[MVS_PATH_MUSIC];
    mvs_effect_path_t *rec = &profile.paths[MVS_PATH_REC];
    music->present = true;
    rec->present = true;

    assert(music->virtual_bass.effect_id == 0x86);
    assert(rec->virtual_bass.effect_id == 0x87);
    assert(mvs_device_profile_set_module_validated(
        &profile, MVS_PATH_MUSIC, &music->virtual_bass,
        MVS_MODULE_VIRTUAL_BASS, true, 8));
    assert(!mvs_device_profile_set_module_validated(
        &profile, MVS_PATH_REC, &music->virtual_bass,
        MVS_MODULE_VIRTUAL_BASS, true, 8));
    assert(music->virtual_bass.available);
    assert(!rec->virtual_bass.available);

    assert(mvs_device_profile_set_module_validated(
        &profile, MVS_PATH_REC, &rec->virtual_bass,
        MVS_MODULE_VIRTUAL_BASS, true, 8));
    assert(music->virtual_bass.available);
    assert(rec->virtual_bass.available);
    assert(profile.virtual_bass.effect_id == 0x86);

    uint8_t music_frame[8];
    uint8_t rec_frame[8];
    assert(mvs_build_write_frame(music->virtual_bass.effect_id, 0, 1,
                                 music_frame, sizeof(music_frame)) == ESP_OK);
    assert(mvs_build_write_frame(rec->virtual_bass.effect_id, 0, 1,
                                 rec_frame, sizeof(rec_frame)) == ESP_OK);
    assert(music_frame[2] == 0x86);
    assert(rec_frame[2] == 0x87);

    puts("bass_path_isolation_host_tests: PASS");
    return 0;
}

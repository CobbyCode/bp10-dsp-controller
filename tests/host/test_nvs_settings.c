#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "nvs_settings.h"

#define SLOT_COUNT 32
#define VALUE_MAX 1024
typedef struct { bool used; char key[16]; size_t len; unsigned char value[VALUE_MAX]; } slot_t;
static slot_t slots[SLOT_COUNT];

static slot_t *find_slot(const char *key, bool create)
{
    for (size_t i = 0; i < SLOT_COUNT; ++i)
        if (slots[i].used && strcmp(slots[i].key, key) == 0) return &slots[i];
    if (!create) return NULL;
    for (size_t i = 0; i < SLOT_COUNT; ++i) if (!slots[i].used) {
        slots[i].used = true;
        snprintf(slots[i].key, sizeof(slots[i].key), "%s", key);
        return &slots[i];
    }
    return NULL;
}
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) { (void)name; (void)mode; *handle = 1; return ESP_OK; }
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t len) { (void)h; slot_t *s=find_slot(key,true); if(!s||len>VALUE_MAX)return ESP_ERR_NO_MEM; memcpy(s->value,value,len); s->len=len; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *value, size_t *len) { (void)h; slot_t *s=find_slot(key,false); if(!s)return ESP_ERR_NOT_FOUND; if(*len<s->len){*len=s->len;return ESP_ERR_INVALID_SIZE;} memcpy(value,s->value,s->len); *len=s->len; return ESP_OK; }
esp_err_t nvs_set_str(nvs_handle_t h,const char*k,const char*v){return nvs_set_blob(h,k,v,strlen(v)+1);}
esp_err_t nvs_get_str(nvs_handle_t h,const char*k,char*v,size_t*l){return nvs_get_blob(h,k,v,l);}
esp_err_t nvs_erase_key(nvs_handle_t h,const char*k){(void)h;slot_t*s=find_slot(k,false);if(!s)return ESP_ERR_NOT_FOUND;memset(s,0,sizeof(*s));return ESP_OK;}
esp_err_t nvs_erase_all(nvs_handle_t h){(void)h;memset(slots,0,sizeof(slots));return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;return ESP_OK;}

static dsp_profile_t sample_profile(unsigned marker)
{
    dsp_profile_t p = {0};
    p.noise_suppressor_enabled = true;
    p.noise_suppressor_ratio = (uint16_t)marker;
    p.virtual_bass_cutoff_hz = (uint16_t)(80 + marker);
    return p;
}

static mvs_schema_fingerprint_t fingerprint(uint16_t vid, uint16_t pid,
                                            uint8_t adapter, uint16_t module)
{
    mvs_schema_fingerprint_t fp = { .vid=vid, .pid=pid, .adapter_kind=adapter,
                                    .module_type_count=1, .module_types={module} };
    return fp;
}

static void test_legacy_migrates_once(void)
{
    nvs_erase_all(1);
    dsp_profile_t legacy=sample_profile(11), newer=sample_profile(22), loaded;
    assert(nvs_settings_save_dsp_config(&legacy)==ESP_OK);
    assert(nvs_settings_migrate_legacy()==ESP_OK);
    assert(!nvs_settings_has_dsp_config());
    assert(nvs_settings_load_a800x_config(&loaded)==ESP_OK && loaded.noise_suppressor_ratio==11);
    assert(nvs_settings_save_dsp_config(&newer)==ESP_OK);
    assert(nvs_settings_migrate_legacy()==ESP_OK);
    assert(nvs_settings_load_a800x_config(&loaded)==ESP_OK && loaded.noise_suppressor_ratio==11);
}

static void test_separation_and_fingerprint_matching(void)
{
    nvs_erase_all(1);
    dsp_profile_t a800x=sample_profile(31), generic=sample_profile(41), loaded;
    mvs_schema_fingerprint_t fp=fingerprint(0x8888,0x1719,MVS_DEVICE_GENERIC_ACP,5);
    assert(nvs_settings_save_a800x_config(&a800x)==ESP_OK);
    assert(nvs_settings_load_generic_config(&fp,&loaded)==ESP_ERR_NOT_FOUND);
    assert(nvs_settings_save_generic_config(&fp,&generic)==ESP_OK);
    assert(nvs_settings_load_generic_config(&fp,&loaded)==ESP_OK);
    assert(memcmp(&loaded,&generic,sizeof(loaded))==0);
    mvs_schema_fingerprint_t variants[] = {
        fingerprint(0x9999,0x1719,MVS_DEVICE_GENERIC_ACP,5),
        fingerprint(0x8888,0x9999,MVS_DEVICE_GENERIC_ACP,5),
        fingerprint(0x8888,0x1719,MVS_DEVICE_A800X_FIXED,5),
        fingerprint(0x8888,0x1719,MVS_DEVICE_GENERIC_ACP,13),
    };
    for(size_t i=0;i<sizeof(variants)/sizeof(variants[0]);++i)
        assert(nvs_settings_load_generic_config(&variants[i],&loaded)==ESP_ERR_NOT_FOUND);
}

static void test_factory_reset(void)
{
    dsp_profile_t p=sample_profile(51), loaded;
    mvs_schema_fingerprint_t fp=fingerprint(0x8888,0x1719,MVS_DEVICE_GENERIC_ACP,5);
    nvs_erase_all(1);
    assert(nvs_settings_save_dsp_config(&p)==ESP_OK);
    assert(nvs_settings_save_a800x_config(&p)==ESP_OK);
    assert(nvs_settings_save_generic_config(&fp,&p)==ESP_OK);
    assert(nvs_settings_factory_reset()==ESP_OK);
    assert(!nvs_settings_has_dsp_config() && !nvs_settings_has_a800x_config());
    assert(nvs_settings_load_generic_config(&fp,&loaded)==ESP_ERR_NOT_FOUND);
}

static void test_restore_gate_has_no_writes_on_mismatch(void)
{
    dsp_profile_t p=sample_profile(61), loaded;
    mvs_schema_fingerprint_t saved=fingerprint(0x8888,0x1719,MVS_DEVICE_GENERIC_ACP,5);
    mvs_schema_fingerprint_t discovered=fingerprint(0x8888,0x1719,MVS_DEVICE_GENERIC_ACP,13);
    unsigned dsp_writes=0;
    nvs_erase_all(1);
    assert(nvs_settings_save_generic_config(&saved,&p)==ESP_OK);
    if (nvs_settings_load_generic_config(&discovered,&loaded)==ESP_OK) dsp_writes++;
    assert(dsp_writes==0);
    discovered=saved;
    if (nvs_settings_load_generic_config(&discovered,&loaded)==ESP_OK) dsp_writes++;
    assert(dsp_writes==1);
}

int main(void)
{
    test_legacy_migrates_once();
    test_separation_and_fingerprint_matching();
    test_factory_reset();
    test_restore_gate_has_no_writes_on_mismatch();
    puts("nvs_settings_host_tests: PASS");
    return 0;
}

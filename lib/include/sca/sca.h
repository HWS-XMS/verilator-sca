#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void sca_init(const char* config_path);
void sca_set_metadata(const char* key, const char* value);
void sca_start_capture();
void sca_sample();
void sca_end_capture();
void sca_save(const char* output_path);

#ifdef __cplusplus
}
#endif

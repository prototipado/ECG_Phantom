#ifndef ECG_TYPES_H
#define ECG_TYPES_H

typedef struct {
    const char* rhythms;
    const char* cond_abnor;
    const char* hypertrophies;
    const char* ischemia;
    const char* pacing;
    const char* extrasystolies;
    int rows;
    int cols;
    const int* array; 
} ecg_struct_t;

#endif // ECG_TYPES_H

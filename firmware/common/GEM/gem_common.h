#ifndef GEM_COMMON_H
#define GEM_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Basic types
typedef uint8_t byte;

// Return codes
typedef enum {
    GEM_OK = 0,
    GEM_ERROR = -1
} gem_err_t;

// Key codes
typedef enum {
    GEM_KEY_NONE = 0,
    GEM_KEY_UP = 1,
    GEM_KEY_RIGHT = 2,
    GEM_KEY_DOWN = 3,
    GEM_KEY_LEFT = 4,
    GEM_KEY_CANCEL = 5,
    GEM_KEY_OK = 6
} gem_key_t;

// Item types
typedef enum {
    GEM_ITEM_VAL = 0,
    GEM_ITEM_LINK = 1,
    GEM_ITEM_BACK = 2,
    GEM_ITEM_BUTTON = 3,
    GEM_ITEM_LABEL = 4,
    GEM_ITEM_SPACER = 5
} gem_item_type_t;

// Value types
typedef enum {
    GEM_VAL_INTEGER = 0,
    GEM_VAL_BYTE,
    GEM_VAL_FLOAT,
    GEM_VAL_DOUBLE,
    GEM_VAL_BOOL,
    GEM_VAL_CHAR,   // String
    GEM_VAL_SELECT,
    GEM_VAL_SPINNER,
    GEM_VAL_UINT8,
    GEM_VAL_UINT16,
    GEM_VAL_INT16
} gem_val_type_t;

// Callback Data
struct gem_item; // Forward declaration

typedef struct {
    struct gem_item* pMenuItem;
    union {
        byte valByte;
        int valInt;
        float valFloat;
        double valDouble;
        bool valBool;
        const char* valChar;
        void* valPointer;
    };
} gem_callback_data_t;

typedef void (*gem_callback_t)(gem_callback_data_t data);

// Constants
#define GEM_STR_LEN 21 // Max string length for values

#endif // GEM_COMMON_H

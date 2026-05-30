/**
 * nvs_store_stub.cpp — host stub for the NVS key/value store.
 *
 * claude_widget reads nvs_store::get_str("claude","endpoint",...) at construction;
 * returning the default ("") makes it take the no-legacy-config branch.
 */
#include "../../src/os/nvs_store.h"

namespace nvs_store {
    void    put_i32(const char*, const char*, int32_t) {}
    int32_t get_i32(const char*, const char*, int32_t d) { return d; }
    void    put_u8(const char*, const char*, uint8_t) {}
    uint8_t get_u8(const char*, const char*, uint8_t d) { return d; }
    void    put_str(const char*, const char*, const char*) {}
    String  get_str(const char*, const char*, const char* d) { return String(d ? d : ""); }
}

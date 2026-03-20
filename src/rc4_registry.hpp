#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>


class RC4Registry {
public:
    bool rc4=false;
    int rc4_i=0;
    uint64_t start_time=0;
    uint64_t pre_time=0; //previous time
    uint32_t register_transponder(uint64_t timestamp, uint32_t transponder_id);
    RC4Registry();
    void clear();
    

private:
    void init_db(); 
    void sort_by_rc4_ids();
    uint64_t save_to_db();
    uint64_t find_id_by_transponder(uint64_t target_id);
    std::unordered_map<uint64_t, uint64_t> lookup_cache;
};
extern RC4Registry g_rc4_registry;



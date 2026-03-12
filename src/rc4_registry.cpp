#include "rc4_registry.hpp"
#include <algorithm>
#include <sqlite3.h>
#include <iostream>
#include <sstream>
#include <string>
std::vector<std::vector<uint32_t>> rc4_ids(1000, std::vector<uint32_t>(2, 1));

// Global registry instance
RC4Registry g_rc4_registry;
RC4Registry::RC4Registry() {
    init_db();
}
void RC4Registry::init_db() {
    sqlite3* db;
    char* err_msg = 0;   
    int rc = sqlite3_open("openstint_rc4.db", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Unable to start database initialization: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

      // 2. Create a table and set the AUTOINCREMENT starting value
    // Use batch commands: Create table -> Attempt to insert starting value
    const char* init_sql = 
        "CREATE TABLE IF NOT EXISTS transponder_rc4 ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  transponder_id INTEGER UNIQUE,"
        "  rc4_ids TEXT,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        
        // 設定起始值 (下一筆從 1,000,000 開始)
        "INSERT OR IGNORE INTO sqlite_sequence (name, seq) VALUES ('transponder_rc4', 999999);"

        // 當新增資料後，自動將 id 填入 transponder_id
        "CREATE TRIGGER IF NOT EXISTS sync_transponder_id "
        "AFTER INSERT ON transponder_rc4 "
        "FOR EACH ROW "
        "WHEN NEW.transponder_id IS NULL " 
        "BEGIN "
        "  UPDATE transponder_rc4 SET transponder_id = NEW.id WHERE id = NEW.id; "
        "END;";

    rc = sqlite3_exec(db, init_sql, 0, 0, &err_msg);
    
    if (rc != SQLITE_OK) {
        std::cerr << "Initializing the data table failed: " << err_msg << std::endl;
        sqlite3_free(err_msg);
    } else {
        
    }

    sqlite3_close(db);
}
uint64_t RC4Registry::save_to_db() {  
    sort_by_rc4_ids();
    if(rc4_ids[0][1]<200){ 
        clear();
        return 0;
    }
    std::stringstream ss;
    int limit = std::min(this->rc4_i, 32);
    for (int i = 0; i < limit; i++) {
        ss << rc4_ids[i][0]; 
        if (i < limit - 1) {
            ss << ","; 
        }
    }
    std::string final_ids = ss.str();

    if (final_ids.empty()) return 0;
    uint64_t new_id=0;  
    sqlite3* db;
    if (sqlite3_open("openstint_rc4.db", &db) == SQLITE_OK) {
        std::string sql = "INSERT INTO transponder_rc4 (rc4_ids) VALUES ('" + final_ids + "');";
        char* err_msg = 0;
        int rc = sqlite3_exec(db, sql.c_str(), 0, 0, &err_msg);
        
        if (rc == SQLITE_OK) {            
            new_id = sqlite3_last_insert_rowid(db);
            std::cout << "RC4 comparison data successfully saved, ID: " << new_id << std::endl;
        } else {
            fprintf(stderr, "SQL Error: %s\n", err_msg);
            sqlite3_free(err_msg);            
        }
        sqlite3_close(db);
    }
    return new_id;
}
uint64_t RC4Registry::find_id_by_transponder(uint64_t target_id) {
    sqlite3* db;
    sqlite3_stmt* stmt;
    uint64_t found_db_id = 0;

    
    if (sqlite3_open("openstint_rc4.db", &db) != SQLITE_OK) {
        return 0;
    }

        // 2. Use precise comparison logic:
    // Pad rc4_ids with commas before and after it, and also pad the search with commas before and after it to ensure that complete numbers are compared.
    // For example: search if ",3157844207," exists within ",123,3157844207,456,"
    std::string sql = "SELECT transponder_id FROM transponder_rc4 WHERE ',' || rc4_ids || ',' LIKE ? LIMIT 1;";

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) == SQLITE_OK) {
        // Convert uint64_t to a string and wrap it as a LIKE parameter
        std::string search_str = "%," + std::to_string(target_id) + ",%";
        sqlite3_bind_text(stmt, 1, search_str.c_str(), -1, SQLITE_TRANSIENT);

        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            found_db_id = sqlite3_column_int64(stmt, 0);
        }
    } else {
        std::cerr << "SQL Prepare error: " << sqlite3_errmsg(db) << std::endl;
    }

    // 4. 清理與關閉
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return found_db_id;
}
uint32_t RC4Registry::register_transponder(uint64_t timestamp,uint32_t transponder_id) {
    bool t=true;
    uint64_t id=find_id_by_transponder(transponder_id);
    if(id != 0){
        clear();
        return id; 
    }
    if(pre_time != 0 && timestamp-pre_time > 1000){
        clear();
        return 0;
    }     
    for(int i=0;i <= rc4_i;i++){
        if(rc4_ids[i][0] == transponder_id){
            rc4_ids[i][1]++;
            t=false;
            break;
        }
    }
    if(t){                
        if(rc4_i == 0)start_time=timestamp;
        rc4_ids[rc4_i][0]=transponder_id;
        if(rc4_i < 999) rc4_i++;
    }
    if(pre_time !=0 && timestamp - start_time > 15000){
        save_to_db();
        clear();
        return transponder_id;
    }
    pre_time=timestamp;
    return 0;

}
void RC4Registry::clear() {
    rc4 = false;    
    if(rc4_i > 0) {
        for (int i = 0; i < rc4_i; i++) {
            rc4_ids[i][0] = 0;
            rc4_ids[i][1] = 1;
        }
    }
    rc4_i = 0;
    pre_time=0;
}
void RC4Registry::sort_by_rc4_ids() {
    
    if (rc4_i < 2) return; 

    // Sort rc4_ids[i][1] (cumulative count) in descending order.
    std::sort(rc4_ids.begin(), rc4_ids.begin() + rc4_i, 
        [](const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
            return a[1] > b[1]; 
        });
}

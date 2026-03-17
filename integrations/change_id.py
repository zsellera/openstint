#!/usr/bin/env python3
import sqlite3
import sys
import os

def main():
    # 1. Check command line arguments count
    if len(sys.argv) != 4:
        print("Usage: python3 change_id.py [DB_PATH] [ID] [NEW_TRANSPONDER_ID]")
        print("Example: python3 change_id.py /openstint_rc4.db 1000000 888888")
        sys.exit(1)

    db_path = sys.argv[1]            # Database path
    target_id = sys.argv[2]          # Internal database ID
    new_transponder_id = sys.argv[3] # New transponder_id to set

    # 2. Check if the database file exists
    if not os.path.exists(db_path):
        print(f"❌ Error: Database file not found: {db_path}")
        sys.exit(1)

    conn = None
    try:
        # 3. Establish connection
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()

        # 4. Prepare and execute the update statement (using ? for parameterization)
        sql = "UPDATE transponder_rc4 SET transponder_id = ? WHERE transponder_id = ?"
        cursor.execute(sql, (new_transponder_id, target_id))
        
        # Commit the changes
        conn.commit()

        # 5. Check the execution result
        if cursor.rowcount > 0:
            print(f"✅ Success! In [{db_path}], ID: {target_id} has been updated to Transponder ID: {new_transponder_id}")
        else:
            print(f"⚠️  No changes: ID {target_id} not found, or the new ID is identical to the current one.")

    except sqlite3.IntegrityError:
        # Handle unique constraint violation
        print(f"❌ Error: New Transponder ID ({new_transponder_id}) already exists in the database (Unique Constraint Failed).")
    except sqlite3.Error as e:
        print(f"❌ Database error: {e}")
    except Exception as e:
        print(f"❌ Execution error: {e}")
    finally:
        # 6. Close the connection
        if conn:
            conn.close()

if __name__ == "__main__":
    main()


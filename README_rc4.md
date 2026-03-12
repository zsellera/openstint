# RC4 Transponder ID 

There is no decoding method for RC4 transponders, so please refer to  [RCHourglass](https://github.com/mv4wd/RCHourglass) for a solution for RC4 transponder

## How to add a new ID:

Place a vehicle containing an RC4 transponder in the antenna loop for approximately 15 seconds. The program will automatically generate an ID starting with 1000000, which will correspond to that RC4 transponder.
## How to change ID number
change_id.py db_path orange_id new_id
### example 
PS D:\Downloads\openstint-windows-x64 (3)\integrations> python.exe .\change_id.py ..\openstint_rc4.db 1000000 6170831
✅ Success! In [..\openstint_rc4.db], ID: 1000000 has been updated to Transponder ID: 6170831
# RC4 Transponder ID 

There is no decoding method for RC4 transponders, so please refer to  [RCHourglass](https://github.com/mv4wd/RCHourglass) for a solution for RC4 transponder

## How to add a new ID:

Place a vehicle containing an RC4 transponder in the antenna loop for approximately 15 seconds. The program will automatically generate an ID starting with 1000000, which will correspond to that RC4 transponder.
## How to change ID number
change_id.py db_path orange_id new_id
### example 
PS D:\Downloads\openstint-windows-x64 (3)\integrations> python.exe .\change_id.py ..\openstint_rc4.db 1000000 6170831
✅ Success! In [..\openstint_rc4.db], ID: 1000000 has been updated to Transponder ID: 6170831

S 25007 -42.8074 0.8859625 3013 0
S 30012 -42.8074 0.8859625 2890 0
RC4 comparison data successfully saved, ID: 1000000
S 35014 -42.90708 0.88302165 1931 515
P 32751 RC4 1000000 -1.38 514 0
 
        ⬇️

S 5003 -43.486725 0.8898394 0 0
P 6994 RC4 6170831 -5.29 86 0
S 10005 -43.48082 0.8907051 115 86
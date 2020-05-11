Import("env")
#env.Replace( MKSPIFFSTOOL='python" "' + env.get("PROJECT_DIR") + '/mklittlefs.py' )
env.Replace (MKSPIFFSTOOL = env.get ("PROJECT_DIR") + "/tools/LittleFs/mklittlefs.exe")
#env.Replace( MKSPIFFSTOOL='python" "' + env.get('PROJECT_DIR') + '/mklittlefs.py' )

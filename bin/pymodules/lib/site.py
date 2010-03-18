"""
this is the Naali site.py for windows, where PYTHONHOME in Naali code is set to the pymodules dir.
on linux and mac the os has py and we can use the stdlib from there, PYTHONHOME is untouched
and this file not loaded.

PythonEngine.cpp PythonEngine::Initialize() adds the additional paths Naali needs for all plats.
"""

import sys
sys.path.append('pymodules/python26_Lib.zip') #python modules from folder python26/Lib
#apparently setting PYTHONHOME puts bin\python26.zip already in path - probably we should switch to that.

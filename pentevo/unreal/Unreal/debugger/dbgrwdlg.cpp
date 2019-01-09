#include "std.h"
#include "emul.h"
#include "vars.h"
#include "debug.h"
#include "dbgtrace.h"
#include "util.h"
#include "consts.h"

/*
	 dialogs design

旼컴컴컴컴컴컴컴컴컴컴컴�
쿝ead from TR-DOS file  �
쳐컴컴컴컴컴컴컴컴컴컴컴�
쿭rive: A               �
쿯ile:  12345678 C      �
퀂tart: E000 end: FFFF  �
읕컴컴컴컴컴컴컴컴컴컴컴�

旼컴컴컴컴컴컴컴컴컴컴컴�
쿝ead from TR-DOS sector�
쳐컴컴컴컴컴컴컴컴컴컴컴�
쿭rive: A               �
퀃rk (00-9F): 00        �
퀂ec (00-0F): 08        �
퀂tart: E000 end: FFFF  �
읕컴컴컴컴컴컴컴컴컴컴컴�

旼컴컴컴컴컴컴컴컴컴컴컴�
쿝ead RAW sectors       �
쳐컴컴컴컴컴컴컴컴컴컴컴�
쿭rive: A               �
쿬yl (00-4F): 00 side: 0�
퀂ec (00-0F): 08 num: 01�
퀂tart: E000            �
읕컴컴컴컴컴컴컴컴컴컴컴�

旼컴컴컴컴컴컴컴컴컴컴컴�
쿝ead from host file    �
쳐컴컴컴컴컴컴컴컴컴컴컴�
쿯ile: 12345678.bin     �
퀂tart: C000 end: FFFF  �
읕컴컴컴컴컴컴컴컴컴컴컴�

*/






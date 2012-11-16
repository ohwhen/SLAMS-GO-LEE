#include "config.h"

#if (DEBUG_LEVEL >= INFO)
	#define info(message) Serial.print(message);
	#define infoln(message) Serial.println(message);
#else
	#define info(message)
	#define infoln(message)
#endif

#if (DEBUG_LEVEL >= LOG)
	#define log(message) Serial.print(message);
	#define logln(message) Serial.println(message);
#else
	#define log(message)
	#define logln(message)
#endif

#if (DEBUG_LEVEL >= ERROR)
	#define error(message) Serial.print(message);
	#define errorln(message) Serial.println(message);
#else
	#define error(message)
	#define errorln(message)
#endif
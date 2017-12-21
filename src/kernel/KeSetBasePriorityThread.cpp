#include "common.h"

/*
 * KeSetBasePriorityThread
 *
 * Import Number:      143
 * Calling Convention: stdcall
 * Parameter 0:        PKTHREAD Thread
 * Parameter 1:        LONG     Increment
 * Return Type:        LONG
 */
int Xbox::KeSetBasePriorityThread()
{
	K_ENTER_STDCALL();
	K_INIT_ARG(PKTHREAD, Thread);
	K_INIT_ARG(LONG,     Increment);
	LONG rval;

	K_EXIT_WITH_VALUE(rval);
	return ERROR_NOT_IMPLEMENTED;
}
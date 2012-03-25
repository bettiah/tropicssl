#include <tropicssl/timing.h>
#include <personality/example/personality.h>

unsigned long hardclock( void )
{
    return tick_count();
}
unsigned long get_timer( struct hr_time *val, int reset )
{
    return tick_count();
}
/*
void set_alarm( int seconds )
{    
}
*/
void m_sleep( int milliseconds )
{
    delay(milliseconds);
}

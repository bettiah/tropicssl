#include <tropicssl/timing.h>
#include <personality/example/personality.h>

unsigned long hardclock( void )
{
    return tick_count() * 72000UL;
}
unsigned long get_timer( struct hr_time *val, int reset )
{
    return tick_count();
}
void m_sleep( int milliseconds )
{
    delay(milliseconds);
}

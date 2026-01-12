
#include <lvgl.h>
#include "ui_ClockScreen.h"


/* // Function to initialize the clock functionality
void clock_init(void);

// Function to update the clock hands
void clock_update(lv_timer_t *timer);

// Function to start the clock timer
void start_clock_timer(void); */
void clock_init(void);
void clock_update(lv_timer_t * timer);



extern int hour_value;   // Value from 0 to 11
extern int minute_value; // Value from 0 to 59
extern int second_value; // Value from 0 to 59

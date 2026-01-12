#include "clock.h"
#include "ui.h"
#include <time.h>



int hour_value = 0;
int minute_value = 0;
int second_value = 0;


void clock_init(void) {
    // Create a timer that updates the clock every second
    lv_timer_create(clock_update, 1000, NULL); // 1000 ms = 1 second
}

void clock_update(lv_timer_t * timer) {
    LV_UNUSED(timer);

    // Get the current time
    time_t now = time(NULL);
    struct tm * current_time = localtime(&now);

    // Update global variables
    hour_value = current_time->tm_hour % 12; // 0 to 11
    minute_value = current_time->tm_min;     // 0 to 59
    second_value = current_time->tm_sec;     // 0 to 59

    // Notify screens to update
    update_clock_screen();
    update_main_screen();
}
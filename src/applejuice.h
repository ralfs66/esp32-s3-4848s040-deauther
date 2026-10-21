#ifndef APPLEJUICE_H
#define APPLEJUICE_H

void applejuice_init();
void start_attack_task();
void stop_attack_task();

extern volatile unsigned int applejuice_cycles;

#endif

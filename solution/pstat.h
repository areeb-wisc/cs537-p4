#ifndef __PSTAT_H
#define __PSTAT_H

#include "param.h"

struct pstat {
  int inuse[NPROC];      // Whether this slot of the process table is in use (1 or 0)
  int tickets[NPROC];    // Number of tickets for each process
  int pid[NPROC];        // PID of each process
  int pass[NPROC];       // Pass value of each process
  int remain[NPROC];     // Remain value of each process
  int stride[NPROC];     // Stride value for each process
  int rtime[NPROC];      // Total running time of each process
};

int getpinfo(struct pstat* a) {
    return 0;
}

int settickets(int n) {
    return 0;
}

#endif
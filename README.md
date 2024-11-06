# CS537 Fall 2024, Project 4

## Author
Name - Mohammad Areeb\
CS Login - areeb@cs.wisc.edu\
Net ID - 9087095403\
Email - areeb2@wisc.edu

## Status
Working as intended

## Explaination

We can compare the last measurement of both schedulers.

### RR scheduler
| PID | Tickets | Pass | Stride | Runtime |
|-----|---------|------|--------|---------|
| 5   | 0       | 0    | 0      | 3612    |
| 4   | 0       | 0    | 0      | 38      |
| 6   | 0       | 0    | 0      | 3611    |
| 7   | 0       | 0    | 0      | 3611    |
| 8   | 0       | 0    | 0      | 3611    |
| 9   | 0       | 0    | 0      | 3611    |
| 10  | 0       | 0    | 0      | 3611    |
| 11  | 0       | 0    | 0      | 3611    |
| 12  | 0       | 0    | 0      | 3611    |
| 13  | 0       | 0    | 0      | 3610    |
| 14  | 0       | 0    | 0      | 3610    |
| 15  | 0       | 0    | 0      | 2130    |
| 16  | 0       | 0    | 0      | 2130    |
| 17  | 0       | 0    | 0      | 2129    |
| 18  | 0       | 0    | 0      | 2129    |
| 19  | 0       | 0    | 0      | 2129    |
| 20  | 0       | 0    | 0      | 2129    |

All processes that start together essentially have the same runtime, as expected.\
RR scheduler just goes over them one by one, so they all get equal CPU time.

### Stride scheduler
| PID | Tickets | Pass  | Stride | Runtime |
|-----|---------|-------|--------|---------|
| 5   | 1       | 25946 | 1024   | 24      |
| 4   | 8       | 12734 | 128    | 29      |
| 6   | 2       | 25422 | 512    | 48      |
| 7   | 4       | 25140 | 256    | 94      |
| 8   | 8       | 25027 | 128    | 188     |
| 9   | 16      | 24986 | 64     | 376     |
| 10  | 32      | 24944 | 32     | 751     |
| 11  | 32      | 24959 | 32     | 751     |
| 12  | 32      | 24971 | 32     | 751     |
| 13  | 32      | 24945 | 32     | 750     |
| 14  | 32      | 24970 | 32     | 746     |
| 15  | 32      | 24968 | 32     | 358     |
| 16  | 32      | 24974 | 32     | 358     |
| 17  | 8       | 24994 | 128    | 89      |
| 18  | 2       | 25179 | 512    | 22      |
| 19  | 8       | 24998 | 128    | 89      |
| 20  | 8       | 24998 | 128    | 89      |

All processes have CPU runtime proportional to their tickets.\
All processes have similar pass values (because they all sync with global pass on each leave/rejoin)

**Advantage of stride scheduler** - heavier workloads get proportionately more CPU time when compared to RR scheduler.
